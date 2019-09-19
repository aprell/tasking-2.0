#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include "tasking.h"
#include "async.h"

typedef Channel *chan;

// Example: channel-related convenience macros for user code
#define chan_alloc(sz, n) channel_alloc(sz, n)

#define chan_free(c) channel_free(c)

#define chan_send(c, v) \
do { \
	typeof(v) __v_tmp = (v); \
	while (!channel_send(c, &__v_tmp, sizeof(__v_tmp))) \
		RT_check_for_steal_requests(); \
	if (channel_unbuffered(c)) { \
		/* Wait until value has been received */ \
		while (channel_peek(c)) \
			RT_check_for_steal_requests(); \
	} \
} while (0)

#define chan_recv(c, p) \
do { \
	while (!channel_receive(c, p, sizeof(*(p)))) \
		RT_check_for_steal_requests(); \
} while (0)

#define LOG(...) { printf(__VA_ARGS__); fflush(stdout); }

void produce_numbers(chan c1, chan c2)
{
	int fib = 0, f2 = 0, f1 = 1, i, n;

	chan_recv(c1, &n);

	for (i = 0; i <= n; i++) {
		if (i < 2) {
			chan_send(c2, i);
			continue;
		}
		fib = f1 + f2;
		f2 = f1;
		f1 = fib;
		chan_send(c2, fib);
	}
}

DEFINE_ASYNC(produce_numbers, (chan, chan));

void print_numbers(chan c, int n)
{
	int fib, i;

	chan c1 = chan_alloc(32, 0);
	chan c2 = chan_alloc(sizeof(int), 7);

	ASYNC(produce_numbers, (c1, c2));

	chan_send(c1, n);

	for (i = 0; i <= n; i++) {
		chan_recv(c2, &fib);
		LOG("fib(%d) = %d\n", i, fib);
	}

	// Notify completion with a message
	chan_send(c, true);
	chan_free(c1);
	chan_free(c2);
}

DEFINE_ASYNC(print_numbers, (chan, int));

int main(int argc, char *argv[])
{
	bool done;

	TASKING_INIT(&argc, &argv);

	chan c = chan_alloc(32, 0);

	ASYNC(print_numbers, (c, 42));

	// Wait for completion
	chan_recv(c, &done);
	assert(done == true);
	chan_free(c);

	TASKING_EXIT();

	return 0;
}
