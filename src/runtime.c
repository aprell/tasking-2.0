#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "runtime.h"
// Disable unit tests
#include "utest.h"
UTEST_MAIN() {}
#include "timer.h"

#define MANAGER	if (is_manager)
#define MAXNP 48
#define PARTITIONS 1
#include "partition.c"

#define LOG(...) { printf(__VA_ARGS__); fflush(stdout); }
#define UNUSED(x) x __attribute__((unused))

PRIVATE PRM_task_queue *PRM_TQ, *PRM_FL;

// Manager/worker -> manager: inter-partition steal requests (MPSC)
// chan_manager and chan_requests could be merged, but only if we don't care
// about mutlithreading managers 
static Channel *chan_manager[MAXNP];

// Manager/worker -> worker: intra-partition steal requests (MPSC)
static Channel *chan_requests[MAXNP];

// Worker -> worker: tasks (SPSC)
static Channel *chan_tasks[MAXNP];

// Worker -> worker: number of tasks that will be sent (SPSC)
static Channel *chan_notify[MAXNP];

// Manager -> manager: global quiescence detection (SPSC)
static Channel *chan_quiescence[MAXNP];

// Manager -> master: notify about global quiescence
static Channel *chan_barrier;

struct token {
	int sender;		// ID of sender (one of the managers)
	int val;		// 1 <= val <= num_partitions
	char __[24];	// pad to cache line
};

// Helper thread -> worker thread: steal requests (SPSC)
static Channel *chan_helper;

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
	char __[10];   	// pad to cache line
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

