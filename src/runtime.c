#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "runtime.h"
#include "deque_list_tl.h"
// Disable unit tests
#include "utest.h"
UTEST_MAIN() {}
#include "timer.h"

#define MANAGER	if (is_manager)
#define MAXNP 256
#define PARTITIONS 1
#include "partition.c"

#define LOG(...) { printf(__VA_ARGS__); fflush(stdout); }
#define UNUSED(x) x __attribute__((unused))

static PRIVATE DequeListTL *deque;

// Manager/worker -> manager: inter-partition steal requests (MPSC)
// chan_manager and chan_requests could be merged, but only if we don't care
// about mutlithreading managers
static Channel *chan_manager[MAXNP];

// Manager/worker -> worker: intra-partition steal requests (MPSC)
static Channel *chan_requests[MAXNP];

// Worker -> worker: tasks (SPSC)
static Channel *chan_tasks[MAXNP];

// Manager -> manager: global quiescence detection (SPSC)
static Channel *chan_quiescence[MAXNP];

// Manager -> master: notify about global quiescence
static Channel *chan_barrier;

struct token {
	int sender;		// ID of sender (one of the managers)
	int val;		// 1 <= val <= num_partitions
	char __[24];	// pad to cache line
};

// One possible extension would be to be able to request more than one task
// at a time. No need to change the work-stealing algorithm.
// Another idea would be to be able to request a task from a specific worker,
// for example, in order to implement leapfrogging. A problem might be that
// handling such a request could also delay other requests.
struct steal_request {
	int ID;			// ID of requesting worker
	int try;	   	// 0 <= try <= num_workers_rt
	int pass;	   	// 0 <= pass <= num_partitions
	int partition; 	// partition in which the steal request was initiated
	int pID;		// ID of requesting worker within partition
	bool idle;		// ID has nothing left to work on?
	bool quiescent;	// manager assumes ID is in a quiescent state?
#ifdef STEAL_ADAPTIVE
	bool stealhalf; // true ? attempt steal-half : attempt steal-one
	char __[9];     // pad to cache line
#else
	char __[10];   	// pad to cache line
#endif
};

static inline void print_steal_req(struct steal_request *req)
{
	LOG("{ .ID = %d, .try = %d, .pass = %d, .partition = %d, .pID = %d, .idle = %s, .quiescent = %s }\n",
			req->ID, req->try, req->pass, req->partition, req->pID,
			req->idle == true ? "Y" : "N", req->quiescent == true ? "Y" : "N");
}

// .try == 0, .pass == 0, .idle == false, .quiescence == false
static PRIVATE struct steal_request steal_req;

// A worker can have only one outstanding steal request
static PRIVATE bool requested;

#ifdef STEAL_LASTVICTIM
// ID of last victim
static PRIVATE int last_victim = -1;
#endif

#ifdef STEAL_LEAPFROG
// ID of last thief
static PRIVATE int last_thief = -1;
#endif

// Every worker keeps a list of victims that can be read by other workers
// Shared state!
static int *victims[MAXNP];

// Private copy of victims field
static PRIVATE int *my_victims;

// A worker has a unique ID within its partition
// 0 <= pID <= num_workers_rt
static PRIVATE int pID;

struct worker_info {
	int coreID, rank, hops_away;
};

// Only manager threads perform load balancing
// workers field is uninitialized for other threads
// We calculate victim information at initialization time in order to reduce
// the overhead of finding victims at runtime
static PRIVATE struct worker_info workers[MAXNP][MAXNP];

static void init_victims(int ID)
{
	int available_workers[num_workers];
	int i, j;

	// Get all available workers in my_partition
	for (i = 0, j = 0; i < my_partition->num_workers; i++) {
		int worker = my_partition->workers[i];
		if (worker < num_workers) {
			available_workers[j] = worker;
			my_partition->num_workers_rt++;
			j++;
		}
	}

	// This is only really needed for latency-oriented work-stealing
	// We need a different "latency map" depending on which worker is thief
	// In random work-stealing, we don't care which worker is thief because
	// we always select a victim at random
	for (i = 0; i < num_workers; i++) {
		for (j = 0; j < my_partition->num_workers_rt; j++) {
			int worker = available_workers[j];
			workers[i][j].rank = worker;
		}
	}

	// my_victims contains all possible victims in my_partition
	for (i = 0, j = 0; i < my_partition->num_workers_rt; i++) {
		if (workers[ID][i].rank != ID) {
			my_victims[j] = workers[ID][i].rank;
			//LOG("Worker %2d: victim[%d] = %d\n", ID, j, my_victims[j]);
			j++;
		}
	}

	// We put our own ID there because eventually, after N unsuccessful tries
	// (N being the number of potential victims in my_partition), the steal
	// request must go back to the thief. Quiescence detection depends on that.
	my_victims[j] = ID;

	MANAGER LOG("Manager %2d: %d of %d workers available\n", ID,
			my_partition->num_workers_rt-1, my_partition->num_workers);
}

static PRIVATE unsigned int seed;

#ifdef STEAL_RANDOM
static void swap(int *a, int *b)
{
	int tmp = *a;
	*a = *b;
	*b = tmp;
}

// Fisher-Yates shuffle
static void shuffle(int *workers, int len)
{
	int rand, i;
	for (i = len-1; i > 0; i--) {
		rand = rand_r(&seed) % (i + 1);
		swap(&workers[rand], &workers[i]);
	}
}
static inline void shuffle_victims()
{
	shuffle(my_victims, my_partition->num_workers_rt-1);
	my_victims[my_partition->num_workers_rt-1] = ID;
}
#endif // STEAL_RANDOM

