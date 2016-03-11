#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include "channel.h"
#include "atomic.h"
#include "utest.h"

#if 0
#include <ck_spinlock.h>
#define pthread_mutex_t          ck_spinlock_t
#define pthread_mutex_init(x, _) ck_spinlock_init(x)
#define pthread_mutex_destroy(_) ((void)0)
#define pthread_mutex_lock(x)    ck_spinlock_lock(x)
#define pthread_mutex_unlock(x)  ck_spinlock_unlock(x)
#endif

struct SHM_channel {
	pthread_mutex_t head_lock, tail_lock;
	// The owner that has allocated the channel
	int owner;
	// Internal implementation (MPMC, MPSC, or SPSC)
	int impl;
	int closed;
	// A channel of size n can buffer n-1 items
	// This allows us to distinguish between an empty channel and a full channel
	// without needing to calculate (or maintain) the number of items
	unsigned int size;
	// Up to itemsize bytes can be exchanged over this channel
	unsigned int itemsize;
	// Items are taken from head and new items are inserted at tail
	unsigned int head;
	// Separate head and tail by at least one cache line
	char pad[64];
	unsigned int tail;
	// Channel buffer
	char *buffer;
};

#define INC(i, s) \
	(((i) + 1) % (s))

#define DEC(i, s) \
	(((i) - 1 + (s)) % (s))

#define NUM_ITEMS(chan) \
	(((chan)->size + (chan)->tail - (chan)->head) % ((chan)->size))

#define IS_FULL(chan) \
	(NUM_ITEMS(chan) == (chan)->size - 1)

#define IS_EMPTY(chan) \
	((chan)->head == (chan)->tail)

// Special macros for unbuffered (synchronous) channels

// 0 or 1
#define NUM_ITEMS_UNBUF(chan) \
	((chan)->head)

// 1 == full
#define IS_FULL_UNBUF(chan) \
	((chan)->head == 1)

// 0 == empty
#define IS_EMPTY_UNBUF(chan) \
	((chan)->head == 0)

#define IS_MULTIPLE(num, n) (((num) & ((n)-1)) == 0x0)

Channel *channel_alloc(unsigned int size, unsigned int n, int impl)
{
	Channel *chan;
	unsigned int bufsize;

	if (impl != MPMC && impl != MPSC && impl != SPSC) {
		fprintf(stderr, "Warning: Requested invalid channel implementation\n");
		fprintf(stderr, "Must be either MPMC, MPSC, or SPSC\n");
		return NULL;
	}

	chan = (Channel *)malloc(sizeof(Channel));
	if (!chan) {
		fprintf(stderr, "Warning: malloc failed\n");
		return NULL;
	}

	// To buffer n items, we must allocate space for n + 1
	bufsize = (n + 1) * size;
	chan->buffer = (char *)malloc(bufsize);
	if (!chan->buffer) {
		fprintf(stderr, "Warning: malloc failed\n");
		free(chan);
		return NULL;
	}

	pthread_mutex_init(&chan->head_lock, NULL);
	pthread_mutex_init(&chan->tail_lock, NULL);

	//XXX
	chan->owner = -1;
	chan->impl = impl;
	chan->closed = 0;
	chan->size = n + 1;
	chan->itemsize = size;
	chan->head = 0;
	chan->tail = 0;

	return chan;
}

Channel *channel_alloc(unsigned int size, unsigned int n)
{
	return channel_alloc(size, n, MPMC);
}

// Dummy channel: nothing is ever written to or read from it
Channel *channel_alloc(int impl)
{
	Channel *chan = (Channel *)malloc(sizeof(Channel));
	if (!chan) {
		fprintf(stderr, "Warning: malloc failed\n");
		return NULL;
	}

	chan->buffer = NULL;

	pthread_mutex_init(&chan->head_lock, NULL);
	pthread_mutex_init(&chan->tail_lock, NULL);

	//XXX
	chan->owner = -1;
	chan->impl = impl;
	chan->closed = 0;
	chan->size = 0;
	chan->itemsize = 0;
	chan->head = 0;
	chan->tail = 0;

	return chan;
}

void channel_free(Channel *chan)
{
	if (!chan)
		return;

	if (chan->buffer)
		free(chan->buffer);

	pthread_mutex_destroy(&chan->head_lock);
	pthread_mutex_destroy(&chan->tail_lock);

	free(chan);
}

//////////////////////////////////////////////////////////////////////////////
//
//	MPMC implementation
//
//////////////////////////////////////////////////////////////////////////////

