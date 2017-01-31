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
#include "profile.h"
#ifdef STEAL_BACKOFF
#include "wtime.h"
#endif

#define MANAGER	if (is_manager)
#define MAXNP 256
#define PARTITIONS 1
#include "partition.c"

// Supported loop-splitting strategies (-DSPLIT=[half|guided|adaptive])
// Default is split-half (-DSPLIT=half)
#define half 0
#define guided 1
#define adaptive 2

#define LOG(...) { printf(__VA_ARGS__); fflush(stdout); }
#define UNUSED(x) x __attribute__((unused))

static PRIVATE DequeListTL *deque;

// Worker -> worker: intra-partition steal requests (MPSC)
static Channel *chan_requests[MAXNP];

// Worker -> worker: tasks (SPSC)
static Channel *chan_tasks[MAXNP];

#ifdef VICTIM_CHECK

struct task_indicator {
	atomic_t tasks;
	char __[64 - sizeof(atomic_t)];
};

static struct task_indicator task_indicators[MAXNP];

#define LIKELY_HAS_TASKS(ID)    (atomic_read(&task_indicators[ID].tasks) > 0)
#define HAVE_TASKS()             atomic_set(&task_indicators[ID].tasks, 1)
#define HAVE_NO_TASKS()          atomic_set(&task_indicators[ID].tasks, 0)

#else

#define LIKELY_HAS_TASKS(ID)     true      // Assume yes, victim has tasks
#define HAVE_TASKS()             ((void)0) // NOOP
#define HAVE_NO_TASKS()          ((void)0) // NOOP

#endif // VICTIM_CHECK

struct steal_request {
	Channel *chan;  // channel for sending tasks
	int ID;			// ID of requesting worker
	int try;	   	// 0 <= try <= num_workers_rt
	int partition; 	// partition in which the steal request was initiated
	int pID;		// ID of requesting worker within partition
#ifdef STEAL_BACKOFF
	int rounds;		// worker backs off after n rounds of failed stealing
#endif
	bool idle;		// ID has nothing left to work on?
	bool quiescent;	// manager assumes ID is in a quiescent state?
	bool is_update; // forwarded steal request that informs the manager that
	                // the requesting worker is no longer idle?
#ifdef STEAL_ADAPTIVE
	bool stealhalf; // true ? attempt steal-half : attempt steal-one
#endif
#if defined STEAL_BACKOFF && defined STEAL_ADAPTIVE
	char __[0];	    // pad to cache line
#elif defined STEAL_BACKOFF
	char __[1];     // pad to cache line
#elif defined STEAL_ADAPTIVE
	char __[4];     // pad to cache line
#else
	char __[5];     // pad to cache line
#endif
};

static inline void print_steal_req(struct steal_request *req)
{
	LOG("{ .ID = %d, .try = %d, .partition = %d, .pID = %d, "
		".idle = %s, .quiescent = %s, .is_update = %s }\n",
		req->ID, req->try, req->partition, req->pID,
		req->idle == true ? "Y" : "N", req->quiescent == true ? "Y" : "N",
		req->is_update == true ? "Y" : "N");
}

// .try == 0, .idle == false, .quiescence == false
static PRIVATE struct steal_request steal_req;

#ifdef STEAL_BACKOFF
// The last steal request before backing off
static PRIVATE struct steal_request last_steal_req;
#endif

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
			my_partition->num_workers_rt, my_partition->num_workers);
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

// To profile different parts of the runtime
PROFILE_DECL(RUN_TASK);
PROFILE_DECL(ENQ_DEQ_TASK);
PROFILE_DECL(SEND_RECV_TASK);
PROFILE_DECL(SEND_RECV_REQ);
PROFILE_DECL(IDLE);

PRIVATE unsigned int requests_sent, requests_handled;
PRIVATE unsigned int requests_declined, tasks_sent;
PRIVATE unsigned int tasks_split;

int RT_init(void)
{
	// Small sanity checks
	// At this point, we have not yet decided who will be manager(s)
	assert(is_manager == false);
	assert(sizeof(struct steal_request) == 32);
	assert(sizeof(Task) == 192);

	int i;

#ifndef MANAGER_ID
#define MANAGER_ID 0
#endif

	PARTITION_ASSIGN_xlarge(MANAGER_ID);
	PARTITION_SET();

	deque = deque_list_tl_new();

	MANAGER {
		// Unprocessed update message followed by new steal request
		// => up to two messages per worker
		chan_requests[ID] = channel_alloc(sizeof(struct steal_request), num_workers * 2, MPSC);
	} else {
		chan_requests[ID] = channel_alloc(sizeof(struct steal_request), num_workers, MPSC);
	}

	chan_tasks[ID] = channel_alloc(sizeof(Task *), 1, SPSC);

	victims[ID] = (int *)malloc(MAXNP * sizeof(int));
	my_victims = (int *)malloc(MAXNP * sizeof(int));

	ws_init();

	for (i = 0; i < my_partition->num_workers_rt; i++) {
		if (ID == my_partition->workers[i]) {
			pID = i;
			break;
		}
	}

	steal_req = (struct steal_request) {
		.chan = chan_tasks[ID],
		.ID = ID,
		.try = 0,
		.partition = my_partition->number,
		.pID = pID,
#ifdef STEAL_BACKOFF
		.rounds = 0,
#endif
		.idle = false,
		.quiescent = false
#ifdef STEAL_ADAPTIVE
		, .stealhalf = false
#endif
	};

	requested = false;

#ifdef VICTIM_CHECK
	assert(sizeof(struct task_indicator) == 64);
	atomic_set(&task_indicators[ID].tasks, 0);
#endif

	PROFILE_INIT(RUN_TASK);
	PROFILE_INIT(ENQ_DEQ_TASK);
	PROFILE_INIT(SEND_RECV_TASK);
	PROFILE_INIT(SEND_RECV_REQ);
	PROFILE_INIT(IDLE);

	return 0;
}

