// gcc -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -DTEST -I.. channel.c -o channel && ./channel
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include "channel.h"
#include "atomic.h"

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

#ifdef CHANNEL_CACHE
// The maximum number of channels of a certain type that can be cached
#define CHANNEL_CACHE_CAPACITY CHANNEL_CACHE
#if CHANNEL_CACHE_CAPACITY < 1
#error "CHANNEL_CACHE must be > 0"
#endif

struct channel_cache {
	struct channel_cache *next;
	unsigned int chan_size;
	unsigned int chan_n;
	int chan_impl;
	int num_cached;
	Channel *cache[CHANNEL_CACHE_CAPACITY];
};

// Channel caches are per thread
static __thread struct channel_cache *channel_cache;
static __thread unsigned int channel_cache_len;

// Allocates a free list for storing channels of a given type
bool channel_cache_alloc(unsigned int size, unsigned int n, int impl)
{
	struct channel_cache *p;

	if (impl != MPMC && impl != MPSC && impl != SPSC) {
		fprintf(stderr, "Warning: Requested invalid channel implementation\n");
		fprintf(stderr, "Must be either MPMC, MPSC, or SPSC\n");
		return false;
	}

	// Avoid multiple free lists for the exact same type of channel
	for (p = channel_cache; p != NULL; p = p->next) {
		if (size == p->chan_size && n == p->chan_n && impl == p->chan_impl) {
			return false;
		}
	}

	p = (struct channel_cache *)malloc(sizeof(struct channel_cache));
	if (!p) {
		fprintf(stderr, "Warning: malloc failed\n");
		return false;
	}

	p->chan_size = size;
	p->chan_n = n;
	p->chan_impl = impl;
	p->num_cached = 0;

	p->next = channel_cache;
	channel_cache = p;
	channel_cache_len++;

	return true;
}

// Frees the entire channel cache, including all channels
void channel_cache_free(void)
{
	struct channel_cache *p, *q;
	int i;

	for (p = channel_cache; p != NULL; p = q) {
		q = p->next;
		for (i = 0; i < p->num_cached; i++) {
			Channel *chan = p->cache[i];
			if (chan->buffer)
				free(chan->buffer);
			pthread_mutex_destroy(&chan->head_lock);
			pthread_mutex_destroy(&chan->tail_lock);
			free(chan);
		}
		free(p);
		channel_cache_len--;
	}

	assert(channel_cache_len == 0);
	channel_cache = NULL;
}
#endif // CHANNEL_CACHE

Channel *channel_alloc(unsigned int size, unsigned int n, int impl)
{
	Channel *chan;
	unsigned int bufsize;
#ifdef CHANNEL_CACHE
	struct channel_cache *p;
#endif

	if (impl != MPMC && impl != MPSC && impl != SPSC) {
		fprintf(stderr, "Warning: Requested invalid channel implementation\n");
		fprintf(stderr, "Must be either MPMC, MPSC, or SPSC\n");
		return NULL;
	}

#ifdef CHANNEL_CACHE
	for (p = channel_cache; p != NULL; p = p->next) {
		if (size == p->chan_size && n == p->chan_n && impl == p->chan_impl) {
			// Check if free list contains channel
			if (p->num_cached > 0) {
				chan = p->cache[--p->num_cached];
				assert(IS_EMPTY(chan));
				return chan;
			} else {
				// There can be only one matching free list
				break;
			}
		}
	}
#endif

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

#ifdef CHANNEL_CACHE
	// In addition to allocating the channel, try to allocate a cache
	channel_cache_alloc(size, n, impl);
#endif

	return chan;
}

Channel *channel_alloc(unsigned int size, unsigned int n)
{
	return channel_alloc(size, n, MPMC);
}