// ID of last victim from which we got a task
//static int last_victim = -1;

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

	// XXX my_victims contains all possible victims in my_partition
	for (i = 0, j = 0; i < my_partition->num_workers_rt; i++) {
		if (workers[ID][i].rank != ID && workers[ID][i].rank != 1) {
		//if (workers[ID][i].rank != ID && workers[ID][i].rank != 1 && 
		//	workers[ID][i].rank != 7 && workers[ID][i].rank != 25 && workers[ID][i].rank != 31) {
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
	shuffle(my_victims, my_partition->num_workers_rt-2);
	my_victims[my_partition->num_workers_rt-2] = ID;
}

static inline void copy_victims()
{
	// Update shared copy of my_victims
	memcpy(victims[ID], my_victims, (my_partition->num_workers_rt-1) * sizeof(int));
}

// Initializes context needed for random work-stealing
static int ws_init_random(void)
{
	seed = ID;
	init_victims(ID);
	shuffle_victims();
	copy_victims();

	return 0;
}

// To measure the cost of different parts of the runtime
PRIVATE mytimer_t timer_idle, timer_handle, timer_decline, timer_steal, timer_async, timer_manage;
// To measure the impact of lock contention
PRIVATE mytimer_t timer_chan_mpsc, timer_chan_spsc, timer_lock;

#define timer_new(...)	((void)0)
#define timer_start(x)	((void)0)
#define timer_end(x)	((void)0)

int RT_init(void)
{
	// A small sanity check
	// At this point, we have not yet decided who will be manager(s)
	assert(is_manager == false);

	int i;

#if PARTITIONS == 1
	PARTITION_ASSIGN_all(1);
#elif PARTITIONS == 2
	PARTITION_ASSIGN_west(1);
	PARTITION_ASSIGN_east(7);
	//PARTITION_ASSIGN_north(25);
	//PARTITION_ASSIGN_south(1);
#elif PARTITIONS == 3
	PARTITION_ASSIGN_A(1);
	PARTITION_ASSIGN_B(7);
	PARTITION_ASSIGN_C(25);
#elif PARTITIONS == 4
	PARTITION_ASSIGN_Q1(1);
	PARTITION_ASSIGN_Q2(7);
	PARTITION_ASSIGN_Q3(25);
	PARTITION_ASSIGN_Q4(31);
#endif
	PARTITION_SET();

	//MANAGER LOG("Manager %d --> %d\n", ID, next_manager);
	//LOG("Worker %d: in partition %d\n", ID, my_partition->number);
	//LOG("Worker %d --> %d\n", ID, (next_worker == 1) ? next_worker + 1 : next_worker);
	//LOG("Worker %d --> %d\n", ID, (next_worker == 1 || next_worker == 7 || 
	//			                   next_worker == 25 || next_worker == 31) ? next_worker + 1 : next_worker);

	PRM_TQ = PRM_task_queue_init(task_LIFO);
	PRM_FL = PRM_task_queue_init(task_LIFO);

	// Capacity arbitrarily chosen
	chan_requests[ID] = channel_alloc(sizeof(struct steal_request), 32, MPSC);
	chan_tasks[ID] = channel_alloc(sizeof(Task), 0, SPSC);
	chan_notify[ID] = channel_alloc(32, 0, SPSC);
	
	MANAGER {
		chan_manager[ID] = channel_alloc(sizeof(struct steal_request), num_workers, MPSC);
		chan_quiescence[ID] = channel_alloc(sizeof(struct token), 0, SPSC);
	}

	MASTER {
		chan_barrier = channel_alloc(sizeof(bool), 0, SPSC);
	}

	victims[ID] = (int *)malloc(MAXNP * sizeof(int));
	my_victims = (int *)malloc(MAXNP * sizeof(int));
		

	//chan_helper = channel_alloc_shm(sizeof(struct steal_request), 32);

	ws_init_random();

	for (i = 0; i < my_partition->num_workers_rt; i++) {
		if (ID == my_partition->workers[i]) {
			//LOG("Worker %2d: I am (%d, %d)\n", ID, my_partition->number, i);
			pID = i;
			break;
		}
	}

	steal_req.ID = ID;
	steal_req.partition = my_partition->number;
	steal_req.pID = pID;

	timer_new(&timer_idle, RC_REFCLOCKGHZ);
	timer_new(&timer_handle, RC_REFCLOCKGHZ);
	timer_new(&timer_decline, RC_REFCLOCKGHZ);
	timer_new(&timer_steal, RC_REFCLOCKGHZ);
	timer_new(&timer_async, RC_REFCLOCKGHZ);
	timer_new(&timer_manage, RC_REFCLOCKGHZ);

	timer_new(&timer_chan_mpsc, RC_REFCLOCKGHZ);
	timer_new(&timer_chan_spsc, RC_REFCLOCKGHZ);
	timer_new(&timer_lock, RC_REFCLOCKGHZ);

	return 0;
}

int RT_exit(void)
{
	PRM_task_queue_destroy(PRM_TQ);
	PRM_task_queue_destroy(PRM_FL);

	//channel_free_shm(chan_helper);

	free(victims[ID]);
	free(my_victims);

	channel_free(chan_requests[ID]);
	channel_free(chan_tasks[ID]);
	channel_free(chan_notify[ID]);

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

#define VICTIM_RANDOM
//#define VICTIM_ROUNDR

#ifdef VICTIM_ROUNDR
// Chooses the next victim at random
// Can be combined with VICTIM_ROUNDR for example
//XXX Problem if worker is alone in partition!
static inline int random_victim(int thief)
{
	int victim = -1, i;

	for (i = 0; i < my_partition->num_workers_rt-1; i++) {
		do {
			int rand = rand_r(&seed) % (my_partition->num_workers_rt-1);
			victim = my_victims[rand];
		} while (victim == thief || victim == ID || victim == 1);
		//} while (victim == thief || victim == ID || victim == 1 || 
		//		 victim == 7 || victim == 25 || victim == 31);
	}

	assert(is_in_my_partition(victim));

	return victim;
}
#endif // VICTIM_ROUNDR

// Selects the next victim for thief depending on a given strategy
// VICTIM_RANDOM: random victim selection (+ use of last_victim)
// VICTIM_ROUNDR: round robin victim selection
static inline int select_victim(struct steal_request *req)
{
	int victim;

#ifdef VICTIM_ROUNDR
	if (next_worker == 1)
	//if (next_worker == 1 || next_worker == 7 || next_worker == 25 || next_worker == 31)
		victim = next_worker + 1;
	else
		victim = next_worker;
#else // VICTIM_RANDOM
	victim = victims[req->ID][req->try];
#endif

	assert(is_in_my_partition(victim));

	return victim;
}

static inline void UPDATE(void);
static inline void send_steal_request(bool idle);
static inline void decline_steal_request(struct steal_request *);
static inline void decline_all_steal_requests(void);
static inline void split_loop(Task *, long, long *, struct steal_request *);

#define YIELD() \
do { \
	if (pthread_yield()) \
		LOG("Warning: pthread_yield failed\n"); \
} while (0)

#define SEND_REQ(chan, req) \
do { \
	/* Problematic if the target worker has already left scheduling */\
	/* ==> send to full channel will block the sender */\
	while (!channel_send(chan, req, sizeof(*(req)))) { \
		if (*tasking_finished) break; \
	} \
} while (0)

#define SEND_REQ_WORKER(ID, req)	SEND_REQ(chan_requests[ID], req)
#define SEND_REQ_MANAGER(req)		SEND_REQ(chan_manager[my_partition->manager], req)
#define SEND_REQ_PARTITION(req)		SEND_REQ(chan_manager[next_manager], req)

#define RECV_REQ(req) \
	channel_receive(chan_requests[ID], req, sizeof(*(req)))

#define RECV_REQ_FROM_HELPER(req) \
	channel_receive_shm(chan_helper, req, sizeof(*(req)))

#define SEND_QSC_MSG(tok) \
{ \
	/* Problematic if the target worker has already left scheduling */\
	/* ==> send to full channel will block the sender */\
	while (!channel_send(chan_quiescence[next_manager], tok, sizeof(*(tok)))) { \
		if (*tasking_finished) break; \
	} \
}

#define RECV_QSC_MSG(tok) \
{ \
	/* Problematic if the sender has already terminated */\
	while (!channel_receive(chan_quiescence[ID], tok, sizeof(*(tok)))) { \
		if (*tasking_finished) break; \
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
		if (*tasking_finished) break;
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

	timer_start(&timer_manage);

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
				}
			}
		}
		// Quiescence information out of date?
		if (req.partition == my_partition->number && workers_q[req.pID] && !req.quiescent) {
			assert(num_workers_q > 0);
			workers_q[req.pID] = 0;
			num_workers_q--;
			if (quiescent) {
				quiescent = false;
				//LOG("*** Partition %d: left quiescence\n", my_partition->number);
			}
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
#ifdef VICTIM_ROUNDR
					SEND_REQ_WORKER(random_victim(req.ID), &req);
#else // VICTIM_RANDOM
					SEND_REQ_WORKER(select_victim(&req), &req);
#endif
				} else {
					// Steal request made full circle within partition
					assert(req.try == my_partition->num_workers_rt-1);
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
#ifdef VICTIM_ROUNDR
							SEND_REQ_WORKER(random_victim(req.ID), &req);
#else // VICTIM_RANDOM
							SEND_REQ_WORKER(select_victim(&req), &req);
#endif
						} else { // No, this is a new steal request from our partition
							assert(req.pass == 0);
							assert(!req.quiescent);
							assert(!workers_q[req.pID]);
							// Pass request on to neighbor partition
							req.pass++;
							SEND_REQ_PARTITION(&req);
						} 
					} else { // Steal request from remote partition
						// Pass request on to neighbor partition
						if (req.pass < num_partitions)
							req.pass++;
						SEND_REQ_PARTITION(&req);
					} 
				}
				break;
			/////////////////////////////////////////////////////////////////
			// Quiescence                                                  //
			/////////////////////////////////////////////////////////////////
			case true:
				if (req.partition == my_partition->number) {
					assert(req.try == my_partition->num_workers_rt-1);
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
						//LOG("*** Partition %d: left quiescence\n", my_partition->number);
						SEND_REQ_PARTITION(&req);
					} else {
						if (my_partition->number == 0 && !after_barrier) {
							// Assert global quiescence
							// Shake hands with master worker
							assert(channel_send(chan_barrier, &quiescent, sizeof(quiescent)));
							//LOG("*** Partition %d: waiting for master...\n", my_partition->number);
							while (channel_peek(chan_barrier)) ;
							LOG(" *** Partition %d: task barrier complete\n", my_partition->number);
							after_barrier = true;
						}
#ifdef VICTIM_ROUNDR
						SEND_REQ_WORKER(random_victim(req.ID), &req);
#else // VICTIM_RANDOM
						SEND_REQ_WORKER(select_victim(&req), &req);
#endif
					}
				} else {
					assert(req.try == 0 || req.try == my_partition->num_workers_rt-1);
					assert(!is_in_my_partition(req.ID));
					assert(!req.quiescent);
					req.try = 0;
					// We know that stealing would fail, so we just pretend
					if (req.pass < num_partitions)
						req.pass++;
					SEND_REQ_PARTITION(&req);
				}
				break;
		}}
		if (*tasking_finished)
			break;
		if (num_workers_q == my_partition->num_workers_rt-1 && !quiescent) {
			// Transition to quiescent
			//LOG("*** Partition %d: reached quiescence\n", my_partition->number);
			quiescent = true;
		}
		// Token passing for global quiescence detection
		if (channel_receive(chan_quiescence[ID], &tok, sizeof(tok))) {
			assert(tok.val >= 1 && tok.val <= num_partitions);
			if (quiescent)
				tok.val++;
			SEND_QSC_MSG(&tok);
		}
		if (*tasking_finished)
			break;
	}

	timer_end(&timer_manage);

	LOG("Manager received %d notifications\n", notes);

	return 0;
}