static bool channel_send_unbuffered_mpmc(Channel *chan, void *data, unsigned int size)
{
	if (IS_FULL_UNBUF(chan))
		return false;

	pthread_mutex_lock(&chan->head_lock);

	if (IS_FULL_UNBUF(chan)) {
		// Someone was faster
		pthread_mutex_unlock(&chan->head_lock);
		return false;
	}

	assert(IS_EMPTY_UNBUF(chan));
	assert(size <= chan->itemsize);
	memcpy(chan->buffer, data, size);

	chan->head = 1;
	//assert(IS_FULL_UNBUF(chan));

	pthread_mutex_unlock(&chan->head_lock);

	return true;
}

static bool channel_send_mpmc(Channel *chan, void *data, unsigned int size)
{
	assert(chan != NULL);
	assert(data != NULL);

	if (channel_unbuffered(chan))
		return channel_send_unbuffered_mpmc(chan, data, size);

	if (IS_FULL(chan))
		return false;

	pthread_mutex_lock(&chan->tail_lock);

	if (IS_FULL(chan)) {
		pthread_mutex_unlock(&chan->tail_lock);
		return false;
	}

	assert(!IS_FULL(chan));
	assert(size <= chan->itemsize);
	memcpy(chan->buffer + chan->tail * chan->itemsize, data, size);

	chan->tail = INC(chan->tail, chan->size);

	pthread_mutex_unlock(&chan->tail_lock);

	return true;
}

static bool channel_recv_unbuffered_mpmc(Channel *chan, void *data, unsigned int size)
{
	if (IS_EMPTY_UNBUF(chan))
		return false;

	pthread_mutex_lock(&chan->head_lock);

	if (IS_EMPTY_UNBUF(chan)) {
		// Someone was faster
		pthread_mutex_unlock(&chan->head_lock);
		return false;
	}

	assert(IS_FULL_UNBUF(chan));
	assert(size <= chan->itemsize);
	memcpy(data, chan->buffer, size);

	chan->head = 0;
	assert(IS_EMPTY_UNBUF(chan));

	pthread_mutex_unlock(&chan->head_lock);

	return true;
}

static bool channel_recv_mpmc(Channel *chan, void *data, unsigned int size)
{
	assert(chan != NULL);
	assert(data != NULL);

	if (channel_unbuffered(chan))
		return channel_recv_unbuffered_mpmc(chan, data, size);

	if (IS_EMPTY(chan))
		return false;

	pthread_mutex_lock(&chan->head_lock);

	if (IS_EMPTY(chan)) {
		pthread_mutex_unlock(&chan->head_lock);
		return false;
	}

	assert(!IS_EMPTY(chan));
	assert(size <= chan->itemsize);
	memcpy(data, chan->buffer + chan->head * chan->itemsize, size);

	chan->head = INC(chan->head, chan->size);

	pthread_mutex_unlock(&chan->head_lock);

	return true;
}

// Unsynchronized!
static bool channel_close_mpmc(Channel *chan)
{
	assert(chan != NULL);

	if (channel_closed(chan)) {
		// Channel is already closed
		return false;
	}

	atomic_set(&chan->closed, 1);

	return true;
}

// Unsynchronized!
static bool channel_open_mpmc(Channel *chan)
{
	assert(chan != NULL);

	if (!channel_closed(chan)) {
		// Channel is open
		return false;
	}

	atomic_set(&chan->closed, 0);

	return true;
}

//////////////////////////////////////////////////////////////////////////////
//
//	MPSC implementation
//
//////////////////////////////////////////////////////////////////////////////

static bool channel_send_mpsc(Channel *chan, void *data, unsigned int size)
{
	return channel_send_mpmc(chan, data, size);
}

static bool channel_recv_unbuffered_mpsc(Channel *chan, void *data, unsigned int size)
{
	if (IS_EMPTY_UNBUF(chan))
		return false;

	assert(IS_FULL_UNBUF(chan));
	assert(size <= chan->itemsize);
	memcpy(data, chan->buffer, size);
	__memory_barrier(); // Compiler + memory barrier
	chan->head = 0;

	return true;
}

static bool channel_recv_mpsc(Channel *chan, void *data, unsigned int size)
{
	assert(chan != NULL);
	assert(data != NULL);

	unsigned int newhead;

	if (channel_unbuffered(chan))
		return channel_recv_unbuffered_mpsc(chan, data, size);

	if (IS_EMPTY(chan))
		return false;

	assert(!IS_EMPTY(chan));
	assert(size <= chan->itemsize);
	memcpy(data, chan->buffer + chan->head * chan->itemsize, size);

	newhead = INC(chan->head, chan->size);
	__memory_barrier(); // Compiler + memory barrier
	chan->head = newhead;

	return true;
}

// Unsynchronized!
static bool channel_close_mpsc(Channel *chan)
{
	assert(chan != NULL);

	if (channel_closed(chan)) {
		// Channel is already closed
		return false;
	}

	atomic_set(&chan->closed, 1);

	return true;
}

