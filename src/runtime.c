#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "bit.h"
#include "runtime.h"
#include "deque_list_tl.h"
// Disable unit tests
#include "utest.h"
UTEST_MAIN() {}
#include "profile.h"

#define PARTITIONS 1
#include "partition.h"
#include "partition.c"

#if MAXSTEAL > 1
#error "MAXSTEAL > 1 not yet supported"
#endif

#define LOG(...) { printf(__VA_ARGS__); fflush(stdout); }
#define UNUSED(x) x __attribute__((unused))

// Private task deque
static PRIVATE DequeListTL *deque;

// Worker -> worker: intra-partition steal requests (MPSC)
static Channel *chan_requests[MAXWORKERS];

// Worker -> worker: tasks (SPSC)
static Channel *chan_tasks[MAXWORKERS][MAXSTEAL];

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

static struct task_indicator task_indicators[MAXWORKERS];

#define LIKELY_HAS_TASKS(ID)    (atomic_read(&task_indicators[ID].tasks) > 0)
#define HAVE_TASKS()             atomic_set(&task_indicators[ID].tasks, 1)
#define HAVE_NO_TASKS()          atomic_set(&task_indicators[ID].tasks, 0)

#else

#define LIKELY_HAS_TASKS(ID)     true      // Assume yes, victim has tasks
#define HAVE_TASKS()             ((void)0) // NOOP
#define HAVE_NO_TASKS()          ((void)0) // NOOP

#endif // VICTIM_CHECK

/*
 * When a steal request is returned to its sender after MAX_STEAL_ATTEMPTS
 * unsuccessful attempts, the steal request changes state to STATE_FAILED and
 * is then passed on to parent_worker, which holds on to this request until it
 * can send tasks in return. Thus, when a worker receives a steal request whose
 * state is STATE_FAILED, the sender is either left_worker or right_worker. At
 * this point, there is a "lifeline" between parent and child: the child will
 * not send further steal requests (unless MAXSTEAL > 1) until it receives new
 * work from its parent. We have switched from work stealing to work sharing.
 * This also means that backing off from work stealing by withdrawing a steal
 * request for a short while is no longer needed, as steal requests are
 * withdrawn automatically.
 *
 * Termination occurs once worker 0 detects that both left and right subtrees
 * of workers are idle and worker 0 is itself idle.
 *
 * When a worker receives new work, it must check its "lifelines" and try to
 * distribute as many tasks as possible, thereby reactivating workers further
 * down in the tree.
 */

/*
 * Steal requests carry one of the following states:
 * - STATE_WORKING means the requesting worker is still busy
 * - STATE_IDLE means the requesting worker has run out of tasks
 * - STATE_FAILED means the requesting worker backs off and waits for tasks
 *   from its parent worker
 */

typedef unsigned char state_t;

#define STATE_WORKING  0x00
#define STATE_IDLE     0x02
#define STATE_FAILED   0x04

#define INIT_VICTIMS (0xFFFFFFFF & BIT_MASK_32(my_partition->num_workers_rt))

struct steal_request {
	Channel *chan;  // channel for sending tasks
	int ID;			// ID of requesting worker
	int try;	   	// 0 <= try <= num_workers_rt
	int partition; 	// partition in which the steal request was initiated
	int pID;		// ID of requesting worker within partition
	unsigned int victims; // Bit field of potential victims
	state_t state;  // state of steal request and, by extension, requesting worker
#if STEAL == adaptive
	bool stealhalf; // true ? attempt steal-half : attempt steal-one
	char __[2];     // pad to cache line
#else
	char __[3];	    // pad to cache line
#endif
};

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
	.victims = INIT_VICTIMS, \
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

#define BACKOFF_QUEUE_SIZE (2*MAXSTEAL + 1)

static PRIVATE struct steal_request backoff_queue[BACKOFF_QUEUE_SIZE];
static PRIVATE int backoff_queue_hd = 0, backoff_queue_tl = 0;
static PRIVATE int backoff_queue_num_entries = 0;

static inline bool BACKOFF_QUEUE_EMPTY(void)
{
	return backoff_queue_hd == backoff_queue_tl;
}

static inline bool BACKOFF_QUEUE_FULL(void)
{
	return (backoff_queue_tl + 1) % BACKOFF_QUEUE_SIZE == backoff_queue_hd;
}