static inline void copy_victims()
{
	// Update shared copy of my_victims
	memcpy(victims[ID], my_victims, my_partition->num_workers_rt * sizeof(int));
}

#ifdef STEAL_RANDOM_RR
// Arrange the victims in my_victims so that visiting them in order
// corresponds to round robin / next neighbor
static inline void order_victims()
{
	int vs[my_partition->num_workers_rt];
	int i = 0, j = my_partition->num_workers_rt-1;

	while (my_victims[i] < ID) {
		i++; j--;
	}

	assert(my_victims[i] >= ID);
	assert(j >= 0);

	if (my_victims[i] == 0 || my_victims[i] == ID) {
		// my_victims is already correctly ordered
		if (my_victims[i] == ID) {
			assert(i == my_partition->num_workers_rt-1);
			assert(j == 0);
		}
		return;
	}

	assert(my_victims[i] > ID);
	assert(j > 0);

	// Save the first i victims that must be moved to the end of my_victims
	memcpy(vs, my_victims, i * sizeof(int));
	// Move the remaining j victims to the front of my_victims
	memmove(my_victims, my_victims + i, j * sizeof(int));
	// Copy the other victims back into place
	memcpy(my_victims + j, vs, i * sizeof(int));

	assert(my_victims[my_partition->num_workers_rt-1] == ID);
}
#endif // STEAL_RANDOM_RR

// Initializes context needed for work-stealing
static int ws_init(void)
{
	seed = ID;
	init_victims(ID);
#ifdef STEAL_RANDOM
	shuffle_victims();
#else // STEAL_RANDOM_RR
	order_victims();
#endif
	// Not strictly needed in case of STEAL_RANDOM_RR: no shared state
	copy_victims();

	return 0;
}

#ifndef NTIME
#ifdef __MIC__
#define CPUFREQ 1.053 // in GHz
#else
#define CPUFREQ 2.1 // in GHZ
#endif
// To measure the cost of different parts of the runtime
PRIVATE mytimer_t timer_run_tasks;
PRIVATE mytimer_t timer_enq_deq_tasks;
PRIVATE mytimer_t timer_send_recv_tasks;
PRIVATE mytimer_t timer_send_recv_sreqs;
PRIVATE mytimer_t timer_idle;
PRIVATE mytimer_t timer_check;
#endif

PRIVATE unsigned int requests_sent, requests_handled;
PRIVATE unsigned int requests_declined, tasks_sent;

int RT_init(void)
{
	// Small sanity checks
	// At this point, we have not yet decided who will be manager(s)
	assert(is_manager == false);
	assert(sizeof(struct steal_request) == 32);
	assert(sizeof(Task) == 192);

	int i;

#if PARTITIONS == 1
	PARTITION_ASSIGN_xlarge(1);
#elif PARTITIONS == 2
	PARTITION_ASSIGN_X(2);
	PARTITION_ASSIGN_Y(1);
#endif
	PARTITION_SET();

	//MANAGER LOG("Manager %d --> Manager %d\n", ID, next_manager);
	//LOG("Worker %d: in partition %d\n", ID, my_partition->number);

	deque = deque_list_tl_new();

	chan_requests[ID] = channel_alloc(sizeof(struct steal_request), num_workers, MPSC);
	chan_tasks[ID] = channel_alloc(sizeof(Task *), 2, SPSC);

	MANAGER {
		chan_manager[ID] = channel_alloc(sizeof(struct steal_request), num_workers, MPSC);
		chan_quiescence[ID] = channel_alloc(sizeof(struct token), 0, SPSC);
	}

	MASTER {
		chan_barrier = channel_alloc(sizeof(bool), 0, SPSC);
	}

	victims[ID] = (int *)malloc(MAXNP * sizeof(int));
	my_victims = (int *)malloc(MAXNP * sizeof(int));

	ws_init();

	for (i = 0; i < my_partition->num_workers_rt; i++) {
		if (ID == my_partition->workers[i]) {
			pID = i;
			break;
		}
	}

	steal_req.ID = ID;
	steal_req.partition = my_partition->number;
	steal_req.pID = pID;

	timer_new(&timer_run_tasks, CPUFREQ);
	timer_new(&timer_enq_deq_tasks, CPUFREQ);
	timer_new(&timer_send_recv_tasks, CPUFREQ);
	timer_new(&timer_send_recv_sreqs, CPUFREQ);
	timer_new(&timer_idle, CPUFREQ);
	timer_new(&timer_check, CPUFREQ);

	return 0;
}

int RT_exit(void)
{
	deque_list_tl_delete(deque);

	free(victims[ID]);
	free(my_victims);

	channel_free(chan_requests[ID]);
	channel_free(chan_tasks[ID]);

	MANAGER {
		channel_free(chan_manager[ID]);
		channel_free(chan_quiescence[ID]);
	}

	MASTER {
		channel_free(chan_barrier);
	}

	return 0;
}

static inline bool is_in_my_partition(int ID)
{
	int *w;

	foreach_worker(w, my_partition)
		if (*w == ID)
			return true;

	return false;
}

Task *task_alloc(void)
{
	return deque_list_tl_task_new(deque);
}

#ifdef VICTIM_CHECK

#define LIKELY_HAS_TASKS(ID) (!channel_closed(chan_requests[ID]))
#define REQ_CLOSE()            channel_close(chan_requests[ID])
#define REQ_OPEN()             channel_open(chan_requests[ID])

#else

#define LIKELY_HAS_TASKS(ID)   true      // Assume yes, victim has tasks
#define REQ_CLOSE()            ((void)0) // NOOP
#define REQ_OPEN()             ((void)0) // NOOP

#endif

