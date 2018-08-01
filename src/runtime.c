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

#define MAXNP 256
#define PARTITIONS 1
#include "partition.h"
#include "partition.c"

#define LOG(...) { printf(__VA_ARGS__); fflush(stdout); }
#define UNUSED(x) x __attribute__((unused))

// Private task deque
static PRIVATE DequeListTL *deque;

// Worker -> worker: intra-partition steal requests (MPSC)
static Channel *chan_requests[MAXNP];

// Worker -> worker: tasks (SPSC)
static Channel *chan_tasks[MAXNP][MAXSTEAL];

// Every worker needs to keep track of which channels it can use for the next
// steal request
static PRIVATE Channel *channel_stack[MAXSTEAL];
static PRIVATE int channel_stack_top;

#define CHANNEL_PUSH(chan) \
do { \
	assert(0 <= channel_stack_top && channel_stack_top < MAXSTEAL); \
	channel_stack[channel_stack_top++] = chan; \
} while (0)

#define CHANNEL_POP() \
	(assert(0 < channel_stack_top && channel_stack_top <= MAXSTEAL), \
	 channel_stack[--channel_stack_top])

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

// Steal requests carry one of the following states:
// - STATE_WORKING means the requesting worker is still busy
// - STATE_IDLE means the requesting worker has run out of tasks
// - STATE_REG_IDLE means the requesting worker is known to be idle
// - STATE_UPDATE means the requesting worker is no longer idle because it
//   received new tasks

typedef unsigned char state_t;

#define STATE_WORKING  0x00
#define STATE_IDLE     0x02
#define STATE_REG_IDLE 0x04
#define STATE_UPDATE   0x08

struct steal_request {
	Channel *chan;  // channel for sending tasks
	int ID;			// ID of requesting worker
	int try;	   	// 0 <= try <= num_workers_rt
	int partition; 	// partition in which the steal request was initiated
	int pID;		// ID of requesting worker within partition
#ifdef STEAL_BACKOFF
	int rounds;		// worker backs off after n rounds of failed stealing
#endif
	state_t state;  // state of steal request and, by extension, requesting worker
#if STEAL == adaptive
	bool stealhalf; // true ? attempt steal-half : attempt steal-one
#endif
#if defined STEAL_BACKOFF && STEAL == adaptive
	char __[2];	    // pad to cache line
#elif defined STEAL_BACKOFF
	char __[3];     // pad to cache line
#elif STEAL == adaptive
	char __[6];     // pad to cache line
#else
	char __[7];     // pad to cache line
#endif
};

#ifdef STEAL_BACKOFF
#define STEAL_REQUEST_INIT_rounds .rounds = 0,
#else
#define STEAL_REQUEST_INIT_rounds
#endif

#if STEAL == adaptive
#define STEAL_REQUEST_INIT_stealhalf , .stealhalf = stealhalf
#else
#define STEAL_REQUEST_INIT_stealhalf
#endif

#define STEAL_REQUEST_INIT \
(struct steal_request) { \
	.chan = CHANNEL_POP(), \
	.ID = ID, \
	.try = 0, \
	.partition = my_partition->number, \
	.pID = pID, \
	STEAL_REQUEST_INIT_rounds \
	.state = STATE_WORKING \
	STEAL_REQUEST_INIT_stealhalf \
}

static inline void print_steal_req(struct steal_request *req)
{
#if STEAL == adaptive
	LOG("{ .ID = %d, .try = %d, .partition = %d, .pID = %d, .state = %d, .stealhalf = %s }\n",
		req->ID, req->try, req->partition, req->pID, req->state, req->stealhalf ? "true" : "false");
#else
	LOG("{ .ID = %d, .try = %d, .partition = %d, .pID = %d, .state = %d }\n",
		req->ID, req->try, req->partition, req->pID, req->state);
#endif
}

#ifdef STEAL_BACKOFF

// Every worker has a backoff queue of steal requests

#define BACKOFF_QUEUE_SIZE (MAXSTEAL + 1)

static PRIVATE struct steal_request backoff_queue[BACKOFF_QUEUE_SIZE];
static PRIVATE int backoff_queue_hd = 0, backoff_queue_tl = 0;

#define BACKOFF_QUEUE_EMPTY() \
	(backoff_queue_hd == backoff_queue_tl)

#define BACKOFF_QUEUE_FULL() \
	((backoff_queue_tl + 1) % BACKOFF_QUEUE_SIZE == backoff_queue_hd)