static inline void BACKOFF_QUEUE_PUSH(struct steal_request *req)
{
	assert(!BACKOFF_QUEUE_FULL());
	assert(backoff_queue_num_entries < BACKOFF_QUEUE_SIZE - 1);

	backoff_queue[backoff_queue_tl] = *req;
	backoff_queue_tl = (backoff_queue_tl + 1) % BACKOFF_QUEUE_SIZE;
	backoff_queue_num_entries++;
}

static inline struct steal_request *BACKOFF_QUEUE_POP(void)
{
	assert(!BACKOFF_QUEUE_EMPTY());
	assert(backoff_queue_num_entries > 0);

	struct steal_request *req = &backoff_queue[backoff_queue_hd];
	backoff_queue_hd = (backoff_queue_hd + 1) % BACKOFF_QUEUE_SIZE;
	backoff_queue_num_entries--;

	return req;
}

static inline struct steal_request *BACKOFF_QUEUE_HEAD(void)
{
	return &backoff_queue[backoff_queue_hd];
}

// A worker can have up to MAXSTEAL outstanding steal requests:
// 0 <= requested <= MAXSTEAL
static PRIVATE int requested;

// When a worker backs off from stealing and waits for tasks from its parent
static PRIVATE bool waiting_for_tasks;

#ifdef STEAL_LASTVICTIM
// ID of last victim
static PRIVATE int last_victim = -1;
#endif

#ifdef STEAL_LASTTHIEF
// ID of last thief
static PRIVATE int last_thief = -1;
#endif

static PRIVATE int *victims;

// A worker has a unique ID within its partition:
// 0 <= pID <= num_workers_rt
static PRIVATE int pID;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

#define PRINTF(...) \
do { \
	pthread_mutex_lock(&lock); \
	printf(__VA_ARGS__); \
	fflush(stdout); \
	pthread_mutex_unlock(&lock); \
} while (0)

static inline void print_victims(unsigned int victims, int ID)
{
#define VICTIM(victims, n) (((victims) & BIT(n)) != 0 ? '1' : '0')
	assert(1 <= my_partition->num_workers_rt && my_partition->num_workers_rt <= 32);

	int i;

	pthread_mutex_lock(&lock);

	printf("victims[%2d] = ", ID);

	for (i = 31; i > my_partition->num_workers_rt-1; i--) {
		putchar('.');
	}

	for (; i > 0; i--) {
		putchar(VICTIM(victims, i));
	}

	printf("%c\n", VICTIM(victims, 0));

	pthread_mutex_unlock(&lock);
#undef VICTIM
}

// Currently only needed to count the number of workers
static void init_victims(int ID)
{
	int i, j;

	// Get all available workers in my_partition
	for (i = 0, j = 0; i < my_partition->num_workers; i++) {
		int worker = my_partition->workers[i];
		if (worker < num_workers) {
			victims[j++] = worker;
			my_partition->num_workers_rt++;
		}
	}

	MASTER LOG("Manager %2d: %d of %d workers available\n", ID,
		   my_partition->num_workers_rt, my_partition->num_workers);
}

static PRIVATE unsigned int seed;

// Initializes context needed for work-stealing
static int ws_init(void)
{
	seed = ID;
	init_victims(ID);

	return 0;
}

static PRIVATE int left_worker = -1;
static PRIVATE int right_worker = -1;
static PRIVATE int parent_worker = -1;
static PRIVATE int num_children = 0;

static inline int left_child(int ID)
// requires ID >= 0
{
	int child = 2*ID + 1;

	return child < num_workers ? child : -1;
}

static inline int right_child(int ID)
// requires ID >= 0
{
	int child = 2*ID + 2;

	return child < num_workers ? child : -1;
}

static inline int parent(int ID)
// requires ID >= 0
// ensures ID == 0 ==> \result == -1
// ensures ID > 0 ==> \result >= 0
{
	return ID % 2 != 0 ? (ID - 1) / 2 : (ID - 2) / 2;
}

static inline bool subtree_is_idle(UNUSED(int ID))
{
	return backoff_queue_num_entries == num_children * MAXSTEAL;
}