#ifdef STEAL_RANDOM
static inline int next_victim(struct steal_request *req)
{
	int victim, i;

	for (i = req->try; i < my_partition->num_workers_rt-1; i++) {
		victim = victims[req->ID][i];
		if (victim != my_partition->manager && LIKELY_HAS_TASKS(victim)) {
			//assert(is_in_my_partition(victim));
			//LOG("Worker %d: Found victim after %i tries\n", ID, i);
			return victim;
		}
		req->try++;
	}

	victim = victims[req->ID][i];
	assert(victim == req->ID);

	return victim;
}
#endif // STEAL_RANDOM

#ifdef STEAL_RANDOM_RR
// Chooses the first victim at random
static inline int random_victim(struct steal_request *req)
{
	// Assumption: Beginning of a new round of steal attempts
	assert(req->try == 0);

	int victim = -1;

	// No other workers in this partition besides the manager?
	if (my_partition->num_workers_rt-1 == 1) {
		// Skip manager
		req->try++;
		return req->ID;
	}

	do {
		int rand = rand_r(&seed) % (my_partition->num_workers_rt-1);
		victim = my_victims[rand];
		assert(victim != ID);
	} while (victim == req->ID || victim == my_partition->manager);

	//assert(is_in_my_partition(victim));

	return victim;
}

static inline int next_victim(struct steal_request *req)
{
	int victim, i;

	assert(req->try > 0 && req->try <= my_partition->num_workers_rt-1);

	if (req->try == my_partition->num_workers_rt-1) {
		return req->ID;
	}

	// Check all potential victims (excluding ID)
	// Skip req->ID and my_partition->manager. Skipping my_partition->manager
	// requires incrementing req->try, as if we attempted a steal but failed.
	for (i = 0; i < my_partition->num_workers_rt-1,
			    req->try < my_partition->num_workers_rt-1; i++) {
		victim = my_victims[i];
		if (victim == my_partition->manager) {
			req->try++;
		} else if (victim != req->ID) {
			if (LIKELY_HAS_TASKS(victim)) {
				return victim;
			} else {
				req->try++;
			}
		} else {
			assert(victim == req->ID);
		}
	}

	// Steal request has passed each worker exactly once; send it back
	assert(req->try == my_partition->num_workers_rt-1);
	return req->ID;
}
#endif // STEAL_RANDOM_RR

#ifdef STEAL_LASTVICTIM
#ifndef STEAL_RANDOM
#error "STEAL_LASTVICTIM depends on STEAL_RANDOM"
#endif
static inline int lastvictim(struct steal_request *req)
{
	int victim;

	if (req->try < my_partition->num_workers_rt-1) {
		if (last_victim != -1 && last_victim != req->ID) {
			victim = last_victim;
			assert(victim != my_partition->manager);
			return victim;
		}
		// Fall back to random victim selection
		return next_victim(req);
	}

	victim = victims[req->ID][my_partition->num_workers_rt-1];
	assert(victim == req->ID);

	return victim;
}
#endif // STEAL_LASTVICTIM

#ifdef STEAL_LEAPFROG
#ifndef STEAL_RANDOM
#error "STEAL_LEAPFROG depends on STEAL_RANDOM"
#endif
static inline int leapfrog(struct steal_request *req)
{
	int victim;

	if (req->try < my_partition->num_workers_rt-1) {
		if (last_thief != -1 && last_thief != req->ID) {
			victim = last_thief;
			assert(victim != my_partition->manager);
			return victim;
		}
		// Fall back to random victim selection
		return next_victim(req);
	}

	victim = victims[req->ID][my_partition->num_workers_rt-1];
	assert(victim == req->ID);

	return victim;
}
#endif // STEAL_LEAPFROG

static inline void UPDATE(void);
static inline void send_steal_request(bool idle);
static inline void decline_steal_request(struct steal_request *);
static inline void decline_all_steal_requests(void);
static inline void split_loop(Task *, struct steal_request *);

#define SEND_REQ(chan, req) \
do { \
	int __nfail = 0; \
	/* Problematic if the target worker has already left scheduling */\
	/* ==> send to full channel will block the sender */\
	while (!channel_send(chan, req, sizeof(*(req)))) { \
		if (++__nfail % 100 == 0) { \
			LOG("*** Worker %d: blocked on channel send\n", ID); \
			assert(false && "Check channel capacities!"); \
		} \
		if (tasking_done()) break; \
	} \
} while (0)

#define SEND_REQ_WORKER(ID, req)	SEND_REQ(chan_requests[ID], req)
#define SEND_REQ_MANAGER(req)		SEND_REQ(chan_manager[my_partition->manager], req)
#define SEND_REQ_PARTITION(req)		SEND_REQ(chan_manager[next_manager], req)

#if 0
#define RECV_REQ(req) \
	channel_receive(chan_requests[ID], req, sizeof(*(req)))
#endif

static inline bool RECV_REQ(struct steal_request *req)
{
	bool ret;

	timer_start(&timer_send_recv_sreqs);

	ret = channel_receive(chan_requests[ID], req, sizeof(*(req)));

	timer_end(&timer_send_recv_sreqs);

	return ret;
}

#define SEND_QSC_MSG(tok) \
{ \
	/* Problematic if the target worker has already left scheduling */\
	/* ==> send to full channel will block the sender */\
	while (!channel_send(chan_quiescence[next_manager], tok, sizeof(*(tok)))) { \
		if (tasking_done()) break; \
	} \
}

#define RECV_QSC_MSG(tok) \
{ \
	/* Problematic if the sender has already terminated */\
	while (!channel_receive(chan_quiescence[ID], tok, sizeof(*(tok)))) { \
		if (tasking_done()) break; \
	} \
}