void channel_free(Channel *chan)
{
#ifdef CHANNEL_CACHE
	struct channel_cache *p;
#endif

	if (!chan)
		return;

#ifdef CHANNEL_CACHE
	for (p = channel_cache; p != NULL; p = p->next) {
		if (chan->itemsize == p->chan_size &&
			chan->size-1 == p->chan_n &&
			chan->impl == p->chan_impl) {
			// Check if free list has space left
			if (p->num_cached < CHANNEL_CACHE_CAPACITY) {
				p->cache[p->num_cached++] = chan;
				return;
			} else {
				// There can be only one matching free list
				break;
			}
		}
	}
#endif

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

//==========================================================================//

#ifdef TEST

//==========================================================================//

#include "utest.h"

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

UTEST_INIT;

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

static void test_Channel(void)
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

static void test_Channel_close(void)
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

#ifdef CHANNEL_CACHE
static bool check_if_cached(Channel *chan)
{
	assert(chan != NULL);

	struct channel_cache *p;

	for (p = channel_cache; p != NULL; p = p->next) {
		if (chan->itemsize == p->chan_size &&
			chan->size-1 == p->chan_n &&
			chan->impl == p->chan_impl) {
			int i;
			for (i = 0; i < p->num_cached; i++) {
				if (chan == p->cache[i]) return true;
			}
			return false;
		}
	}

	return false;
}

// Must be run in isolation
// CHANNEL_CACHE_CAPACITY should be greater than two to keep Asan happy
static void test_Channel_cache(void)
{
	Channel *chan[10], *stash[10];
	struct channel_cache *p;
	int i;

	// Allocate channel caches explicitly
	check_equal(channel_cache_alloc(sizeof(char), 4, MPMC), true);
	check_equal(channel_cache_alloc(sizeof(int), 8, MPSC), true);
	check_equal(channel_cache_alloc(sizeof(double *), 16, SPSC), true);
	check_equal(channel_cache_alloc(sizeof(char), 4, MPMC), false);
	check_equal(channel_cache_alloc(sizeof(int), 8, MPSC), false);
	check_equal(channel_cache_alloc(sizeof(double *), 16, SPSC), false);

	for (p = channel_cache, i = SPSC; p != NULL; p = p->next, i--) {
		check_equal(p->chan_impl, i);
	}

	// Length of list is 3
	check_equal(channel_cache_len, 3);

	// Allocate channel caches implicitly
	chan[0] = channel_alloc(sizeof(char), 4, MPMC);
	chan[1] = channel_alloc(sizeof(int), 8, MPSC);
	chan[2] = channel_alloc(sizeof(double *), 16, SPSC);
	chan[3] = channel_alloc(sizeof(char), 5, MPMC);
	chan[4] = channel_alloc(sizeof(long), 8, MPSC);
	chan[5] = channel_alloc(sizeof(double *), 16, MPSC);

	// Length of list is now 6
	check_equal(channel_cache_len, 6);

	check_equal(check_if_cached(chan[0]), false);
	check_equal(check_if_cached(chan[1]), false);
	check_equal(check_if_cached(chan[2]), false);
	check_equal(check_if_cached(chan[3]), false);
	check_equal(check_if_cached(chan[4]), false);
	check_equal(check_if_cached(chan[5]), false);

	memcpy(stash, chan, 6 * sizeof(Channel *));

	channel_free(chan[0]);
	channel_free(chan[1]);
	channel_free(chan[2]);
	channel_free(chan[3]);
	channel_free(chan[4]);
	channel_free(chan[5]);

	check_equal(check_if_cached(stash[0]), true);
	check_equal(check_if_cached(stash[1]), true);
	check_equal(check_if_cached(stash[2]), true);
	check_equal(check_if_cached(stash[3]), true);
	check_equal(check_if_cached(stash[4]), true);
	check_equal(check_if_cached(stash[5]), true);

	chan[6] = channel_alloc(sizeof(char), 4, MPMC);
	chan[7] = channel_alloc(sizeof(int), 8, MPSC);
	chan[8] = channel_alloc(sizeof(float *), 16, SPSC);
	chan[9] = channel_alloc(sizeof(double *), 16, SPSC);

	// Length of list is still 6
	check_equal(channel_cache_len, 6);

	check_equal(chan[6], stash[0]);
	check_equal(chan[7], stash[1]);
	check_equal(chan[8], stash[2]);

	memcpy(stash + 6, chan + 6, 4 * sizeof(Channel *));

	channel_free(chan[6]);
	channel_free(chan[7]);
	channel_free(chan[8]);
	channel_free(chan[9]);

	check_equal(check_if_cached(stash[6]), true);
	check_equal(check_if_cached(stash[7]), true);
	check_equal(check_if_cached(stash[8]), true);
	check_equal(check_if_cached(stash[9]), true);

	channel_cache_free();

	// Length of list is back to 0
	check_equal(channel_cache_len, 0);

	chan[0] = channel_alloc(sizeof(Channel), 1, SPSC);
	chan[1] = channel_alloc(sizeof(int), 0, SPSC);
	chan[2] = channel_alloc(sizeof(int), 0, SPSC);

	// Length of list has grown to 2
	check_equal(channel_cache_len, 2);

	channel_cache_free();

	// Length of list is back to 0
	check_equal(channel_cache_len, 0);

	channel_free(chan[0]);
	channel_free(chan[1]);
	channel_free(chan[2]);
}
#endif // CHANNEL_CACHE

int main(void)
{
#ifndef CHANNEL_CACHE
	test_Channel();
	test_Channel_close();
#else
	test_Channel_cache();
#endif

	UTEST_DONE;

	return 0;
}

//==========================================================================//

#endif // TEST

//==========================================================================//