static PRIVATE unsigned int sent, declined;

// Send steal request when number of local tasks <= REQ_THRESHOLD
// Steal requests are always sent before actually running out of tasks.
// REQ_THRESHOLD == 0 means that we send a steal request just _before_ we 
// start executing the last task in the queue.
#define REQ_THRESHOLD 0

static inline void UPDATE(void)
{
	if (num_workers == 1)
		return;

	if (PRM_task_queue_num_tasks(PRM_TQ) <= REQ_THRESHOLD)
		send_steal_request(false);
}

// We make sure that there is at most one outstanding steal request per worker
// New steal requests are created with idle == false, to show that the 
// requesting worker is still working on a number of remaining tasks.
static inline void send_steal_request(bool idle)
{
	if (!requested) {
		steal_req.idle = idle;
#ifdef VICTIM_RANDOM
		shuffle_victims();
		//if (last_victim != -1) {
		//	// Find last_victim in field my_victims and swap it to the front
		//	int i;
		//	for (i = 0; i < my_partition->num_workers_rt-2; i++) {
		//		if (my_victims[i] == last_victim) {
		//			swap(&my_victims[i], &my_victims[0]);
		//			break;
		//		}
		//	}
		//}
		copy_victims();
		SEND_REQ_WORKER(select_victim(&steal_req), &steal_req);
#else // VICTIM_ROUNDR
		SEND_REQ_WORKER(random_victim(ID), &steal_req);
#endif
		requested = true;
		sent++;
	}
}