// Global quiescence detection
// Initiated when partition is quiescent
static bool global_quiescence(void)
{
	struct token tok = { .sender = ID, .val = 1 };

	if (num_partitions == 1)
		return true;

	// Send and wait until we get the token back
	SEND_QSC_MSG(&tok);

	for (;;) {
		RECV_QSC_MSG(&tok);
		if (tok.sender == ID) break;
		tok.val++;
		SEND_QSC_MSG(&tok);
		if (tasking_done()) break;
	}

	return tok.val == num_partitions;
}

static int loadbalance(void)
{
	// Manager determines when a partition has reached quiescence
	// Once all partitions have reached quiescence, termination is detected
	bool quiescent = false;
	bool after_barrier = false;
	// 0 <= num_workers_q <= num_workers_rt-1
	int num_workers_q, workers_q[my_partition->num_workers_rt], i;
	struct steal_request req;
	struct token tok;

	int notes = 0;

	num_workers_q = 0;
	for (i = 0; i < my_partition->num_workers_rt; i++)
		workers_q[i] = 0;

	// Load balancing loop
	for (;;) {
		if (channel_receive(chan_manager[ID], &req, sizeof(req))) {
		// Right after a barrier, we must take extra measures to avoid
		// premature quiescence detection. We need to filter out
		// the old steal request from the master.
		if (after_barrier) {
			// In a state of global quiescence no request is sent across partitions
			assert(my_partition->number == 0);
			assert(req.partition == my_partition->number);
			assert(num_workers_q == my_partition->num_workers_rt-1);
			if (req.ID == MASTER_ID) {
				if (workers_q[req.pID] && !req.quiescent) {
					workers_q[req.pID] = 0;
					num_workers_q--;
					assert(quiescent);
					quiescent = false;
					after_barrier = false;
					notes++;
					continue;
				} else {
					assert(req.quiescent);
					SEND_REQ_WORKER(MASTER_ID, &req);
					continue;
				}
			}
		}

		// Quiescence information out of date?
		if (req.partition == my_partition->number && workers_q[req.pID] && !req.quiescent) {
			assert(num_workers_q > 0);
			workers_q[req.pID] = 0;
			num_workers_q--;
			quiescent = false;
			notes++;
			continue;
		}
		switch (quiescent) {
			/////////////////////////////////////////////////////////////////
			// No quiescence                                               //
			/////////////////////////////////////////////////////////////////
			case false:
				if (req.try == 0) {
					// Steal request from previous partition
					assert(req.pass > 0);
					if (req.partition == my_partition->number) {
						assert(req.pass == num_partitions);
						if (req.idle) {
							assert(num_workers_q < my_partition->num_workers_rt-1);
							assert(!workers_q[req.pID]);
							workers_q[req.pID] = 1;
							req.quiescent = true;
							num_workers_q++;
						}
					}
					// Pass steal request on to random victim
#ifdef STEAL_RANDOM_RR
					SEND_REQ_WORKER(random_victim(&req), &req);
#else // STEAL_RANDOM
					if (victims[req.ID][0] != ID) {
						SEND_REQ_WORKER(victims[req.ID][0], &req);
					} else {
						SEND_REQ_WORKER(victims[req.ID][1], &req);
					}
#endif
					requests_declined++;
				} else {
					// Steal request made full circle within partition
					assert(req.try == my_partition->num_workers_rt);
					req.try = 0;
					// Have we seen this steal request before?
					if (req.partition == my_partition->number) {
						if (req.pass == num_partitions) {
							if (req.idle && !req.quiescent) {
								assert(num_workers_q < my_partition->num_workers_rt-1);
								assert(!workers_q[req.pID]);
								workers_q[req.pID] = 1;
								req.quiescent = true;
								num_workers_q++;
							}
							// Pass it back to partition
#ifdef STEAL_RANDOM_RR
							SEND_REQ_WORKER(random_victim(&req), &req);
#else // STEAL_RANDOM
							if (victims[req.ID][0] != ID) {
								SEND_REQ_WORKER(victims[req.ID][0], &req);
							} else {
								SEND_REQ_WORKER(victims[req.ID][1], &req);
							}
#endif
							requests_declined++;
						} else { // No, this is a new steal request from our partition
							assert(req.pass == 0);
							assert(!req.quiescent);
							assert(!workers_q[req.pID]);
							// Pass request on to neighbor partition
							// Note: We could optimize away the send in the case
							// of only one partition, but performance doesn't
							// seem to benefit.
							req.pass++;
							SEND_REQ_PARTITION(&req);
							requests_declined++;
						}
					} else { // Steal request from remote partition
						// Pass request on to neighbor partition
						if (req.pass < num_partitions)
							req.pass++;
						SEND_REQ_PARTITION(&req);
						requests_declined++;
					}
				}
				break;
			/////////////////////////////////////////////////////////////////
			// Quiescence                                                  //
			/////////////////////////////////////////////////////////////////
			case true:
				if (req.partition == my_partition->number) {
					assert(req.try == my_partition->num_workers_rt);
					assert(req.pass == num_partitions);
					assert(req.idle);
					assert(req.quiescent);
					req.try = 0;
					// Initiate global quiescence detection
					if (!global_quiescence()) {
						req.quiescent = false;
						workers_q[req.pID] = 0;
						num_workers_q--;
						quiescent = false;
						SEND_REQ_PARTITION(&req);
						requests_declined++;
					} else {
						if (my_partition->number == 0 && !after_barrier) {
							// Assert global quiescence
							// Shake hands with master worker
							channel_send(chan_barrier, &quiescent, sizeof(quiescent));
							while (channel_peek(chan_barrier)) ;
							after_barrier = true;
						}
#ifdef STEAL_RANDOM_RR
						SEND_REQ_WORKER(random_victim(&req), &req);
#else // STEAL_RANDOM
						if (victims[req.ID][0] != ID) {
							SEND_REQ_WORKER(victims[req.ID][0], &req);
						} else {
							SEND_REQ_WORKER(victims[req.ID][1], &req);
						}
#endif
						requests_declined++;
					}
				} else {
					assert(req.try == 0 || req.try == my_partition->num_workers_rt);
					assert(!is_in_my_partition(req.ID));
					assert(!req.quiescent);
					req.try = 0;
					// We know that stealing would fail, so we just pretend
					if (req.pass < num_partitions)
						req.pass++;
					SEND_REQ_PARTITION(&req);
					requests_declined++;
				}
				break;
		}}
		if (tasking_done())
			break;
		if (num_workers_q == my_partition->num_workers_rt-1 && !quiescent) {
			// Transition to quiescent
			quiescent = true;
		}
		// Token passing for global quiescence detection
		if (channel_receive(chan_quiescence[ID], &tok, sizeof(tok))) {
			assert(tok.val >= 1 && tok.val <= num_partitions);
			if (quiescent)
				tok.val++;
			SEND_QSC_MSG(&tok);
		}
		if (tasking_done())
			break;
	}

	LOG("Manager received %d notifications\n", notes);

	return 0;
}