static inline void mark_as_idle(unsigned int *victims, int n)
// requires victims != NULL
// requires -1 <= n < num_workers
{
	// Valid worker ID?
	if (n == -1) return;

	if (n < num_workers) {
		mark_as_idle(victims, left_child(n));
		mark_as_idle(victims, right_child(n));
		// Unset worker n
		*victims &= ~BIT(n);
	}
}

// Find the rightmost potential victim != ID
static inline int rightmost_victim(unsigned int victims, int ID)
{
	int victim = rightmost_one_bit_pos(victims);
	if (victim == ID) {
		victim = rightmost_one_bit_pos(ZERO_RIGHTMOST_ONE_BIT(victims));
	}

	assert(
		/* in case of success */ (0 <= victim && victim < my_partition->num_workers_rt && victim != ID) ||
		/* in case of failure */ victim == -1
	);

	return victim;
}

PRIVATE unsigned int random_receiver_calls, random_receiver_early_exits;

// Choose a random victim != ID from the list of potential victims
static inline int random_victim(unsigned int victims, int ID)
{
	unsigned int i, j, n;

	random_receiver_calls++;
	random_receiver_early_exits++;

	// No eligible victim? Return message to sender.
	if (victims == 0) return -1;

#define POTENTIAL_VICTIM(victims, n) ((victims) & BIT(n))

	// Try to choose a victim at random
	for (i = 0; i < 3; i++) {
		int victim = rand_r(&seed) % my_partition->num_workers_rt;
		if (POTENTIAL_VICTIM(victims, victim) && victim != ID) return victim;
	}

	random_receiver_early_exits--;

	// Build list of potential victims and select one of them at random

	unsigned int num_victims = count_one_bits(victims);
	assert(0 < num_victims && num_victims < (unsigned int)my_partition->num_workers_rt);

	// Length of array is upper-bounded by the number of workers, but
	// num_victims is likely less than that, or we would have found a victim
	// above
	int potential_victims[num_victims];

	for (i = 0, j = 0, n = victims; n != 0; i++, n >>= 1) {
		if (POTENTIAL_VICTIM(n, 0)) {
			potential_victims[j++] = i;
		}
	}

	assert(j == num_victims);
	// assert(is_sorted(potential_victims, num_victims));

	int victim = potential_victims[rand_r(&seed) % num_victims];
	assert(POTENTIAL_VICTIM(victims, victim));

#undef POTENTIAL_VICTIM

	assert(
		/* in case of success */ (0 <= victim && victim < my_partition->num_workers_rt && victim != ID) ||
		/* in case of failure */ victim == -1
	);

	return victim;
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

	victims = (int *)malloc(MAXWORKERS * sizeof(int));

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

	left_worker = left_child(ID);
	if (left_worker != -1) num_children++;

	right_worker = right_child(ID);
	if (right_worker != -1) num_children++;

	assert(left_worker != ID && right_worker != ID);
	assert(0 <= num_children && num_children <= 2);

	parent_worker = parent(ID);

	MASTER assert(parent_worker == -1);
	else assert(parent_worker >= 0);

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

	free(victims);

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

	LOG("Worker %d: random_receiver fast path (slow path): %3.0f %% (%3.0f %%)\n",
		ID, (double)random_receiver_early_exits * 100 / random_receiver_calls,
		(100 - ((double)random_receiver_early_exits * 100 / random_receiver_calls)));

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

static int next_victim(struct steal_request *req)
{
	int victim = -1;

#define POTENTIAL_VICTIM(n) ((req->victims) & BIT(n))

	if (req->ID == ID) {
		assert(req->try == 0);
		// Initially: send message to random worker != ID
		req->victims &= ~BIT(ID);
		do { victim = rand_r(&seed) % my_partition->num_workers_rt; } while (victim == ID);
	} else if (req->try == MAX_STEAL_ATTEMPTS) {
		// Return steal request to thief
		req->victims &= ~BIT(ID);
		//print_victims(req->victims, req->ID);
		victim = req->ID;
	} else {
		// Forward steal request to different worker != ID, if possible
		// TODO: Right now, we can only detect when the entire subtree rooted
		// at worker ID is idle.
		if (subtree_is_idle(ID)) {
			mark_as_idle(&req->victims, ID);
		} else {
			req->victims &= ~BIT(ID);
		}
		assert(!POTENTIAL_VICTIM(ID));
		victim = random_victim(req->victims, req->ID);
	}

#undef POTENTIAL_VICTIM

	if (victim == -1) {
		// Couldn't find victim; return steal request to thief
		assert(req->victims == 0);
		victim = req->ID;
	}

#if 0
	if (victim == req->ID) {
		LOG("%d -{%d}-> %d after %d tries (%u ones)\n",
			ID, req->ID, victim, req->try, count_one_bits(req->victims));
	}
#endif

	assert(0 <= victim && victim < my_partition->num_workers_rt && victim != ID);
	assert(0 <= req->try && req->try <= MAX_STEAL_ATTEMPTS);

	return victim;
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

static void try_send_steal_request(bool);
static void decline_steal_request(struct steal_request *);
static void decline_all_steal_requests(void);
static void split_loop(Task *, struct steal_request *);

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
		ret = channel_receive(chan_requests[ID], req, sizeof(*req));
		while (ret && req->state == STATE_FAILED) {
#ifdef DEBUG_TD
			LOG("Worker %d receives STATE_FAILED from worker %d\n", ID, req->ID);
#endif
			assert(req->ID == left_worker || req->ID == right_worker);
			// Hold on to this steal request
			BACKOFF_QUEUE_PUSH(req);
			ret = channel_receive(chan_requests[ID], req, sizeof(*req));
		}
		// No special treatment for other states
		assert((ret && req->state != STATE_FAILED) || !ret);
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

	if (!ret) {
		try_send_steal_request(/* idle = */ idle);
	} else {
		waiting_for_tasks = false;
	}

	return ret;
}

static inline bool RECV_TASK(Task **task)
{
	return RECV_TASK(task, true);
}

#define FORGET_REQ(req) \
do { \
	assert((req)->ID == ID); \
	assert(requested); \
	requested--; \
	CHANNEL_PUSH((req)->chan); \
} while (0)

static PRIVATE bool quiescent;

static inline void detect_termination(void)
{
	assert(ID == MASTER_ID);
	assert(subtree_is_idle(MASTER_ID));
#if MAXSTEAL == 1
	assert(!quiescent);
#endif

#ifdef DEBUG_TD
	LOG(">>> Worker %d detects termination <<<\n", ID);
#endif
	quiescent = true;
}

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

	if (left_worker != -1) {
		async_action(notify_workers, chan_tasks[left_worker][0]);
	}

	if (right_worker != -1) {
		async_action(notify_workers, chan_tasks[right_worker][0]);
	}

	WORKER num_tasks_exec--;

	tasking_finished = true;
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

// Try to send a steal request
// Every worker can have at most MAXSTEAL pending steal requests. A steal
// request with idle == false indicates that the requesting worker is still
// busy working on some tasks. A steal request with idle == true indicates that
// the requesting worker is in fact idle and has nothing to work on.
static void try_send_steal_request(bool idle)
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

// Pass steal request on to another worker
static void decline_steal_request(struct steal_request *req)
{
	assert(req->try < MAX_STEAL_ATTEMPTS+1);

	req->try++;

	PROFILE(SEND_RECV_REQ) {

	requests_declined++;

	if (req->ID == ID) {
		// Steal request was returned
		if (req->state == STATE_IDLE && subtree_is_idle(ID)) {
			MASTER {
				// TODO: Is this the best place to detect termination?
				detect_termination();
				FORGET_REQ(req);
			} else {
				// Pass steal request on to parent
				mark_as_idle(&req->victims, ID);
				req->state = STATE_FAILED;
#ifdef DEBUG_TD
				LOG("Worker %d sends STATE_FAILED to worker %d\n", ID, parent_worker);
#endif
#if 0
				LOG("%d -{%d}-> %d (%d tries, %d victims unchecked)\n",
					ID, req->ID, parent_worker, req->try, count_one_bits(req->victims));
#endif
				SEND_REQ_WORKER(parent_worker, req);
				assert(!waiting_for_tasks);
				waiting_for_tasks = true;
			}
		} else {
			// Continue circulating the steal request
			//print_victims(req->victims, ID);
			req->try = 0;
			req->victims = INIT_VICTIMS;
			SEND_REQ_WORKER(next_victim(req), req);
		}
	} else {
		SEND_REQ_WORKER(next_victim(req), req);
	}

	} // PROFILE
}

static void decline_all_steal_requests(void)
{
	struct steal_request req;

	PROFILE_STOP(IDLE);

	if (RECV_REQ(&req)) {
		// decline_all_steal_requests is only called when a worker has nothing
		// else to do but relay steal requests, which means the worker is idle.
		if (req.ID == ID && req.state == STATE_WORKING) {
			req.state = STATE_IDLE;
		}
		decline_steal_request(&req);
	}

	PROFILE_START(IDLE);
}

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

#ifdef STEAL_EARLY
#ifndef STEAL_EARLY_THRESHOLD
#define STEAL_EARLY_THRESHOLD 0
#endif
#endif

// Handle a steal request by sending tasks in return or passing it on to
// another worker
static void handle_steal_request(struct steal_request *req)
{
	Task *task;
	int loot = 1;

	if (req->ID == ID) {
		assert(req->state != STATE_FAILED);
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
			// TODO: Which is more reasonable: Continue circulating steal
			// request or dropping it for now?
			//FORGET_REQ(req);
			decline_steal_request(req);
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
		// There's nothing we can do with this steal request except pass it on
		// to a different worker
		assert(deque_list_tl_empty(deque));
		decline_steal_request(req);
		HAVE_NO_TASKS();
	}
}

// Loop task with iterations left for splitting?
#define SPLITTABLE(t) \
	((bool)((t) != NULL && (t)->is_loop && abs((t)->end - (t)->cur) > (t)->sst))

// Convenience function for handling a steal request
// Returns true if work is available, false otherwise
static inline bool handle(struct steal_request *req)
{
	Task *this = get_current_task();

	// Send independent task(s) if possible
	if (!deque_list_tl_empty(deque)) {
		handle_steal_request(req);
		return true;
	}

	// Split current task (this) if possible
	if (SPLITTABLE(this)) {
		if (req->ID != ID) {
			split_loop(this, req);
			return true;
		} else {
			HAVE_NO_TASKS();
			FORGET_REQ(req);
			return false;
		}
	}

	if (req->state == STATE_FAILED) {
		// Don't recirculate this steal request
		// TODO: Is this a reasonable decision?
		assert(req->ID == left_worker || req->ID == right_worker);
	} else {
		HAVE_NO_TASKS();
		decline_steal_request(req);
	}

	return false;
}

// Handle as many steal requests in backoff_queue as possible; leave steal
// requests that cannot be answered with tasks enqueued
static inline void share_work(void)
{
	while (!BACKOFF_QUEUE_EMPTY()) {
		// Don't dequeue yet
		struct steal_request *req = BACKOFF_QUEUE_HEAD();
		if (handle(req)) {
			// Dequeue/Discard
			(void)BACKOFF_QUEUE_POP();
		} else {
			break;
		}
	}
}

// Receive and handle steal requests
// Can be called from user code
void RT_check_for_steal_requests(void)
{
	if (!BACKOFF_QUEUE_EMPTY()) {
		share_work();
	}

	if (channel_peek(chan_requests[ID])) {
		struct steal_request req;
		while (RECV_REQ(&req)) {
			handle(&req);
		}
	}
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

		share_work();

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

	if (num_workers == 1) {
		quiescent = true;
		goto RT_barrier_exit;
	}

	if (quiescent) {
		goto RT_barrier_exit;
	}

	try_send_steal_request(/* idle = */ true);
	assert(requested);

	PROFILE(IDLE) {

	while (!RECV_TASK(&task)) {
		assert(deque_list_tl_empty(deque));
		assert(requested);
		decline_all_steal_requests();
		if (quiescent) {
			goto RT_barrier_exit;
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

	share_work();

	PROFILE(RUN_TASK) run_task(task);
	PROFILE(ENQ_DEQ_TASK) deque_list_tl_task_cache(deque, task);
	goto empty_local_queue;

RT_barrier_exit:
	// Execution continues, but quiescent remains true until new tasks are created
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

		share_work();

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

	// MASTER
	if (quiescent) {
		assert(ID == MASTER_ID);
#ifdef DEBUG_TD
		LOG(">>> Worker %d resumes execution after barrier <<<\n", ID);
#endif
		quiescent = false;
	}

	share_work();

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

	share_work();

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

	share_work();

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