// Got a steal request that can't be served?
// Pass it on to a different victim, or send it back to manager to prevent circling
static inline void decline_steal_request(struct steal_request *req)
{
	declined++;
	req->try++;
	if (req->try < my_partition->num_workers_rt-1) {
		SEND_REQ_WORKER(select_victim(req), req);
	} else {
		SEND_REQ_MANAGER(req);
	}
}

static inline void decline_all_steal_requests(void)
{
	struct steal_request req;

	timer_start(&timer_decline);

	while (RECV_REQ(&req)) {
		if (req.ID == ID && !req.idle) {
			//LOG("Worker %2d: changing steal request to idle\n", ID);
			req.idle = true;
		}
		decline_steal_request(&req);
	}

	timer_end(&timer_decline);
}

static void handle_steal_request(struct steal_request *req)
{
	if (req->ID == ID) {
		// Got own steal request
		// Forget about it if we have more tasks than previously
		if (PRM_task_queue_num_tasks(PRM_TQ) > REQ_THRESHOLD) {
			assert(!req->quiescent);
			assert(requested);
			requested = false;
			return;
		} else {
			timer_end(&timer_handle);
			timer_start(&timer_decline);
			decline_steal_request(req);
			timer_end(&timer_decline);
			timer_start(&timer_handle);
			return;
		}
	}
	assert(req->ID != ID);
	Task *task = PRM_task_steal(PRM_TQ);
	if (task) {
		int n[8]; n[0] = 1; n[1] = ID;
		if (req->quiescent) {
			assert(req->idle);
			assert(req->pass == num_partitions);
			assert(req->partition == my_partition->number);
			req->quiescent = false;
			SEND_REQ_MANAGER(req);
		}
		//LOG("Worker %2d: sending task to worker %d\n", ID, req->ID);
		assert(channel_send(chan_tasks[req->ID], task, sizeof(*task)));
		assert(channel_send(chan_notify[req->ID], n, sizeof(n)));
		PRM_task_dispatch(PRM_task_clear(task), PRM_FL);
	} else {
		// Got steal request, but can't serve it
		// Pass it on to someone else
		assert(PRM_task_queue_is_empty(PRM_TQ));
		timer_end(&timer_handle);
		timer_start(&timer_decline);
		decline_steal_request(req);
		timer_end(&timer_decline);
		timer_start(&timer_handle);
	}
}