#ifdef STEAL_ADAPTIVE
// Switch from steal-one to steal-half after executing STEAL_HALF_THRESHOLD
// flat tasks (tasks that don't create child tasks) in series.
// Switch back to steal-one as soon as we create the next child task.
#define STEAL_HALF_THRESHOLD 10
static PRIVATE unsigned int flat_tasks_in_series;
static PRIVATE bool stealhalf;
#endif

// Send steal request when number of local tasks <= REQ_THRESHOLD
// Steal requests are always sent before actually running out of tasks.
// REQ_THRESHOLD == 0 means that we send a steal request just _before_ we
// start executing the last task in the queue.
#define REQ_THRESHOLD 0

static inline void UPDATE(void)
{
	if (num_workers == 1)
		return;

	timer_start(&timer_send_recv_sreqs);

	if (deque_list_tl_num_tasks(deque) <= REQ_THRESHOLD) {
#ifdef STEAL_ADAPTIVE
		if (!stealhalf && flat_tasks_in_series >= STEAL_HALF_THRESHOLD) {
			//LOG("Worker %d switches to steal-half\n", ID);
			stealhalf = true;
		}
#endif
		send_steal_request(false);
	}

	timer_end(&timer_send_recv_sreqs);
}

// We make sure that there is at most one outstanding steal request per worker
// New steal requests are created with idle == false, to show that the
// requesting worker is still working on a number of remaining tasks.
static inline void send_steal_request(bool idle)
{
	if (!requested) {
		steal_req.idle = idle;
		steal_req.try = 0;
#ifdef STEAL_ADAPTIVE
		steal_req.stealhalf = stealhalf;
#endif
#ifdef STEAL_RANDOM
		shuffle_victims();
		copy_victims();
#ifdef STEAL_LASTVICTIM
		SEND_REQ_WORKER(lastvictim(&steal_req), &steal_req);
#elif defined STEAL_LEAPFROG
		SEND_REQ_WORKER(leapfrog(&steal_req), &steal_req);
#else
		SEND_REQ_WORKER(next_victim(&steal_req), &steal_req);
#endif
#else // STEAL_RANDOM_RR
		SEND_REQ_WORKER(random_victim(&steal_req), &steal_req);
#endif
		requested = true;
		requests_sent++;
	}
}

// Got a steal request that can't be served?
// Pass it on to a different victim, or send it back to manager to prevent circling
static inline void decline_steal_request(struct steal_request *req)
{
	requests_declined++;
	req->try++;
	if (req->try < my_partition->num_workers_rt) {
		if (my_partition->num_workers_rt > 2) {
			assert(ID != req->ID);
		}
		SEND_REQ_WORKER(next_victim(req), req);
	} else {
		assert(ID == req->ID);
		if (ID != MASTER_ID && req->quiescent) {
			// Don't bother manager; we are quiescent anyway
			req->try = 0;
#ifdef STEAL_RANDOM
			SEND_REQ_WORKER(next_victim(req), req);
#else // STEAL_RANDOM_RR
			SEND_REQ_WORKER(random_victim(req), req);
#endif
		} else {
			SEND_REQ_MANAGER(req);
		}
	}
}

static inline void decline_all_steal_requests(void)
{
	struct steal_request req;

	timer_end(&timer_idle);

	if (RECV_REQ(&req)) {
		timer_start(&timer_send_recv_sreqs);
		if (req.ID == ID && !req.idle) {
			req.idle = true;
		}
		decline_steal_request(&req);
		timer_end(&timer_send_recv_sreqs);
	}

	timer_start(&timer_idle);
}