// Unsynchronized!
static bool channel_open_mpsc(Channel *chan)
{
	assert(chan != NULL);

	if (!channel_closed(chan)) {
		// Channel is open
		return false;
	}

	atomic_set(&chan->closed, 0);

	return true;
}

//////////////////////////////////////////////////////////////////////////////
//
//	SPSC implementation
//
//////////////////////////////////////////////////////////////////////////////

static bool channel_send_unbuffered_spsc(Channel *chan, void *data, unsigned int size)
{
	if (IS_FULL_UNBUF(chan))
		return false;

	assert(IS_EMPTY_UNBUF(chan));
	assert(size <= chan->itemsize);
	memcpy(chan->buffer, data, size);
	__memory_barrier(); // Compiler + memory barrier
	chan->head = 1;

	return true;
}

static bool channel_send_spsc(Channel *chan, void *data, unsigned int size)
{
	assert(chan != NULL);
	assert(data != NULL);

	unsigned int newtail;

	if (channel_unbuffered(chan))
		return channel_send_unbuffered_spsc(chan, data, size);

	if (IS_FULL(chan))
		return false;

	assert(!IS_FULL(chan));
	assert(size <= chan->itemsize);
	memcpy(chan->buffer + chan->tail * chan->itemsize, data, size);

	newtail = INC(chan->tail, chan->size);
	__memory_barrier(); // Compiler + memory barrier
	chan->tail = newtail;

	return true;
}

// Unsynchronized!
static bool channel_close_spsc(Channel *chan)
{
	assert(chan != NULL);

	if (channel_closed(chan)) {
		// Channel is already closed
		return false;
	}

	atomic_set(&chan->closed, 1);

	return true;
}

// Unsynchronized!
static bool channel_open_spsc(Channel *chan)
{
	assert(chan != NULL);

	if (!channel_closed(chan)) {
		// Channel is open
		return false;
	}

	atomic_set(&chan->closed, 0);

	return true;
}

static bool channel_recv_spsc(Channel *chan, void *data, unsigned int size)
{
	return channel_recv_mpsc(chan, data, size);
}

typedef bool (*channel_send_fn)(Channel *, void *, unsigned int);
typedef bool (*channel_recv_fn)(Channel *, void *, unsigned int);
typedef bool (*channel_close_fn)(Channel *);
typedef bool (*channel_open_fn)(Channel *);

// Function tables
static channel_send_fn send_fn[3] =
{
	channel_send_mpmc,
	channel_send_mpsc,
	channel_send_spsc
};

static channel_recv_fn recv_fn[3] =
{
	channel_recv_mpmc,
	channel_recv_mpsc,
	channel_recv_spsc
};

static channel_close_fn close_fn[3] =
{
	channel_close_mpmc,
	channel_close_mpsc,
	channel_close_spsc
};

static channel_open_fn open_fn[3] =
{
	channel_open_mpmc,
	channel_open_mpsc,
	channel_open_spsc
};

// Send an item to the channel
// (Insert an item at the back of the channel buffer)
bool channel_send(Channel *chan, void *data, unsigned int size)
{
	return send_fn[chan->impl](chan, data, size);
}

// Receive an item from the channel
// (Remove an item from the front of the channel buffer)
bool channel_receive(Channel *chan, void *data, unsigned int size)
{
	return recv_fn[chan->impl](chan, data, size);
}

// Close channel
bool channel_close(Channel *chan)
{
	return close_fn[chan->impl](chan);
}

// (Re)open channel
bool channel_open(Channel *chan)
{
	return open_fn[chan->impl](chan);
}

int channel_owner(Channel *chan)
{
	if (!chan)
		return -1;

	return chan->owner;
}

unsigned int channel_peek(Channel *chan)
{
	if (channel_unbuffered(chan))
		return NUM_ITEMS_UNBUF(chan);

	return NUM_ITEMS(chan);
}

unsigned int channel_capacity(Channel *chan)
{
	return chan->size-1;
}

bool channel_buffered(Channel *chan)
{
	return chan->size-1 > 0;
}

int channel_impl(Channel *chan)
{
	return chan->impl;
}

void channel_inspect(Channel *chan)
{
	unsigned int i, j;

	if (channel_unbuffered(chan))
		return;

	printf("[ ");
	for (i = chan->head, j = 0; i != chan->tail; i = INC(i, chan->size), j++)
		printf("X ");
	for (i = 0; i < chan->size - j; i++)
		printf("  ");
	printf("]\n");
	fflush(stdout);
}

bool channel_closed(Channel *chan)
{
	return (bool)(atomic_read(&chan->closed) == 1);
}

#define MASTER 		WORKER(0)
#define WORKER(id) 	if (A->ID == (id))
#define PRINT(...)  { printf(__VA_ARGS__); fflush(stdout); }