int RT_check_for_steal_requests(void)
{
	struct steal_request req;
	int n = 0;

	timer_start(&timer_handle);

	// Check if someone requested to steal from us
	while (RECV_REQ(&req)) {
		handle_steal_request(&req);
		n++;
	}

	timer_end(&timer_handle);

	return n;
}

// Executed by worker threads
void *schedule(UNUSED(void *args))
{
	Task task, *taskp;
	int n[8];

	// Scheduling loop
	for (;;) {
		// (1) PRM task queue
		while ((taskp = pop()) != NULL) {
			run_task(taskp);
			PRM_task_dispatch(PRM_task_clear(taskp), PRM_FL);
		}
		// (2) Work-stealing request
		assert(requested);
		timer_start(&timer_idle);
		while (!channel_receive(chan_notify[ID], n, sizeof(n))) {
			assert(PRM_task_queue_is_empty(PRM_TQ));
			assert(requested);
			timer_end(&timer_idle);
			decline_all_steal_requests();
			timer_start(&timer_idle);
			if (*tasking_finished) {
				timer_end(&timer_idle);
				goto schedule_exit;
			}
		}
		assert(channel_receive(chan_tasks[ID], &task, sizeof(task)));
		//last_victim = n[1];
		//assert(last_victim != 1 && last_victim != ID);
		requested = false;
		timer_end(&timer_idle);
		//TODO Figure out at which points updates are reasonable
		//timer_start(&timer_steal);
		//UPDATE();
		//timer_end(&timer_steal);
		run_task(&task);
	}

schedule_exit:
	return 0;
}

#define FORWARD_REQ(req) \
{ \
	while (!channel_send(chan_helper, req, sizeof(*(req)))) { \
		if (*tasking_finished) break; \
	} \
}

// Executed by helper threads
static void *help(UNUSED(void *args))
{
	struct steal_request req;

	for (;;) {
		while (RECV_REQ(&req)) {
			// Minimum amount of shared state
			if (*(volatile unsigned int *)&PRM_TQ->ntasks) {
				FORWARD_REQ(&req);
			} else {
				if (req.ID == ID && !req.idle) {
					//LOG("Worker %2d: changing steal request to idle\n", ID);
					req.idle = true;
				}
				decline_steal_request(&req);
			}
		}
		YIELD();
		if (*tasking_finished) break;
	}
	
	return 0;
}