static void handle_steal_request(struct steal_request *req)
{
	Task *task, *tail;
	int loot = 1;

	if (req->ID == ID) {
		// Got own steal request
		// Forget about it if we have more tasks than previously
		if (deque_list_tl_num_tasks(deque) > REQ_THRESHOLD) {
			assert(!req->quiescent);
			assert(requested);
			requested = false;
			return;
		} else {
			timer_start(&timer_send_recv_sreqs);
			decline_steal_request(req); // => send to manager
			timer_end(&timer_send_recv_sreqs);
			return;
		}
	}
	assert(req->ID != ID);
	timer_start(&timer_send_recv_tasks);
#ifdef STEAL_ADAPTIVE
	if (req->stealhalf) {
		task = deque_list_tl_steal_half(deque, &tail, &loot);
	} else {
		task = deque_list_tl_steal(deque);
	}
#elif defined STEAL_HALF
	task = deque_list_tl_steal_half(deque, &tail, &loot);
#else // Default is steal-one
	task = deque_list_tl_steal(deque);
#endif
	if (task) {
		if (req->quiescent) {
			assert(req->idle);
			assert(req->pass == num_partitions);
			assert(req->partition == my_partition->number);
			req->quiescent = false;
			SEND_REQ_MANAGER(req);
		}
		task->batch = loot;
#ifdef STEAL_LASTVICTIM
		task->victim = ID;
#endif
		channel_send(chan_tasks[req->ID], (void *)&task, sizeof(Task *));
		if (loot > 1) {
			channel_send(chan_tasks[req->ID], (void *)&tail, sizeof(Task *));
		}
		//LOG("Worker %2d: sending %d tasks to worker %d\n", ID, loot, req->ID);
		requests_handled++;
		tasks_sent += loot;
#ifdef STEAL_LEAPFROG
		last_thief = req->ID;
#endif
		timer_end(&timer_send_recv_tasks);
	} else {
		// Got steal request, but can't serve it
		// Pass it on to someone else
		timer_end(&timer_send_recv_tasks);
		assert(deque_list_tl_empty(deque));
		timer_start(&timer_send_recv_sreqs);
		REQ_CLOSE();
		decline_steal_request(req);
		timer_end(&timer_send_recv_sreqs);
	}
}

// Loop task with iterations left for splitting?
#define SPLITTABLE(t) \
	((bool)((t) != NULL && (t)->is_loop && abs((t)->end - (t)->cur) > 1))

int RT_check_for_steal_requests(void)
{
	Task *this = get_current_task();
	struct steal_request req;
	int n = 0;

	timer_end(&timer_run_tasks);

	if (!channel_peek(chan_requests[ID])) {
		timer_start(&timer_run_tasks);
		return 0;
	}

	// Check if someone requested to steal from us
	while (RECV_REQ(&req)) {
		// (1) Send task(s) if possible
		// (2) Split current task if possible
		// (3) Decline (or ignore) steal request
		if (!deque_list_tl_empty(deque)) {
			handle_steal_request(&req);
		} else if (SPLITTABLE(this) && req.ID != ID) {
			split_loop(this, &req);
		} else {
			handle_steal_request(&req);
		}
		n++;
	}

	timer_start(&timer_run_tasks);

	return n;
}

// Executed by worker threads
void *schedule(UNUSED(void *args))
{
	Task *task, *tail;
	int loot;

	//timer_start(&timer_idle);

	// Scheduling loop
	for (;;) {
		// (1) Private task queue
		//timer_end(&timer_idle);
		while ((task = pop()) != NULL) {
			timer_start(&timer_enq_deq_tasks);
#ifdef STEAL_ADAPTIVE
			flat_tasks_in_series++;
#endif
			run_task(task);
			deque_list_tl_task_cache(deque, task);
			timer_end(&timer_enq_deq_tasks);
		}

		// (2) Work-stealing request
		assert(requested);
		timer_start(&timer_idle);
		while (!channel_receive(chan_tasks[ID], (void *)&task, sizeof(Task *))) {
			assert(deque_list_tl_empty(deque));
			assert(requested);
			decline_all_steal_requests();
			if (tasking_done()) {
				timer_end(&timer_idle);
				goto schedule_exit;
			}
		}
		timer_end(&timer_idle);
		timer_start(&timer_send_recv_tasks);
		loot = task->batch;
#ifdef STEAL_LASTVICTIM
		last_victim = task->victim;
		assert(last_victim != 1 && last_victim != ID);
#endif
		if (loot > 1) {
			while (!channel_receive(chan_tasks[ID], (void *)&tail, sizeof(Task *))) ;
			task = deque_list_tl_pop(deque_list_tl_prepend(deque, task, tail, loot));
			REQ_OPEN();
		}
#ifdef VICTIM_CHECK
		if (loot == 1 && SPLITTABLE(task)) {
			REQ_OPEN();
		}
#endif
		timer_end(&timer_send_recv_tasks);
		requested = false;
		//TODO Figure out at which points updates are reasonable
		//UPDATE();
		timer_start(&timer_enq_deq_tasks);
#ifdef STEAL_ADAPTIVE
		flat_tasks_in_series++;
#endif
		run_task(task);
		deque_list_tl_task_cache(deque, task);
		timer_end(&timer_enq_deq_tasks);
	}

schedule_exit:
	return 0;
}

// Executed by worker threads
int RT_schedule(void)
{
	// Managers do the load balancing (possibly in a separate thread)
	MANAGER {
		//pthread_t worker;
		//pthread_attr_t attr;
		//pthread_attr_init(&attr);
		//pthread_create(&worker, &attr, schedule, NULL);
		loadbalance();
		//pthread_join(worker, NULL);
		//pthread_attr_destroy(&attr);
	} else {
		schedule(NULL);
	}

	return 0;
}