#define channel_send(c, d, s) \
{ \
	while (!channel_send(c, d, s)) ; \
	/*if (channel_unbuffered(c)) {*/ \
		/* Wait until value has been received */ \
		/*while (channel_peek(c)) ;*/ \
	/*}*/ \
}

#define channel_receive(c, d, s) \
{ \
	while (!channel_receive(c, d, s)) ; \
}

struct thread_args { int ID; Channel *chan; };

static void *thread_func(void *args)
{
	struct thread_args *A = (struct thread_args *)args;

#define SENDER	 1
#define RECEIVER 0

	//
	// Worker RECEIVER:
	// ---------
	// <- chan
	// <- chan
	// <- chan
	//
	// Worker SENDER:
	// ---------
	// chan <- 42
	// chan <- 53
	// chan <- 64
	//
	WORKER(RECEIVER) {
		int val, j;
		for (j = 0; j < 3; j++) {
			channel_receive(A->chan, &val, sizeof(val));
			assert(val == 42 + j*11);
		}
	}

	WORKER(SENDER) {
		int val, j;
		check_equal(channel_peek(A->chan), 0);
		for (j = 0; j < 3; j++) {
			val = 42 + j*11;
			channel_send(A->chan, &val, sizeof(val));
		}
	}

	return NULL;
}

UTEST(Channel)
{
	int I, i;

	Channel *chan;

	// Test all channel implementations
	for (I = MPMC; I <= SPSC; I++) {

	for (i = 0; i < 2; i++) {
		switch (i) {
		case 0:
			// Buffered
			chan = channel_alloc(sizeof(int), 7, I);
			check_equal(channel_peek(chan), 0);
			check_equal(channel_capacity(chan), 7);
			check_equal(channel_buffered(chan), true);
			check_equal(channel_unbuffered(chan), false);
			check_equal(chan->impl, I);
			break;
		case 1:
			// Unbuffered
			chan = channel_alloc(32, 0, I);
			check_equal(channel_peek(chan), 0);
			check_equal(channel_capacity(chan), 0);
			check_equal(channel_buffered(chan), false);
			check_equal(channel_unbuffered(chan), true);
			check_equal(chan->impl, I);
			break;
		}

		pthread_t threads[2];
		struct thread_args args[2] = { { 0, chan }, { 1, chan } };

		pthread_create(&threads[0], NULL, thread_func, &args[0]);
		pthread_create(&threads[1], NULL, thread_func, &args[1]);

		pthread_join(threads[0], NULL);
		pthread_join(threads[1], NULL);

		channel_free(chan);
	}

	} // Test all channel implementations
}

#undef channel_receive // used in channel_range in different form

static void *thread_func_2(void *args)
{
	struct thread_args *A = (struct thread_args *)args;

#define SENDER	 1
#define RECEIVER 0
#define N 100

	//
	// Worker RECEIVER:
	// ---------
	// <- chan until closed and empty
	//
	// Worker SENDER:
	// ---------
	// chan <- 42, 53, 64, ...
	//
	WORKER(RECEIVER) {
		int val, j = 0;
		channel_range(A->chan, &val) {
			assert(val == 42 + j*11);
			j++;
		}
	}

	WORKER(SENDER) {
		int val, j;
		check_equal(channel_peek(A->chan), 0);
		for (j = 0; j < N; j++) {
			val = 42 + j*11;
			channel_send(A->chan, &val, sizeof(int));
		}
		channel_close(A->chan);
	}

	return NULL;
}

UTEST(Channel_close)
{
	int I, i;

	Channel *chan;

	// Test all channel implementations
	for (I = MPMC; I <= SPSC; I++) {

	for (i = 0; i < 2; i++) {
		switch (i) {
		case 0:
			// Buffered
			chan = channel_alloc(sizeof(int), 7, I);
			check_equal(channel_peek(chan), 0);
			check_equal(channel_capacity(chan), 7);
			check_equal(channel_buffered(chan), true);
			check_equal(channel_unbuffered(chan), false);
			check_equal(chan->impl, I);
			check_equal(chan->closed, 0);
			break;
		case 1:
			// Unbuffered
			chan = channel_alloc(32, 0, I);
			check_equal(channel_peek(chan), 0);
			check_equal(channel_capacity(chan), 0);
			check_equal(channel_buffered(chan), false);
			check_equal(channel_unbuffered(chan), true);
			check_equal(chan->impl, I);
			check_equal(chan->closed, 0);
			break;
		}

		pthread_t threads[2];
		struct thread_args args[2] = { { 0, chan }, { 1, chan } };

		pthread_create(&threads[0], NULL, thread_func_2, &args[0]);
		pthread_create(&threads[1], NULL, thread_func_2, &args[1]);

		pthread_join(threads[0], NULL);
		pthread_join(threads[1], NULL);

		channel_free(chan);
	}

	} // Test all channel implementations
}