// Executed by worker threads
int RT_schedule(void)
{
	// Managers do the load balancing (in a separate thread)
	MANAGER {
		//pthread_t worker;
		//pthread_attr_t attr;
		//pthread_attr_init(&attr);
		//pthread_create(&worker, &attr, schedule, NULL);
		loadbalance();
		//pthread_join(worker, NULL);
		//pthread_attr_destroy(&attr);
	} else {
		//XXX Every worker thread creates an additional helper thread for
		// passing on steal requests
		//pthread_t helper;
		//pthread_attr_t attr;
		//pthread_attr_init(&attr);
		//pthread_create(&helper, &attr, help, NULL);
		schedule(NULL);
		//pthread_join(helper, NULL);
		//pthread_attr_destroy(&attr);
	}

	LOG("Worker %2d: %10u sent and %10u declined steal requests\n", ID, sent, declined);

	return 0;
}

static PRIVATE pthread_t master_helper;
static PRIVATE pthread_attr_t master_helper_attr;

int RT_helper_create(void)
{
	pthread_attr_init(&master_helper_attr);
	pthread_create(&master_helper, &master_helper_attr, help, NULL);

	return 0;
}

int RT_helper_join(void)
{
	pthread_join(master_helper, NULL);
	pthread_attr_destroy(&master_helper_attr);

	return 0;
}