int RT_barrier(void)
{
	WORKER return 0;

	assert(is_root_task(get_current_task()));

	Task *task, *tail;
	int loot;
	bool quiescent;

empty_local_queue:
	while ((task = pop()) != NULL) {
		timer_start(&timer_enq_deq_tasks);
#ifdef STEAL_ADAPTIVE
		flat_tasks_in_series++;
#endif
		run_task(task);
		deque_list_tl_task_cache(deque, task);
		timer_end(&timer_enq_deq_tasks);
	}

	if (num_workers == 1)
		return 0;

	assert(requested);
	timer_start(&timer_idle);
	while (!channel_receive(chan_tasks[ID], (void *)&task, sizeof(Task *))) {
		assert(deque_list_tl_empty(deque));
		assert(requested);
		decline_all_steal_requests();
		if (channel_receive(chan_barrier, &quiescent, sizeof(quiescent))) {
			assert(quiescent);
			timer_end(&timer_idle);
			goto RT_barrier_exit;
		}
	}
	timer_end(&timer_idle);
	timer_start(&timer_send_recv_tasks);
	loot = task->batch;
#ifdef STEAL_LASTVICTIM
	last_victim = task->victim;
	assert(last_victim != 1 && last_victim != ID);
#endif
	if (loot > 1) {
		while (!channel_receive(chan_tasks[ID], (void *)&tail, sizeof(Task *))) ;
		task = deque_list_tl_pop(deque_list_tl_prepend(deque, task, tail, loot));
		REQ_OPEN();
	}
#ifdef VICTIM_CHECK
	if (loot == 1 && SPLITTABLE(task)) {
		REQ_OPEN();
	}
#endif
	timer_end(&timer_send_recv_tasks);
	requested = false;
	//TODO Figure out at which points updates are reasonable
	//UPDATE();
	timer_start(&timer_enq_deq_tasks);
#ifdef STEAL_ADAPTIVE
	flat_tasks_in_series++;
#endif
	run_task(task);
	deque_list_tl_task_cache(deque, task);
	timer_end(&timer_enq_deq_tasks);
	goto empty_local_queue;

RT_barrier_exit:
	assert(!channel_peek(chan_tasks[ID]));
	// Remove own steal request to reflect the fact that the computation continues
	for (;;) {
		struct steal_request req;
		if (RECV_REQ(&req)) {
			if (req.ID == ID) {
				assert(req.idle);
				assert(req.pass == num_partitions);
				assert(req.partition == my_partition->number);
				req.quiescent = false;
				SEND_REQ_MANAGER(&req);
				assert(requested);
				requested = false;
				break;
			}
			decline_steal_request(&req);
		}
	}

	return 0;
}

void RT_force_future_channel(Channel *chan, void *data, unsigned int size)
{
	Task *task, *tail;
	Task *this = get_current_task();
	struct steal_request req;
	int loot;

	assert(channel_impl(chan) == SPSC);

	if (channel_receive(chan, data, size))
		goto RT_force_future_channel_return;

	while ((task = pop_child()) != NULL) {
		timer_start(&timer_enq_deq_tasks);
#ifdef STEAL_ADAPTIVE
		flat_tasks_in_series++;
#endif
		run_task(task);
		deque_list_tl_task_cache(deque, task);
		timer_end(&timer_enq_deq_tasks);
		if (channel_receive(chan, data, size))
			goto RT_force_future_channel_return;
	}

	assert(get_current_task() == this);

	timer_start(&timer_idle);
	while (!channel_receive(chan, data, size)) {
		timer_end(&timer_idle);
		timer_start(&timer_send_recv_sreqs);
		send_steal_request(false);
		timer_end(&timer_send_recv_sreqs);
		timer_start(&timer_idle);
		while (!channel_receive(chan_tasks[ID], (void *)&task, sizeof(Task *))) {
			assert(requested);
			timer_end(&timer_idle);
			// Check if someone requested to steal from us
			while (RECV_REQ(&req))
				handle_steal_request(&req);
			timer_start(&timer_idle);
			if (channel_receive(chan, data, size)) {
				timer_end(&timer_idle);
				goto RT_force_future_channel_return;
			}
		}
		timer_end(&timer_idle);
		timer_start(&timer_send_recv_tasks);
		loot = task->batch;
#ifdef STEAL_LASTVICTIM
		last_victim = task->victim;
		assert(last_victim != 1 && last_victim != ID);
#endif
		if (loot > 1) {
			while (!channel_receive(chan_tasks[ID], (void *)&tail, sizeof(Task *))) ;
			task = deque_list_tl_pop(deque_list_tl_prepend(deque, task, tail, loot));
			REQ_OPEN();
		}
#ifdef VICTIM_CHECK
		if (loot == 1 && SPLITTABLE(task)) {
			REQ_OPEN();
		}
#endif
		timer_end(&timer_send_recv_tasks);
		requested = false;
		//TODO Figure out at which points updates are reasonable
		//UPDATE();
		timer_start(&timer_enq_deq_tasks);
#ifdef STEAL_ADAPTIVE
		flat_tasks_in_series++;
#endif
		run_task(task);
		deque_list_tl_task_cache(deque, task);
		timer_end(&timer_enq_deq_tasks);
		timer_start(&timer_idle);
	}
	timer_end(&timer_idle);

RT_force_future_channel_return:
	return;
}