#define BACKOFF_QUEUE_PUSH(req) \
do { \
	assert(!BACKOFF_QUEUE_FULL()); \
	backoff_queue[backoff_queue_tl] = *(req); \
	backoff_queue_tl = (backoff_queue_tl + 1) % BACKOFF_QUEUE_SIZE; \
} while (0)

#define BACKOFF_QUEUE_POP() \
({ \
	assert(!BACKOFF_QUEUE_EMPTY()); \
	struct steal_request *req = &backoff_queue[backoff_queue_hd]; \
	backoff_queue_hd = (backoff_queue_hd + 1) % BACKOFF_QUEUE_SIZE; \
	req; \
})

#endif

// A worker can have up to MAXSTEAL outstanding steal requests:
// 0 <= requested <= MAXSTEAL
static PRIVATE int requested;

#ifdef STEAL_LASTVICTIM
// ID of last victim
static PRIVATE int last_victim = -1;
#endif

#ifdef STEAL_LASTTHIEF
// ID of last thief
static PRIVATE int last_thief = -1;
#endif

// Every worker keeps a list of victims that can be read by other workers
// Shared state!
static int *victims[MAXNP];

// Private copy of victims field
static PRIVATE int *my_victims;

// A worker has a unique ID within its partition:
// 0 <= pID <= num_workers_rt
static PRIVATE int pID;

static void init_victims(int ID)
{
	int available_workers[num_workers];
	int i, j;

	// Get all available workers in my_partition
	for (i = 0, j = 0; i < my_partition->num_workers; i++) {
		int worker = my_partition->workers[i];
		if (worker < num_workers) {
			available_workers[j++] = worker;
			my_partition->num_workers_rt++;
		}
	}

	// my_victims contains all possible victims in my_partition
	for (i = 0, j = 0; i < my_partition->num_workers_rt; i++) {
		if (available_workers[i] != ID) {
			my_victims[j++] = available_workers[i];
		}
	}

	// We store our own ID here because eventually, after N unsuccessful tries
	// (N being the number of potential victims in my_partition), the steal
	// request must go back to the thief. Otherwise, we would not be able to
	// send steal requests ahead of time.
	my_victims[j] = ID;

	MASTER LOG("Manager %2d: %d of %d workers available\n", ID,
		   my_partition->num_workers_rt, my_partition->num_workers);
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
	shuffle(my_victims, my_partition->num_workers_rt-1);
	my_victims[my_partition->num_workers_rt-1] = ID;
}

static inline void copy_victims()
{
	// Update shared copy of my_victims
	memcpy(victims[ID], my_victims, my_partition->num_workers_rt * sizeof(int));
}

// Initializes context needed for work-stealing
static int ws_init(void)
{
	seed = ID;
	init_victims(ID);
	shuffle_victims();
	copy_victims();

	return 0;
}

// To profile different parts of the runtime
PROFILE_DECL(RUN_TASK);
PROFILE_DECL(ENQ_DEQ_TASK);
PROFILE_DECL(SEND_RECV_TASK);
PROFILE_DECL(SEND_RECV_REQ);
PROFILE_DECL(IDLE);

PRIVATE unsigned int updates_received;
PRIVATE unsigned int requests_sent, requests_handled;
PRIVATE unsigned int requests_declined, tasks_sent;
PRIVATE unsigned int tasks_split;
#ifdef LAZY_FUTURES
PRIVATE unsigned int futures_converted;
#endif