int RT_barrier(void)
{
	WORKER return 0;

	Task task, *taskp;
	Task *this = get_current_task();
	assert(is_root_task(this));

	int n[8];
	bool quiescent;

empty_local_queue:
	while ((taskp = pop()) != NULL) {
		run_task(taskp);
		PRM_task_dispatch(PRM_task_clear(taskp), PRM_FL);
	}

	if (num_workers == 1)
		return 0;
	
	assert(requested);
	timer_start(&timer_idle);
	while (!channel_receive(chan_notify[ID], n, sizeof(n))) {
		assert(PRM_task_queue_is_empty(PRM_TQ));
		assert(requested);
		timer_end(&timer_idle);
		decline_all_steal_requests();
		timer_start(&timer_idle);
		if (channel_receive(chan_barrier, &quiescent, sizeof(quiescent))) {
			assert(quiescent);
			timer_end(&timer_idle);
			goto RT_barrier_exit;
		}
	}
	assert(channel_receive(chan_tasks[ID], &task, sizeof(task)));
	//last_victim = n[1];
	//assert(last_victim != 1 && last_victim != ID);
	requested = false;
	timer_end(&timer_idle);
	//TODO Figure out at which points updates are reasonable
	//timer_start(&timer_steal);
	//UPDATE();
	//timer_end(&timer_steal);
	run_task(&task);
	goto empty_local_queue;

RT_barrier_exit:
	assert(!channel_peek(chan_tasks[ID]));
	// Remove own steal request to reflect the fact that the computation continues
	for (;;) {
		struct steal_request req;
		if (RECV_REQ(&req)) {
			LOG("Got request\n");
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
	LOG("After barrier\n");
	return 0;
}

void RT_force_future_channel(Channel *chan, void *data, unsigned int size)
{
	Task task, *taskp;
	Task *this = get_current_task();
	struct steal_request req;
	int n[8];

	assert(channel_impl(chan) == SPSC);

	if (channel_receive(chan, data, size))
		goto RT_force_future_channel_return;

	while ((taskp = pop()) != NULL) {
		if (taskp->parent == this) {
			run_task(taskp);
			PRM_task_dispatch(PRM_task_clear(taskp), PRM_FL);
			if (channel_receive(chan, data, size))
				goto RT_force_future_channel_return;
		} else {
			// Push it back onto the queue
			push(taskp);
			break;
		}
	}

	assert(get_current_task() == this);

	timer_start(&timer_idle);
	while (!channel_receive(chan, data, size)) {
		timer_end(&timer_idle);
		timer_start(&timer_steal);
		send_steal_request(false);
		timer_end(&timer_steal);
		timer_start(&timer_idle);
		while (!channel_receive(chan_notify[ID], n, sizeof(n))) {
			assert(requested);
			timer_end(&timer_idle);
			timer_start(&timer_handle);
			// Check if someone requested to steal from us
			while (RECV_REQ(&req))
				handle_steal_request(&req);
			timer_end(&timer_handle);
			timer_start(&timer_idle);
			if (channel_receive(chan, data, size)) {
				timer_end(&timer_idle);
				goto RT_force_future_channel_return;
			}
		}
		assert(channel_receive(chan_tasks[ID], &task, sizeof(task)));
		//last_victim = n[1];
		//assert(last_victim != 1 && last_victim != ID);
		requested = false;
		timer_end(&timer_idle);
		//TODO Figure out at which points updates are reasonable
		//timer_start(&timer_steal);
		//UPDATE();
		//timer_end(&timer_steal);
		run_task(&task);
		timer_start(&timer_idle);
	}
	timer_end(&timer_idle);

RT_force_future_channel_return:
	return;
}

void push(Task *task)
{
	struct steal_request req;

	timer_start(&timer_async);

	PRM_task_dispatch(task, PRM_TQ);

	timer_end(&timer_async);

	timer_start(&timer_handle);

	// Check if someone requested to steal from us
	while (RECV_REQ(&req))
		handle_steal_request(&req);

	timer_end(&timer_handle);
}

// t MAY BE NULL, so we can't implement this with an inline function.
// Consider this use:
// if (SPLITTABLE(task, task->start, task->end))
//     ...
// If task is NULL -> segfault
// Macro expansion plus short circuit evaluation prevent this from happening.
#define SPLITTABLE(t, s, e) \
	((bool)((t) != NULL && (t)->is_loop && abs((e)-(s)) > 1))

Task *pop(void)
{
	struct steal_request req;

	Task *task = PRM_task_remove(PRM_TQ);
	timer_start(&timer_steal);
	UPDATE();
	timer_end(&timer_steal);

	timer_start(&timer_handle);
	
	// Check if someone requested to steal from us
	while (RECV_REQ(&req)) {
		// If we just popped a loop task, we may split right here
		// Makes handle_steal_request simpler
		if (SPLITTABLE(task, task->start, task->end) && req.ID != ID) {
			split_loop(task, task->start, &task->end, &req);
		} else {
			handle_steal_request(&req);
		}
	}

	timer_end(&timer_handle);
	
	return task;
}

bool RT_loop_init(long *start, long *end)
{
	Task *this = get_current_task();
	
	if (!this->is_loop)
		return false;

	*start = this->start;
	*end = this->end;

	return true;
}

bool RT_loop_split(long next, long *end)
{
	Task *this = get_current_task();
	struct steal_request req;

	if (!SPLITTABLE(this, next, *end))
		return false;

    // Split lazily, that is, only when needed
	if (!RECV_REQ(&req))
		return false;

	timer_start(&timer_handle);

	if (req.ID == ID) {
		// Don't split in this case; that would be silly
		// Discard steal request
		assert(!req.quiescent);
		assert(requested);
		requested = false;
		timer_end(&timer_handle);
		return false;
	}

	split_loop(this, next, end, &req);

	timer_end(&timer_handle);

	return true;
}

// Split iteration range in half
static inline long split_half(long start, long end)
{
    return start + (end - start) / 2;
}

// Work-splitting modifies task in-place (avoids a new copy)!
static void split_loop(Task *task, long start, long *end, struct steal_request *req)
{
	long _start = task->start, split, _end = *end;
	int n[8]; n[0] = 1; n[1] = ID;

	// Split iteration range according to given strategy
    // [start, end) => [start, split) + [split, end)
    split = split_half(start, _end);
	task->start = split;
	task->end = _end;

	//LOG("Worker %2d: Sending (%ld, %ld) to worker %d\n", ID, task->start, task->end, req->ID);

	if (req->quiescent) {
		assert(req->idle);
		assert(req->pass == num_partitions);
		assert(req->partition == my_partition->number);
		req->quiescent = false;
		SEND_REQ_MANAGER(req);
	}

	assert(channel_send(chan_tasks[req->ID], task, sizeof(*task)));
	assert(channel_send(chan_notify[req->ID], n, sizeof(n)));

	task->start = _start;
	task->end = *end = split;

	//LOG("Worker %2d: Continuing with (%ld, %ld)\n", ID, task->start, task->end);
}