int RT_exit(void)
{
	deque_list_tl_delete(deque);

	free(victims[ID]);
	free(my_victims);

	channel_free(chan_requests[ID]);
	channel_free(chan_tasks[ID]);

	PARTITION_RESET();

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

// Number of steal attempts before a steal request is sent back to the thief
// Default value is the number of workers minus one
#ifndef MAX_STEAL_ATTEMPTS
#define MAX_STEAL_ATTEMPTS (my_partition->num_workers_rt-1)
#endif

#ifdef STEAL_RANDOM
static inline int next_victim(struct steal_request *req)
{
	int victim, i;

	for (i = req->try; i < MAX_STEAL_ATTEMPTS; i++) {
		victim = victims[req->ID][i];
		if (LIKELY_HAS_TASKS(victim)) {
			//assert(is_in_my_partition(victim));
			//LOG("Worker %d: Choosing victim %d after %i tries (requester %d)\n", ID, victim, i, req->ID);
			return victim;
		}
		req->try++;
	}

	assert(i == req->try);
	assert(req->try == MAX_STEAL_ATTEMPTS);

	return req->ID;
}
#endif // STEAL_RANDOM

#ifdef STEAL_RANDOM_RR
// Chooses the first victim at random
static inline int random_victim(struct steal_request *req)
{
	// Assumption: Beginning of a new round of steal attempts
	assert(req->try == 0);

	int victim = -1;

	do {
		int rand = rand_r(&seed) % (my_partition->num_workers_rt-1);
		victim = my_victims[rand];
		assert(victim != ID);
	} while (victim == req->ID);

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
	for (i = 0; i < my_partition->num_workers_rt-1,
			    req->try < my_partition->num_workers_rt-1; i++) {
		victim = my_victims[i];
		if (victim != req->ID) {
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

	if (req->try < MAX_STEAL_ATTEMPTS) {
		if (last_victim != -1 && last_victim != req->ID && LIKELY_HAS_TASKS(last_victim)) {
			victim = last_victim;
			return victim;
		}
		// Fall back to random victim selection
		return next_victim(req);
	}

	return req->ID;
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
		if (last_thief != -1 && last_thief != req->ID && LIKELY_HAS_TASKS(last_thief)) {
			victim = last_thief;
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
static inline void send_steal_request(bool);
static inline void decline_steal_request(struct steal_request *);
static inline void decline_all_steal_requests(void);
static inline void split_loop(Task *, struct steal_request *);

static inline bool register_idle(struct steal_request *);
static inline bool unregister_idle(struct steal_request *);
static inline bool detect_termination(void);

#define SEND_REQ(chan, req) \
do { \
	int __nfail = 0; \
	/* Problematic if the target worker has already left scheduling */ \
	/* ==> send to full channel will block the sender */ \
	while (!channel_send(chan, req, sizeof(*(req)))) { \
		if (++__nfail % 3 == 0) { \
			LOG("*** Worker %d: blocked on channel send\n", ID); \
			assert(false && "Check channel capacities!"); \
		} \
		if (tasking_done()) break; \
	} \
} while (0)

#define SEND_REQ_WORKER(ID, req)	SEND_REQ(chan_requests[ID], req)
#define SEND_REQ_MANAGER(req)		SEND_REQ(chan_requests[my_partition->manager], req)

static inline bool RECV_REQ(struct steal_request *req)
{
	bool ret;

	PROFILE(SEND_RECV_REQ) {
		ret = channel_receive(chan_requests[ID], req, sizeof(*(req)));
#ifndef DISABLE_MANAGER
		MANAGER {
			// Handle update messages
			while (ret && unregister_idle(req)) {
				ret = channel_receive(chan_requests[ID], req, sizeof(*(req)));
			}
			// Are all workers idle?
			ret && register_idle(req) && detect_termination();
			assert((ret && !req->is_update) || !ret);
		}
#endif
	} // PROFILE

	return ret;
}

static inline bool RECV_TASK(Task **task)
{
	bool ret;

	PROFILE(SEND_RECV_TASK) {
		ret = channel_receive(chan_tasks[ID], (void *)task, sizeof(Task *));
	}

	return ret;
}

// Termination detection

#ifdef DISABLE_MANAGER

#define NOTIFY_MANAGER(req) \
do { \
	assert((req)->idle); \
	atomic_dec(td_count); \
} while (0)

#define CHECK_NOTIFY_MANAGER(req) \
do { \
	if ((req)->idle) { \
		NOTIFY_MANAGER(req); \
	} \
} while (0)

#else

#define NOTIFY_MANAGER(req) \
do { \
	assert((req)->quiescent && (req)->idle); \
	(req)->quiescent = false; \
	(req)->is_update = true; \
	MANAGER { \
		/* Elide message */ \
		unregister_idle(req); \
	} else { \
		PROFILE(SEND_RECV_REQ) SEND_REQ_MANAGER(req); \
	} \
} while (0)

#define CHECK_NOTIFY_MANAGER(req) \
do { \
	if ((req)->quiescent) { \
		NOTIFY_MANAGER(req); \
	} \
} while (0)

#endif // DISABLE_MANAGER

#define FORGET_REQ(req) \
do { \
	assert((req)->ID == ID); \
	CHECK_NOTIFY_MANAGER(req); \
	assert(requested); \
	requested = false; \
} while (0)

static PRIVATE int workers_q[MAXNP];
static PRIVATE int num_workers_q;
static PRIVATE int notes;
static PRIVATE bool quiescent;
static PRIVATE bool after_barrier;

#ifdef STEAL_ADAPTIVE
// Number of steals after which the current strategy is reevaluated
#ifndef STEAL_ADAPTIVE_INTERVAL
#define STEAL_ADAPTIVE_INTERVAL 25
#endif
PRIVATE int num_tasks_exec_recently;
static PRIVATE int num_steals_exec_recently;
static PRIVATE bool stealhalf;
PRIVATE unsigned int requests_steal_one, requests_steal_half;
#endif

static inline bool register_idle(struct steal_request *req)
{
	if (req->idle && !req->quiescent && !workers_q[req->pID]) {
		workers_q[req->pID] = 1;
		req->quiescent = true;
		num_workers_q++;
		notes++;
		return true;
	}

	return false;
}

static inline bool unregister_idle(struct steal_request *req)
{
	if (req->idle && !req->quiescent && workers_q[req->pID]) {
		assert(req->is_update);
		workers_q[req->pID] = 0;
		num_workers_q--;
		quiescent = false;
		if (after_barrier) {
			after_barrier = false;
		}
		notes++;
		return true;
	}

	return false;
}

// Asynchronous call of function fn on worker ID
// Executed for side effects only
static void async_action(void (*fn)(void), int ID)
{
	// Package up and send a dummy task
	PROFILE(SEND_RECV_TASK) {

	Task *dummy = task_alloc();
	dummy->fn = (void (*)(void *))fn;
	dummy->batch = 1;
#ifdef STEAL_LASTVICTIM
	dummy->victim = ID;
#endif
	while (!channel_send(chan_tasks[ID], (void *)&dummy, sizeof(Task *))) ;

	} // PROFILE
}

#define ASYNC_ACTION(fn) void fn()

static ASYNC_ACTION(confirm_termination)
{
	assert(!requested);
	requested = true;
	quiescent = true;
#ifdef STEAL_ADAPTIVE
	num_steals_exec_recently--;
#endif
	num_tasks_exec_worker--;
}

static inline bool detect_termination(void)
{
	if (num_workers_q == my_partition->num_workers_rt && !quiescent) {
		//LOG("Termination detected\n");
		quiescent = true;
	}

	if (!after_barrier && quiescent && !channel_peek(chan_tasks[MASTER_ID])) {
#if MANAGER_ID != MASTER_ID
		async_action(confirm_termination, MASTER_ID);
#endif
		//LOG("Termination confirmed\n");
		after_barrier = true;
		return true;
	}

	return false;
}

// Notify each other when it's time to shut down
ASYNC_ACTION(notify_workers)
{
	assert(!tasking_finished);

	int child = 2*ID + 1;

	if (child < num_workers) {
		async_action(notify_workers, child);
	}

	if (child + 1 < num_workers) {
		async_action(notify_workers, child + 1);
	}

	WORKER num_tasks_exec_worker--;

	tasking_finished = true;
}

// Send steal request when number of local tasks <= REQ_THRESHOLD
// Steal requests are always sent before actually running out of tasks.
// REQ_THRESHOLD == 0 means that we send a steal request just _before_ we
// start executing the last task in the queue.
#define REQ_THRESHOLD 0

static inline void UPDATE(void)
{
	if (num_workers == 1)
		return;

	if (deque_list_tl_num_tasks(deque) <= REQ_THRESHOLD) {
#ifdef STEAL_ADAPTIVE
		// Estimate work-stealing efficiency during the last interval
		// If the value is below a threshold, switch strategies
		if (num_steals_exec_recently == STEAL_ADAPTIVE_INTERVAL) {
			double ratio = ((double)num_tasks_exec_recently) / STEAL_ADAPTIVE_INTERVAL;
			if (stealhalf && ratio < 2) stealhalf = false;
			else if (!stealhalf && ratio == 1) stealhalf = true;
			num_tasks_exec_recently = 0;
			num_steals_exec_recently = 0;
		}
#endif
		send_steal_request(false);
	}
}

// We make sure that there is at most one outstanding steal request per worker
// New steal requests are created with idle == false, to show that the
// requesting worker is still working on a number of remaining tasks.
static inline void send_steal_request(bool idle)
{
	PROFILE(SEND_RECV_REQ) {

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
#ifdef STEAL_ADAPTIVE
		stealhalf == true ?  requests_steal_half++ : requests_steal_one++;
#endif
	}

	} // PROFILE
}

#ifdef STEAL_BACKOFF

#define STEAL_BACKOFF_BASE 100
#define STEAL_BACKOFF_MULTIPLIER 2
#define STEAL_BACKOFF_ROUNDS 1

static PRIVATE int steal_backoff_usec = STEAL_BACKOFF_BASE;
static PRIVATE double steal_backoff_intvl_start;
static PRIVATE bool steal_backoff_waiting;
static PRIVATE unsigned int steal_backoffs;
PRIVATE unsigned int requests_resent;

// Resend steal request after waiting steal_backoff_usec microseconds
static inline void resend_steal_request(void)
{
	PROFILE(SEND_RECV_REQ) {

	assert(requested);
	assert(steal_backoff_waiting);
	assert(last_steal_req.idle);
#ifndef DISABLE_MANAGER
	assert(last_steal_req.quiescent);
#endif
	last_steal_req.try = 0;
	last_steal_req.rounds = 0;
#ifdef STEAL_RANDOM
	shuffle_victims();
	copy_victims();
#ifdef STEAL_LASTVICTIM
	SEND_REQ_WORKER(lastvictim(&last_steal_req), &last_steal_req);
#elif defined STEAL_LEAPFROG
	SEND_REQ_WORKER(leapfrog(&last_steal_req), &last_steal_req);
#else
	SEND_REQ_WORKER(next_victim(&last_steal_req), &last_steal_req);
#endif
#else // STEAL_RANDOM_RR
	SEND_REQ_WORKER(random_victim(&last_steal_req), &last_steal_req);
#endif
	steal_backoff_waiting = false;
	steal_backoff_usec *= STEAL_BACKOFF_MULTIPLIER;
	requests_resent++;
	requests_sent++;
#ifdef STEAL_ADAPTIVE
	stealhalf == true ?  requests_steal_half++ : requests_steal_one++;
#endif

	} // PROFILE
}
#endif // STEAL_BACKOFF

#ifdef DISABLE_MANAGER

static inline void decline_steal_request(struct steal_request *req)
{
	PROFILE(SEND_RECV_REQ) {

	requests_declined++;
	req->try++;

	assert(req->try <= MAX_STEAL_ATTEMPTS+1);

	if (req->try < MAX_STEAL_ATTEMPTS+1) {
		//if (my_partition->num_workers_rt > 2) {
		//	if (ID == req->ID) print_steal_req(req);
		//	assert(ID != req->ID);
		//}
		SEND_REQ_WORKER(next_victim(req), req);
	} else {
#ifdef STEAL_BACKOFF
		if (req->idle && ++req->rounds == STEAL_BACKOFF_ROUNDS) {
			last_steal_req = *req;
#ifdef STEAL_ADAPTIVE
			stealhalf = false;
			last_steal_req.stealhalf = false;
			num_steals_exec_recently = 0;
			num_tasks_exec_recently = 0;
#endif
			steal_backoff_intvl_start = Wtime_usec();
			steal_backoff_waiting = true;
			steal_backoffs++;
		} else {
			req->try = 0;
#ifdef STEAL_RANDOM
			SEND_REQ_WORKER(next_victim(req), req);
#else // STEAL_RANDOM_RR
			SEND_REQ_WORKER(random_victim(req), req);
#endif
		}
#else
		req->try = 0;
#ifdef STEAL_RANDOM
		SEND_REQ_WORKER(next_victim(req), req);
#else // STEAL_RANDOM_RR
		SEND_REQ_WORKER(random_victim(req), req);
#endif
#endif
	}

	} // PROFILE
}

#else

// Got a steal request that can't be served?
// Pass it on to a different victim or send it back to manager
static inline void decline_steal_request(struct steal_request *req)
{
	MANAGER {
		if (req->try == MAX_STEAL_ATTEMPTS+1) {
			req->try = 0;
		}
	}

	PROFILE(SEND_RECV_REQ) {

	requests_declined++;
	req->try++;

	assert(req->try <= MAX_STEAL_ATTEMPTS+1);

	if (req->try < MAX_STEAL_ATTEMPTS+1) {
		//if (my_partition->num_workers_rt > 2) {
		//	if (ID == req->ID) print_steal_req(req);
		//	assert(ID != req->ID);
		//}
		SEND_REQ_WORKER(next_victim(req), req);
	} else {
#ifdef STEAL_BACKOFF
		if (ID != MASTER_ID && req->quiescent && ++req->rounds == STEAL_BACKOFF_ROUNDS) {
			last_steal_req = *req;
#ifdef STEAL_ADAPTIVE
			stealhalf = false;
			last_steal_req.stealhalf = false;
			num_steals_exec_recently = 0;
			num_tasks_exec_recently = 0;
#endif
			steal_backoff_intvl_start = Wtime_usec();
			steal_backoff_waiting = true;
			steal_backoffs++;
		} else if (ID != MASTER_ID && req->quiescent) {
#else
		if (ID != MASTER_ID && req->quiescent) {
#endif
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

	} // PROFILE
}

#endif // DISABLE_MANAGER

static inline void decline_all_steal_requests(void)
{
	struct steal_request req;

	PROFILE_STOP(IDLE);

	if (RECV_REQ(&req)) {
		if (req.ID == ID && !req.idle) {
			req.idle = true;
#ifdef DISABLE_MANAGER
			atomic_inc(td_count);
#endif
		}
		decline_steal_request(&req);
	}

	PROFILE_START(IDLE);
}

static void handle_steal_request(struct steal_request *req)
{
	Task *task;
	int loot = 1;

	if (req->ID == ID) {
		task = get_current_task();
		long tasks_left = task && task->is_loop ? abs(task->end - task->cur) : 0;
		// Got own steal request
		// Forget about it if we have more tasks than previously
		if (deque_list_tl_num_tasks(deque) > REQ_THRESHOLD ||
			tasks_left > REQ_THRESHOLD) {
#ifdef OPTIMIZE_BARRIER
			CHECK_NOTIFY_MANAGER(req);
#else
			assert(!req->quiescent);
#endif
			assert(requested);
			requested = false;
			return;
		} else {
#ifdef VICTIM_CHECK
			// Avoid sending the steal request back to ourselves
			assert(!req->quiescent);
			assert(requested);
			requested = false;
#else
			decline_steal_request(req); // => send to manager
#endif
			return;
		}
	}
	assert(req->ID != ID);

	PROFILE(ENQ_DEQ_TASK) {

#ifdef STEAL_ADAPTIVE
	if (req->stealhalf) {
		task = deque_list_tl_steal_half(deque, &loot);
	} else {
		task = deque_list_tl_steal(deque);
	}
#elif defined STEAL_HALF
	task = deque_list_tl_steal_half(deque, &loot);
#else // Default is steal-one
	task = deque_list_tl_steal(deque);
#endif

	} // PROFILE

	if (task) {
		CHECK_NOTIFY_MANAGER(req);
		PROFILE(SEND_RECV_TASK) {

		task->batch = loot;
#ifdef STEAL_LASTVICTIM
		task->victim = ID;
#endif
		channel_send(req->chan, (void *)&task, sizeof(Task *));
		//LOG("Worker %2d: sending %d tasks to worker %d\n", ID, loot, req->ID);
		requests_handled++;
		tasks_sent += loot;
#ifdef STEAL_LEAPFROG
		last_thief = req->ID;
#endif

		} // PROFILE
	} else {
		// Got steal request, but can't serve it
		// Pass it on to someone else
		assert(deque_list_tl_empty(deque));
		decline_steal_request(req);
		HAVE_NO_TASKS();
	}
}

// Loop task with iterations left for splitting?
#define SPLITTABLE(t) \
	((bool)((t) != NULL && (t)->is_loop && abs((t)->end - (t)->cur) > (t)->sst))

int RT_check_for_steal_requests(void)
{
	Task *this = get_current_task();
	struct steal_request req;
	int n = 0;

	if (!channel_peek(chan_requests[ID])) {
		return 0;
	}

	// Check if someone requested to steal from us
	while (RECV_REQ(&req)) {
		// (1) Send task(s) if possible
		// (2) Split current task if possible
		// (3) Decline (or ignore) steal request
		if (!deque_list_tl_empty(deque)) {
			handle_steal_request(&req);
		} else if (SPLITTABLE(this)) {
			if (req.ID != ID) {
				split_loop(this, &req);
			} else {
				FORGET_REQ(&req);
			}
		} else {
			HAVE_NO_TASKS();
			decline_steal_request(&req);
		}
		n++;
	}

	return n;
}

// Executed by worker threads
void *schedule(UNUSED(void *args))
{
	Task *task;
	int loot;

	// Scheduling loop
	for (;;) {
		// (1) Private task queue
		while ((task = pop()) != NULL) {
			PROFILE(RUN_TASK) run_task(task);
			PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
		}

		// (2) Work-stealing request
		if (!requested) {
			send_steal_request(true);
		}
		assert(requested);

		PROFILE(IDLE) {

		while (!RECV_TASK(&task)) {
			assert(deque_list_tl_empty(deque));
			assert(requested);
			decline_all_steal_requests();
#ifdef STEAL_BACKOFF
			if (steal_backoff_waiting && (Wtime_usec() - steal_backoff_intvl_start >= steal_backoff_usec)) {
				PROFILE_STOP(IDLE);
				resend_steal_request();
				PROFILE_START(IDLE);
				assert(!steal_backoff_waiting);
			}
#endif
		}

		} // PROFILE
		loot = task->batch;
#ifdef STEAL_LASTVICTIM
		last_victim = task->victim;
		assert(last_victim != ID);
#endif
		if (loot > 1) {
			PROFILE(ENQ_DEQ_TASK) task = deque_list_tl_pop(deque_list_tl_prepend(deque, task, loot));
			HAVE_TASKS();
		}
#ifdef VICTIM_CHECK
		if (loot == 1 && SPLITTABLE(task)) {
			HAVE_TASKS();
		}
#endif
		requested = false;
#ifdef STEAL_BACKOFF
		steal_backoff_usec = STEAL_BACKOFF_BASE;
#endif
		//TODO Figure out at which points updates are reasonable
		//UPDATE();
#ifdef STEAL_ADAPTIVE
		num_steals_exec_recently++;
#endif
		PROFILE(RUN_TASK) run_task(task);
		PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);

		if (tasking_done()) break;
	}

	return 0;
}

// Executed by worker threads
int RT_schedule(void)
{
	schedule(NULL);

	MANAGER LOG("Manager %d received %d notifications\n", ID, notes);

	return 0;
}

#ifdef OPTIMIZE_BARRIER
static ASYNC_ACTION(run_dummy_task)
{
	assert(!requested);
#if MANAGER_ID == MASTER_ID
	steal_req.idle = false;
#else
	steal_req.idle = true;
#endif
	steal_req.try = MAX_STEAL_ATTEMPTS;
#ifdef STEAL_ADAPTIVE
	steal_req.stealhalf = stealhalf;
#endif
	requested = true;
	requests_sent++;
#ifdef STEAL_ADAPTIVE
	stealhalf == true ?  requests_steal_half++ : requests_steal_one++;
#endif
	SEND_REQ_WORKER(MASTER_ID, &steal_req);
	num_tasks_exec_worker--;
}
#endif

//double td_start, td_end, td_elapsed;

int RT_barrier(void)
{
	WORKER return 0;

	assert(is_root_task(get_current_task()));

	Task *task;
	int loot;
#ifndef DISABLE_MANAGER
	quiescent = false;
#endif

empty_local_queue:
	while ((task = pop()) != NULL) {
		PROFILE(RUN_TASK) run_task(task);
		PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
	}

	if (num_workers == 1)
		return 0;

	if (!requested) {
		send_steal_request(true);
	}
	assert(requested);

	PROFILE(IDLE) {

	while (!RECV_TASK(&task)) {
		assert(deque_list_tl_empty(deque));
		assert(requested);
		decline_all_steal_requests();
#ifndef DISABLE_MANAGER
		if (quiescent) {
			PROFILE_STOP(IDLE);
			goto RT_barrier_exit;
		}
#else
		if (tasking_all_idle()) {
			goto RT_barrier_exit;
		}
#endif
	}

	} // PROFILE
	loot = task->batch;
#ifdef STEAL_LASTVICTIM
	last_victim = task->victim;
	assert(last_victim != ID);
#endif
	if (loot > 1) {
		PROFILE(ENQ_DEQ_TASK) task = deque_list_tl_pop(deque_list_tl_prepend(deque, task, loot));
		HAVE_TASKS();
	}
#ifdef VICTIM_CHECK
	if (loot == 1 && SPLITTABLE(task)) {
		HAVE_TASKS();
	}
#endif
	requested = false;
	//TODO Figure out at which points updates are reasonable
	//UPDATE();
#ifdef STEAL_ADAPTIVE
	num_steals_exec_recently++;
#endif
	PROFILE(RUN_TASK) run_task(task);
	PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
	goto empty_local_queue;

RT_barrier_exit:
	//td_start = Wtime_usec();
	assert(!channel_peek(chan_tasks[ID]));
#ifndef DISABLE_MANAGER
#ifdef OPTIMIZE_BARRIER
	struct steal_request req;
	while (!RECV_REQ(&req)) ;
	NOTIFY_MANAGER(&req);
	if (req.ID == ID) {
		assert(requested);
		requested = false;
	} else {
		async_action(run_dummy_task, req.ID);
	}
#else
	// Remove own steal request to reflect the fact that the computation continues
	for (;;) {
		struct steal_request req;
		if (RECV_REQ(&req)) {
			if (req.ID == ID) {
				NOTIFY_MANAGER(&req);
				assert(requested);
				requested = false;
				break;
			}
			decline_steal_request(&req);
		}
	}
#endif // OPTIMIZE_BARRIER
#endif // DISABLE_MANAGER

	//td_end = Wtime_usec();
	//td_elapsed += td_end - td_start;

	return 0;
}

void RT_force_future_channel(Channel *chan, void *data, unsigned int size)
{
	Task *task;
	Task *this = get_current_task();
	struct steal_request req;
	int loot;

	assert(channel_impl(chan) == SPSC);

	if (channel_receive(chan, data, size))
		goto RT_force_future_channel_return;

	while ((task = pop_child()) != NULL) {
		PROFILE(RUN_TASK) run_task(task);
		PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
		if (channel_receive(chan, data, size))
			goto RT_force_future_channel_return;
	}

	assert(get_current_task() == this);

	while (!channel_receive(chan, data, size)) {
		send_steal_request(false);
		PROFILE(IDLE) {

		while (!RECV_TASK(&task)) {
			// We might inadvertently remove our own steal request in
			// handle_steal_request, so:
			PROFILE_STOP(IDLE);
			if (!requested) {
				send_steal_request(false);
			}
			// Check if someone requested to steal from us
			while (RECV_REQ(&req))
				handle_steal_request(&req);
			PROFILE_START(IDLE);
			if (channel_receive(chan, data, size)) {
				PROFILE_STOP(IDLE);
				goto RT_force_future_channel_return;
			}
		}

		} // PROFILE
		loot = task->batch;
#ifdef STEAL_LASTVICTIM
		last_victim = task->victim;
		assert(last_victim != ID);
#endif
		if (loot > 1) {
			PROFILE(ENQ_DEQ_TASK) task = deque_list_tl_pop(deque_list_tl_prepend(deque, task, loot));
			HAVE_TASKS();
		}
#ifdef VICTIM_CHECK
		if (loot == 1 && SPLITTABLE(task)) {
			HAVE_TASKS();
		}
#endif
		requested = false;
		//TODO Figure out at which points updates are reasonable
		//UPDATE();
#ifdef STEAL_ADAPTIVE
		num_steals_exec_recently++;
#endif
		PROFILE(RUN_TASK) run_task(task);
		PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
	}

RT_force_future_channel_return:
	return;
}

void RT_force_future_channel(Channel *chan)
{
	Task *task;
	Task *this = get_current_task();
	struct steal_request req;
	int loot;

	assert(channel_impl(chan) == SPSC);

	if (channel_closed(chan))
		goto RT_force_future_channel_return;

	while ((task = pop_child()) != NULL) {
		PROFILE(RUN_TASK) run_task(task);
		PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
		if (channel_closed(chan))
			goto RT_force_future_channel_return;
	}

	assert(get_current_task() == this);

	while (!channel_closed(chan)) {
		send_steal_request(false);
		PROFILE(IDLE) {

		while (!RECV_TASK(&task)) {
			// We might inadvertently remove our own steal request in
			// handle_steal_request, so:
			PROFILE_STOP(IDLE);
			if (!requested) {
				send_steal_request(false);
			}
			// Check if someone requested to steal from us
			while (RECV_REQ(&req))
				handle_steal_request(&req);
			PROFILE_START(IDLE);
			if (channel_closed(chan)) {
				PROFILE_STOP(IDLE);
				goto RT_force_future_channel_return;
			}
		}

		} // PROFILE
		loot = task->batch;
#ifdef STEAL_LASTVICTIM
		last_victim = task->victim;
		assert(last_victim != ID);
#endif
		if (loot > 1) {
			PROFILE(ENQ_DEQ_TASK) task = deque_list_tl_pop(deque_list_tl_prepend(deque, task, loot));
			HAVE_TASKS();
		}
#ifdef VICTIM_CHECK
		if (loot == 1 && SPLITTABLE(task)) {
			HAVE_TASKS();
		}
#endif
		requested = false;
		//TODO Figure out at which points updates are reasonable
		//UPDATE();
#ifdef STEAL_ADAPTIVE
		num_steals_exec_recently++;
#endif
		PROFILE(RUN_TASK) run_task(task);
		PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
	}

RT_force_future_channel_return:
	return;
}

// Define RT_force_future as an alias for RT_force_future_channel
// __RT_force_future_channel_impl__3 is the mangled symbol name
void __attribute__((alias("__RT_force_future_channel_impl__3")))
RT_force_future(Channel *chan, void *data, unsigned int size);

// Return when *num_children == 0
void RT_taskwait(atomic_t *num_children)
{
	Task *task;
	Task *this = get_current_task();
	struct steal_request req;
	int loot;

	if (atomic_read(num_children) == 0)
		goto RT_taskwait_return;

	while ((task = pop_child()) != NULL) {
		PROFILE(RUN_TASK) run_task(task);
		PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
		if (atomic_read(num_children) == 0)
			goto RT_taskwait_return;
	}

	assert(get_current_task() == this);

	while (atomic_read(num_children) > 0) {
		send_steal_request(false);
		PROFILE(IDLE) {

		while (!RECV_TASK(&task)) {
			// We might inadvertently remove our own steal request in
			// handle_steal_request, so:
			PROFILE_STOP(IDLE);
			if (!requested) {
				send_steal_request(false);
			}
			// Check if someone requested to steal from us
			while (RECV_REQ(&req))
				handle_steal_request(&req);
			PROFILE_START(IDLE);
			if (atomic_read(num_children) == 0) {
				PROFILE_STOP(IDLE);
				goto RT_taskwait_return;
			}
		}

		} // PROFILE
		loot = task->batch;
#ifdef STEAL_LASTVICTIM
		last_victim = task->victim;
		assert(last_victim != ID);
#endif
		if (loot > 1) {
			PROFILE(ENQ_DEQ_TASK) task = deque_list_tl_pop(deque_list_tl_prepend(deque, task, loot));
			HAVE_TASKS();
		}
#ifdef VICTIM_CHECK
		if (loot == 1 && SPLITTABLE(task)) {
			HAVE_TASKS();
		}
#endif
		requested = false;
		//TODO Figure out at which points updates are reasonable
		//UPDATE();
#ifdef STEAL_ADAPTIVE
		num_steals_exec_recently++;
#endif
		PROFILE(RUN_TASK) run_task(task);
		PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
	}

RT_taskwait_return:
	return;
}

void push(Task *task)
{
	struct steal_request req;

	deque_list_tl_push(deque, task);

	HAVE_TASKS();

	PROFILE_STOP(ENQ_DEQ_TASK);

	// Check if someone requested to steal from us
	while (RECV_REQ(&req)) {
		handle_steal_request(&req);
	}

	PROFILE_START(ENQ_DEQ_TASK);
}

Task *pop(void)
{
	struct steal_request req;
	Task *task;

	PROFILE(ENQ_DEQ_TASK) {
		task = deque_list_tl_pop(deque);
	}

#ifdef VICTIM_CHECK
	if (!task) HAVE_NO_TASKS();
#endif

	if ((task && !task->is_loop) || !task) {
		UPDATE();
	}

	// Check if someone requested to steal from us
	while (RECV_REQ(&req)) {
		// If we just popped a loop task, we may split right here
		// Makes handle_steal_request simpler
		if (deque_list_tl_empty(deque) && SPLITTABLE(task)) {
			if (req.ID != ID) {
				split_loop(task, &req);
			} else {
				FORGET_REQ(&req);
			}
		} else {
			handle_steal_request(&req);
		}
	}

	return task;
}

Task *pop_child(void)
{
	struct steal_request req;
	Task *task;

	PROFILE(ENQ_DEQ_TASK) {
		task = deque_list_tl_pop_child(deque, get_current_task());
	}

	if ((task && !task->is_loop) || !task) {
		UPDATE();
	}

	// Check if someone requested to steal from us
	while (RECV_REQ(&req)) {
		// If we just popped a loop task, we may split right here
		// Makes handle_steal_request simpler
		if (deque_list_tl_empty(deque) && SPLITTABLE(task)) {
			if (req.ID != ID) {
				split_loop(task, &req);
			} else {
				FORGET_REQ(&req);
			}
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
	if (SPLITTABLE(this)) {
		if (req.ID != ID) {
			split_loop(this, &req);
			return true;
		} else {
			HAVE_NO_TASKS();
			FORGET_REQ(&req);
			return false;
		}
	}

	decline_steal_request(&req);

	return false;
}

#if SPLIT == half
	#define SPLIT_FUNC split_half
#elif SPLIT == guided
	#define SPLIT_FUNC split_guided
#elif SPLIT == adaptive
	#define SPLIT_FUNC split_adaptive
#else // Default is split-half
	#define SPLIT_FUNC split_half
#endif

// Split iteration range in half
static inline long split_half(Task *task)
{
    return task->cur + (task->end - task->cur) / 2;
}

// Split iteration range based on the number of workers
static inline long split_guided(Task *task)
{
	assert(task->chunks > 0);

	long iters_left = abs(task->end - task->cur);

	assert(iters_left > task->sst);

	if (iters_left <= task->chunks) {
		return split_half(task);
	}

	//LOG("Worker %2d: sending %ld iterations\n", ID, task->chunks);

	return task->end - task->chunks;
}

#define IDLE_WORKERS (channel_peek(chan_requests[ID]))

// Split iteration range based on the number of steal requests
static inline long split_adaptive(Task *task)
{
	long iters_left = abs(task->end - task->cur);
	long num_idle, chunk;

	assert(iters_left > task->sst);

	//LOG("Worker %2d: %ld of %ld iterations left\n", ID, iters_left, iters_total);

	// We have already received one steal request
	num_idle = IDLE_WORKERS + 1;

	//LOG("Worker %2d: have %ld steal requests\n", ID, num_idle);

	// Every thief receives a chunk
	chunk = max(iters_left / (num_idle + 1), 1);

	assert(iters_left > chunk);

	//LOG("Worker %2d: sending %ld iterations\n", ID, chunk);

	return task->end - chunk;
}

static void split_loop(Task *task, struct steal_request *req)
{
	Task *dup;
	long split;

	PROFILE(ENQ_DEQ_TASK) {

	dup = task_alloc();

	// dup is a copy of the current task
	*dup = *task;

	// Split iteration range according to given strategy
    // [start, end) => [start, split) + [split, end)
	split = SPLIT_FUNC(task);

	// New task gets upper half of iterations
	dup->start = split;
	dup->cur = split;
	dup->end = task->end;

	} // PROFILE

	//LOG("Worker %2d: Sending [%ld, %ld) to worker %d\n", ID, dup->start, dup->end, req->ID);

	CHECK_NOTIFY_MANAGER(req);

	PROFILE(SEND_RECV_TASK) {

	dup->batch = 1;
#ifdef STEAL_LASTVICTIM
	dup->victim = ID;
#endif

	channel_send(req->chan, (void *)&dup, sizeof(dup));
	requests_handled++;
	tasks_sent++;
#ifdef STEAL_LEAPFROG
	last_thief = req->ID;
#endif

	// Current task continues with lower half of iterations
	task->end = split;

	tasks_split++;

	} // PROFILE

	//LOG("Worker %2d: Continuing with [%ld, %ld)\n", ID, task->cur, task->end);
}