int RT_init(void)
{
	// Small sanity checks
	// At this point, we have not yet decided who will be manager(s)
	assert(is_manager == false);
	assert(sizeof(struct steal_request) == 32);
	assert(sizeof(Task) == 192);

	int i;

	PARTITION_ASSIGN_xlarge(MASTER_ID);
	PARTITION_SET();

	if (is_manager) {
		assert(ID == MASTER_ID);
	}

	deque = deque_list_tl_new();

	MASTER {
		// Unprocessed update message followed by new steal request
		// => up to two messages per worker (assuming MAXSTEAL == 1)
		chan_requests[ID] = channel_alloc(sizeof(struct steal_request), MAXSTEAL * num_workers * 2, MPSC);
	} else {
		chan_requests[ID] = channel_alloc(sizeof(struct steal_request), MAXSTEAL * num_workers, MPSC);
	}

	// Being able to send N steal requests requires either a single MPSC or N
	// SPSC channels
	for (i = 0; i < MAXSTEAL; i++) {
		chan_tasks[ID][i] = channel_alloc(sizeof(Task *), 1, SPSC);
		CHANNEL_PUSH(chan_tasks[ID][i]);
	}

	assert(channel_stack_top == MAXSTEAL);

	victims[ID] = (int *)malloc(MAXNP * sizeof(int));
	my_victims = (int *)malloc(MAXNP * sizeof(int));

	ws_init();

	for (i = 0; i < my_partition->num_workers_rt; i++) {
		if (ID == my_partition->workers[i]) {
			pID = i;
			break;
		}
	}

	requested = 0;

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
	int i;

	deque_list_tl_delete(deque);

	free(victims[ID]);
	free(my_victims);

	channel_free(chan_requests[ID]);
#ifdef CHANNEL_CACHE
	// Free all cached channels
	channel_cache_free();
#endif

	for (i = 0; i < MAXSTEAL; i++) {
		assert(!channel_peek(chan_tasks[ID][i]));
		channel_free(chan_tasks[ID][i]);
	}

	PARTITION_RESET();

	return 0;
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

static inline int next_victim(struct steal_request *req)
{
	int victim, i;

	for (i = req->try; i < MAX_STEAL_ATTEMPTS; i++) {
		victim = victims[req->ID][i];
		if (LIKELY_HAS_TASKS(victim)) {
			//LOG("Worker %d: Choosing victim %d after %i tries (requester %d)\n", ID, victim, i, req->ID);
			return victim;
		}
		req->try++;
	}

	assert(i == req->try && req->try == MAX_STEAL_ATTEMPTS);

	return req->ID;
}

#if defined STEAL_LASTVICTIM || defined STEAL_LASTTHIEF
static inline int steal_from(struct steal_request *req, int worker)
{
	int victim;

	if (req->try < MAX_STEAL_ATTEMPTS) {
		if (worker != -1 && worker != req->ID && LIKELY_HAS_TASKS(worker)) {
			victim = worker;
			return victim;
		}
		// worker is unavailable; fall back to random victim selection
		return next_victim(req);
	}

	return req->ID;
}
#endif // STEAL_LASTVICTIM || STEAL_LASTTHIEF

static inline void try_send_steal_request(bool);
static inline void decline_steal_request(struct steal_request *);
static inline void decline_all_steal_requests(void);
static inline void split_loop(Task *, struct steal_request *);

// Termination detection
static inline void register_idle(struct steal_request *);
static inline void unregister_idle(struct steal_request *);
static inline void detect_termination(void);

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

static PRIVATE bool quiescent;

static inline bool RECV_REQ(struct steal_request *req)
{
	bool ret;

	PROFILE(SEND_RECV_REQ) {
		ret = channel_receive(chan_requests[ID], req, sizeof(*(req)));
#ifndef DISABLE_MANAGER
		MASTER {
			assert(ID == MASTER_ID);
			// Deal with updates
			while (ret && req->state == STATE_UPDATE) {
				unregister_idle(req);
				ret = channel_receive(chan_requests[ID], req, sizeof(*(req)));
			}
			// Check if worker is idle and if termination occurred
			if (ret && req->state == STATE_IDLE) {
				register_idle(req);
				detect_termination();
			}
			// No special treatment for other states
			assert((ret && req->state != STATE_IDLE)   || !ret);
			assert((ret && req->state != STATE_UPDATE) || !ret);
			// No cancellation of steal requests required
		}
#endif
	} // PROFILE

	return ret;
}

#include "overload_RECV_TASK.h"

static inline bool RECV_TASK(Task **task, bool idle)
{
	bool ret;
	int i;

	PROFILE(SEND_RECV_TASK) {
		for (i = 0; i < MAXSTEAL; i++) {
			ret = channel_receive(chan_tasks[ID][i], (void *)task, sizeof(Task *));
			if (ret) {
				CHANNEL_PUSH(chan_tasks[ID][i]);
				break;
			}
		}
	}

	if (!ret) try_send_steal_request(/* idle = */ idle);

	return ret;
}

static inline bool RECV_TASK(Task **task)
{
	return RECV_TASK(task, true);
}

// Termination detection

#ifdef DISABLE_MANAGER

#define NOTIFY_MANAGER(req) \
do { \
	assert((req)->state == STATE_IDLE); \
	atomic_dec(td_count); \
} while (0)

#define CHECK_NOTIFY_MANAGER(req) \
do { \
	if ((req)->state == STATE_IDLE) { \
		NOTIFY_MANAGER(req); \
	} \
} while (0)

#else

#define NOTIFY_MANAGER(req) \
do { \
	assert((req)->state == STATE_REG_IDLE); \
	(req)->state = STATE_UPDATE; \
	MASTER { \
		/* Elide message */ \
		unregister_idle(req); \
	} else { \
		PROFILE(SEND_RECV_REQ) SEND_REQ_MANAGER(req); \
	} \
} while (0)

#define CHECK_NOTIFY_MANAGER(req) \
do { \
	if ((req)->state == STATE_REG_IDLE) { \
		NOTIFY_MANAGER(req); \
	} \
} while (0)

#endif // DISABLE_MANAGER

#define FORGET_REQ(req) \
do { \
	assert((req)->ID == ID); \
	CHECK_NOTIFY_MANAGER(req); \
	assert(requested); \
	requested--; \
	CHANNEL_PUSH((req)->chan); \
} while (0)

static PRIVATE int workers_q[MAXNP];
static PRIVATE int num_workers_q;

static inline void register_idle(struct steal_request *req)
{
	assert(req->state == STATE_IDLE);
	req->state = STATE_REG_IDLE;

#ifdef DEBUG_TD
	LOG(">>> Worker %d registers worker %d <<<\n", ID, req->ID);
#endif

	assert(workers_q[req->pID] < MAXSTEAL);
	workers_q[req->pID]++;
	num_workers_q++;
	updates_received++;

	assert(0 < num_workers_q && num_workers_q <= MAXSTEAL * my_partition->num_workers_rt);
}

static void async_action(void (*)(void), Channel *);
static void ack_termination(void);

static inline void unregister_idle(struct steal_request *req)
{
#ifdef DEBUG_TD
	LOG(">>> Worker %d unregisters worker %d\n", ID, req->ID);
#endif

	assert(req->state == STATE_UPDATE);
	req->state = STATE_WORKING;

	assert(workers_q[req->pID] > 0);
	workers_q[req->pID]--;
	num_workers_q--;
	quiescent = false;
	updates_received++;

	assert(0 <= num_workers_q && num_workers_q < MAXSTEAL * my_partition->num_workers_rt);
}

static inline void detect_termination(void)
{
	if (num_workers_q == MAXSTEAL * my_partition->num_workers_rt && !quiescent) {
		quiescent = true;
	}

	if (quiescent) {
#ifdef DEBUG_TD
		LOG(">>> Worker %d detected termination <<<\n", ID);
#endif
	}
}

#if STEAL == adaptive
// Number of steals after which the current strategy is reevaluated
#ifndef STEAL_ADAPTIVE_INTERVAL
#define STEAL_ADAPTIVE_INTERVAL 25
#endif
PRIVATE int num_tasks_exec_recently;
static PRIVATE int num_steals_exec_recently;
static PRIVATE bool stealhalf;
PRIVATE unsigned int requests_steal_one, requests_steal_half;
#endif

// Asynchronous call of function fn delivered via channel chan
// Executed for side effects only
static void async_action(void (*fn)(void), Channel *chan)
{
	bool ret;

	// Package up and send a dummy task
	PROFILE(SEND_RECV_TASK) {

	Task *dummy = task_alloc();
	dummy->fn = (void (*)(void *))fn;
	dummy->batch = 1;
#ifdef STEAL_LASTVICTIM
	dummy->victim = -1;
#endif
	ret = channel_send(chan, (void *)&dummy, sizeof(Task *));
	assert(ret);

	} // PROFILE
}

// Asynchronous actions are side-effecting pseudo-tasks

#define ASYNC_ACTION(fn) void fn(void)

// Notify each other when it's time to shut down
ASYNC_ACTION(notify_workers)
{
	assert(!tasking_finished);

	int child = 2*ID + 1;

	if (child < num_workers) {
		async_action(notify_workers, chan_tasks[child][0]);
	}

	if (child + 1 < num_workers) {
		async_action(notify_workers, chan_tasks[child + 1][0]);
	}

	WORKER num_tasks_exec--;

	tasking_finished = true;
}

// Try to send a steal request
// Every worker can have at most MAXSTEAL pending steal requests. A steal
// request with idle == false indicates that the requesting worker is still
// busy working on some tasks. A steal request with idle == true indicates that
// the requesting worker is in fact idle and has nothing to work on.
static inline void try_send_steal_request(bool idle)
{
	PROFILE(SEND_RECV_REQ) {

	if (requested < MAXSTEAL) {
#if STEAL == adaptive
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
		assert(requested + channel_stack_top == MAXSTEAL);
		struct steal_request req = STEAL_REQUEST_INIT;
		req.state = idle ? STATE_IDLE : STATE_WORKING;
		assert(req.try == 0);
		// Avoid concurrent access to victims[ID]
		if (!requested) {
			shuffle_victims();
			copy_victims();
		}
#ifdef STEAL_LASTVICTIM
		SEND_REQ_WORKER(steal_from(&req, last_victim), &req);
#elif defined STEAL_LASTTHIEF
		SEND_REQ_WORKER(steal_from(&req, last_thief), &req);
#else
		SEND_REQ_WORKER(next_victim(&req), &req);
#endif
		requested++;
		requests_sent++;
#if STEAL == adaptive
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

	assert(0 < requested && requested <= MAXSTEAL);
	assert(steal_backoff_waiting);

	struct steal_request *req = BACKOFF_QUEUE_POP();

#ifndef DISABLE_MANAGER
	assert(req->state == STATE_REG_IDLE);
#endif
	req->try = 0;
	req->rounds = 0;
	//shuffle_victims();
	//copy_victims();
#ifdef STEAL_LASTVICTIM
	SEND_REQ_WORKER(steal_from(req, last_victim), req);
#elif defined STEAL_LASTTHIEF
	SEND_REQ_WORKER(steal_from(req, last_thief), req);
#else
	SEND_REQ_WORKER(next_victim(req), req);
#endif
	steal_backoff_waiting = false;
	steal_backoff_usec *= STEAL_BACKOFF_MULTIPLIER;
	requests_resent++;
	requests_sent++;
#if STEAL == adaptive
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
		if (req->state == STATE_IDLE && ++req->rounds == STEAL_BACKOFF_ROUNDS) {
#if STEAL == adaptive
			req->stealhalf = stealhalf = false;
			num_steals_exec_recently = 0;
			num_tasks_exec_recently = 0;
#endif
			BACKOFF_QUEUE_PUSH(req);
			steal_backoff_intvl_start = Wtime_usec();
			steal_backoff_waiting = true;
			steal_backoffs++;
		} else {
			req->try = 0;
			SEND_REQ_WORKER(next_victim(req), req);
		}
#else
		req->try = 0;
		SEND_REQ_WORKER(next_victim(req), req);
#endif
	}

	} // PROFILE
}

#else

// Got a steal request that can't be served?
// Pass it on to a different victim or send it back to manager
static inline void decline_steal_request(struct steal_request *req)
{
	MASTER {
		if (req->try == MAX_STEAL_ATTEMPTS+1) {
			req->try = 0;
		} else {
			req->try++;
		}
	} else {
		assert(req->try < MAX_STEAL_ATTEMPTS+1);
		req->try++;
	}

	PROFILE(SEND_RECV_REQ) {

	requests_declined++;

	assert(req->try <= MAX_STEAL_ATTEMPTS+1);

	if (req->try < MAX_STEAL_ATTEMPTS+1) {
		//if (my_partition->num_workers_rt > 2) {
		//	if (ID == req->ID) print_steal_req(req);
		//	assert(ID != req->ID);
		//}
		SEND_REQ_WORKER(next_victim(req), req);
	} else {
#ifdef STEAL_BACKOFF
		if (ID != MASTER_ID && req->state == STATE_REG_IDLE && ++req->rounds == STEAL_BACKOFF_ROUNDS) {
#if STEAL == adaptive
			req->stealhalf = stealhalf = false;
			num_steals_exec_recently = 0;
			num_tasks_exec_recently = 0;
#endif
			BACKOFF_QUEUE_PUSH(req);
			steal_backoff_intvl_start = Wtime_usec();
			steal_backoff_waiting = true;
			steal_backoffs++;
		} else if (ID != MASTER_ID && req->state == STATE_REG_IDLE) {
#else
		if (ID != MASTER_ID && req->state == STATE_REG_IDLE) {
#endif
			// Don't bother manager; we are quiescent anyway
			req->try = 0;
			SEND_REQ_WORKER(next_victim(req), req);
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
		if (req.ID == ID && req.state == STATE_WORKING) {
			req.state = STATE_IDLE;
#ifdef DISABLE_MANAGER
			atomic_inc(td_count);
#endif
		}
		decline_steal_request(&req);
	}

	PROFILE_START(IDLE);
}

#ifdef STEAL_EARLY
#ifndef STEAL_EARLY_THRESHOLD
#define STEAL_EARLY_THRESHOLD 0
#endif
#endif

#ifdef LAZY_FUTURES
static void convert_lazy_future(Task *task)
{
	// Lazy allocation
	lazy_future *f;
	memcpy(&f, task->data, sizeof(lazy_future *));
	if (!f->has_channel) {
		assert(sizeof(f->buf) == 8);
		f->chan = channel_alloc(sizeof(f->buf), 0, SPSC);
		f->has_channel = true;
		futures_converted++;
	} // else nothing to do; already allocated
}
#endif

static void handle_steal_request(struct steal_request *req)
{
	Task *task;
	int loot = 1;

	if (req->ID == ID) {
		task = get_current_task();
		long tasks_left = task && task->is_loop ? abs(task->end - task->cur) : 0;
		// Got own steal request
		// Forget about it if we have more tasks than previously
#ifdef STEAL_EARLY
		if (deque_list_tl_num_tasks(deque) > STEAL_EARLY_THRESHOLD ||
			tasks_left > STEAL_EARLY_THRESHOLD) {
#else
		if (deque_list_tl_num_tasks(deque) > 0 || tasks_left > 0) {
#endif
			FORGET_REQ(req);
			return;
		} else {
#ifdef VICTIM_CHECK
			// Because it's likely that, in the absence of potential victims,
			// we'd end up sending the steal request right back to us, we just
			// give up for now
			FORGET_REQ(req);
#else
			decline_steal_request(req); // => send to manager
#endif
			return;
		}
	}
	assert(req->ID != ID);

	PROFILE(ENQ_DEQ_TASK) {

#if STEAL == adaptive
	if (req->stealhalf) {
		task = deque_list_tl_steal_half(deque, &loot);
	} else {
		task = deque_list_tl_steal(deque);
	}
#elif STEAL == half
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
#ifdef LAZY_FUTURES
		Task *t;
		for (t = task; t != NULL; t = t->next) {
			if (t->has_future) convert_lazy_future(t);
		}
#endif
		channel_send(req->chan, (void *)&task, sizeof(Task *));
		//LOG("Worker %2d: sending %d task%s to worker %d\n",
		//	ID, loot, loot > 1 ? "s" : "", req->ID);
		requests_handled++;
		tasks_sent += loot;
#ifdef STEAL_LASTTHIEF
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
		try_send_steal_request(/* idle = */ true);
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
		if (task->victim != -1) {
			last_victim = task->victim;
			assert(last_victim != ID);
		}
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
		requested--;
		assert(0 <= requested && requested < MAXSTEAL);
#ifdef STEAL_BACKOFF
		steal_backoff_usec = STEAL_BACKOFF_BASE;
#endif
#if STEAL == adaptive
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


	return 0;
}

int RT_barrier(void)
{
	WORKER return 0;

#ifdef DEBUG_TD
	LOG(">>> Worker %d enters barrier <<<\n", ID);
#endif

	assert(is_root_task(get_current_task()));

	Task *task;
	int loot;

empty_local_queue:
	while ((task = pop()) != NULL) {
		PROFILE(RUN_TASK) run_task(task);
		PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
	}

	if (num_workers == 1)
		return 0;

#ifndef DISABLE_MANAGER
	if (quiescent) {
		goto RT_barrier_exit;
	}
#endif

	try_send_steal_request(/* idle = */ true);
	assert(requested);

	PROFILE(IDLE) {

	while (!RECV_TASK(&task)) {
		assert(deque_list_tl_empty(deque));
		assert(requested);
		decline_all_steal_requests();
#ifndef DISABLE_MANAGER
		if (quiescent) {
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
	if (task->victim != -1) {
		last_victim = task->victim;
		assert(last_victim != ID);
	}
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
	requested--;
	assert(0 <= requested && requested < MAXSTEAL);
#if STEAL == adaptive
	num_steals_exec_recently++;
#endif
	PROFILE(RUN_TASK) run_task(task);
	PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
	goto empty_local_queue;

RT_barrier_exit:
	// Execution continues, but quiescent remains true
	assert(quiescent);

#ifdef DEBUG_TD
	LOG(">>> Worker %d leaves barrier <<<\n", ID);
#endif

	return 0;
}

#ifdef LAZY_FUTURES

#define READY ((f->has_channel && channel_receive(f->chan, data, size)) || f->set)

void RT_force_future(lazy_future *f, void *data, unsigned int size)

#else // Regular, eagerly allocated futures

#define READY (channel_receive(chan, data, size))

void RT_force_future(Channel *chan, void *data, unsigned int size)

#endif
{
	Task *task;
	Task *this = get_current_task();
	struct steal_request req;
	int loot;

#ifndef LAZY_FUTURES
	assert(channel_impl(chan) == SPSC);
#endif

	if (READY)
		goto RT_force_future_return;

	while ((task = pop_child()) != NULL) {
		PROFILE(RUN_TASK) run_task(task);
		PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
		if (READY)
			goto RT_force_future_return;
	}

	assert(get_current_task() == this);

	while (!READY) {
		try_send_steal_request(/* idle = */ false);
		PROFILE(IDLE) {

		while (!RECV_TASK(&task, /* idle = */ false)) {
			// We might inadvertently remove our own steal request in
			// handle_steal_request, so:
			PROFILE_STOP(IDLE);
			try_send_steal_request(/* idle = */ false);
			// Check if someone requested to steal from us
			while (RECV_REQ(&req))
				handle_steal_request(&req);
			PROFILE_START(IDLE);
			if (READY) {
				PROFILE_STOP(IDLE);
				goto RT_force_future_return;
			}
		}

		} // PROFILE
		loot = task->batch;
#ifdef STEAL_LASTVICTIM
		if (task->victim != -1) {
			last_victim = task->victim;
			assert(last_victim != ID);
		}
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
		requested--;
		assert(0 <= requested && requested < MAXSTEAL);
#if STEAL == adaptive
		num_steals_exec_recently++;
#endif
		PROFILE(RUN_TASK) run_task(task);
		PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
	}

#undef READY

RT_force_future_return:
#ifdef LAZY_FUTURES
	if (!f->has_channel) {
		assert(f->set);
		memcpy(data, f->buf, size);
	} else {
		assert(f->chan != NULL);
		assert(channel_impl(f->chan) == SPSC);
		channel_free(f->chan);
	}
#endif
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

#ifdef STEAL_EARLY

// Try to send a steal request when number of local tasks <= STEAL_EARLY_THRESHOLD
static inline void try_steal(void)
{
	if (num_workers == 1)
		return;

	if (deque_list_tl_num_tasks(deque) <= STEAL_EARLY_THRESHOLD) {
		// By definition not yet idle
		try_send_steal_request(/* idle = */ false);
	}
}

#endif // STEAL_EARLY

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

	// Sending an idle steal request at this point may lead to termination
	// detection when we're about to quit! Steal requests with idle == false are okay.

#ifdef STEAL_EARLY
	if (task && !task->is_loop) {
		try_steal();
	}
#endif

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

#ifdef STEAL_EARLY
	if (task && !task->is_loop) {
		try_steal();
	}
#endif

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
	assert(req->ID != ID);

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

	if (dup->has_future) {
		// Patch the task with a new future for the result
		// Problematic: We don't know the result type, that is, what kind of
		// values will be sent over the channel
		struct future_node *p = malloc(sizeof(struct future_node));
		p->r = p->await = NULL;
#ifdef LAZY_FUTURES
		p->f = malloc(sizeof(lazy_future));
		p->f->chan = channel_alloc(32, 0, SPSC);
		p->f->has_channel = true;
		p->f->set = false;
		futures_converted++;
#else
		p->f = channel_alloc(32, 0, SPSC);
#endif
		memcpy(dup->data, &p->f, sizeof(future));
		p->next = get_current_task()->futures;
		get_current_task()->futures = p;
		// The list of futures required by the current task must not be shared!
		dup->futures = NULL;
	}

	channel_send(req->chan, (void *)&dup, sizeof(dup));
	requests_handled++;
	tasks_sent++;
#ifdef STEAL_LASTTHIEF
	last_thief = req->ID;
#endif

	// Current task continues with lower half of iterations
	task->end = split;

	tasks_split++;

	} // PROFILE

	//LOG("Worker %2d: Continuing with [%ld, %ld)\n", ID, task->cur, task->end);
}