// Return when *num_children == 0
void RT_taskwait(atomic_t *num_children)
{
	Task *task, *tail;
	Task *this = get_current_task();
	struct steal_request req;
	int loot;

	if (atomic_read(num_children) == 0)
		goto RT_taskwait_return;

	while ((task = pop_child()) != NULL) {
		timer_start(&timer_enq_deq_tasks);
#ifdef STEAL_ADAPTIVE
		flat_tasks_in_series++;
#endif
		run_task(task);
		deque_list_tl_task_cache(deque, task);
		timer_end(&timer_enq_deq_tasks);
		if (atomic_read(num_children) == 0)
			goto RT_taskwait_return;
	}

	assert(get_current_task() == this);

	timer_start(&timer_idle);
	while (atomic_read(num_children) > 0) {
		timer_end(&timer_idle);
		timer_start(&timer_send_recv_sreqs);
		send_steal_request(false);
		timer_end(&timer_send_recv_sreqs);
		timer_start(&timer_idle);
		while (!channel_receive(chan_tasks[ID], (void *)&task, sizeof(Task *))) {
			assert(requested);
			timer_end(&timer_idle);
			// Check if someone requested to steal from us
			while (RECV_REQ(&req))
				handle_steal_request(&req);
			timer_start(&timer_idle);
			if (atomic_read(num_children) == 0) {
				timer_end(&timer_idle);
				goto RT_taskwait_return;
			}
		}
		timer_end(&timer_idle);
		timer_start(&timer_send_recv_tasks);
		loot = task->batch;
#ifdef STEAL_LASTVICTIM
		last_victim = task->victim;
		assert(last_victim != 1 && last_victim != ID);
#endif
		if (loot > 1) {
			while (!channel_receive(chan_tasks[ID], (void *)&tail, sizeof(Task *))) ;
			task = deque_list_tl_pop(deque_list_tl_prepend(deque, task, tail, loot));
			REQ_OPEN();
		}
#ifdef VICTIM_CHECK
		if (loot == 1 && SPLITTABLE(task)) {
			REQ_OPEN();
		}
#endif
		timer_end(&timer_send_recv_tasks);
		requested = false;
		//TODO Figure out at which points updates are reasonable
		//UPDATE();
		timer_start(&timer_enq_deq_tasks);
#ifdef STEAL_ADAPTIVE
		flat_tasks_in_series++;
#endif
		run_task(task);
		deque_list_tl_task_cache(deque, task);
		timer_end(&timer_enq_deq_tasks);
		timer_start(&timer_idle);
	}
	timer_end(&timer_idle);

RT_taskwait_return:
	return;
}

void push(Task *task)
{
	struct steal_request req;

	deque_list_tl_push(deque, task);

#ifdef STEAL_ADAPTIVE
	flat_tasks_in_series = 0;
	if (stealhalf) {
		// Switch back to steal-one
		//LOG("Worker %d switches back to steal-one\n", ID);
		stealhalf = false;
	}
#endif

	REQ_OPEN();

	timer_end(&timer_enq_deq_tasks);

	// Check if someone requested to steal from us
	while (RECV_REQ(&req)) {
		handle_steal_request(&req);
	}

	timer_start(&timer_enq_deq_tasks);
}

Task *pop(void)
{
	struct steal_request req;

	timer_start(&timer_enq_deq_tasks);

	Task *task = deque_list_tl_pop(deque);

#ifdef VICTIM_CHECK
	if (!task) REQ_CLOSE();
#endif

	timer_end(&timer_enq_deq_tasks);

	UPDATE();

	// Check if someone requested to steal from us
	while (RECV_REQ(&req)) {
		// If we just popped a loop task, we may split right here
		// Makes handle_steal_request simpler
		if (deque_list_tl_empty(deque) && SPLITTABLE(task) && req.ID != ID) {
			split_loop(task, &req);
		} else {
			handle_steal_request(&req);
		}
	}

	return task;
}

Task *pop_child(void)
{
	struct steal_request req;

	timer_start(&timer_enq_deq_tasks);

	Task *task = deque_list_tl_pop_child(deque, get_current_task());

	timer_end(&timer_enq_deq_tasks);

	UPDATE();

	// Check if someone requested to steal from us
	while (RECV_REQ(&req)) {
		// If we just popped a loop task, we may split right here
		// Makes handle_steal_request simpler
		if (deque_list_tl_empty(deque) && SPLITTABLE(task) && req.ID != ID) {
			split_loop(task, &req);
		} else {
			handle_steal_request(&req);
		}
	}

	return task;
}

// Returns true when the current task is split, false otherwise
bool RT_loop_split(void)
{
	Task *this = get_current_task();
	struct steal_request req;

    // Split lazily, that is, only when needed
	if (!RECV_REQ(&req)) {
		return false;
	}

	// Send independent tasks if possible
	if (!deque_list_tl_empty(deque)) {
		handle_steal_request(&req);
		return false;
	}

	// Split if possible
	if (SPLITTABLE(this) && req.ID != ID) {
		split_loop(this, &req);
		return true;
	}

	// Decline (or ignore) steal request
	handle_steal_request(&req);

	return false;
}

// Split iteration range in half
static inline long split_half(long start, long end)
{
    return start + (end - start) / 2;
}

static void split_loop(Task *task, struct steal_request *req)
{
	assert(req->ID != ID);

	Task *dup = task_alloc();
	long split;

	// dup is a copy of the current task
	*dup = *task;

	// Split iteration range according to given strategy
    // [start, end) => [start, split) + [split, end)
	split = split_half(task->cur, task->end);

	// New task gets upper half of iterations
	dup->start = split;
	dup->cur = split;
	dup->end = task->end;

	//LOG("Worker %2d: Sending (%ld, %ld) to worker %d\n", ID, dup->start, dup->end, req->ID);

	if (req->quiescent) {
		assert(req->idle);
		assert(req->pass == num_partitions);
		assert(req->partition == my_partition->number);
		req->quiescent = false;
		SEND_REQ_MANAGER(req);
	}

	dup->batch = 1;
#ifdef STEAL_LASTVICTIM
	dup->victim = ID;
#endif

	channel_send(chan_tasks[req->ID], (void *)&dup, sizeof(dup));
	requests_handled++;
	tasks_sent++;
#ifdef STEAL_LEAPFROG
	last_thief = req->ID;
#endif

	// Current task continues with lower half of iterations
	task->end = split;

	//LOG("Worker %2d: Continuing with (%ld, %ld)\n", ID, task->start, task->end);
}
