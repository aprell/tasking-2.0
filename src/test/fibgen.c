#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include "tasking.h"
#include "async.h"
#include "chanref.h"

// Example: channel-related convenience macros for user code
#define chan_alloc(c, sz, n) \
	chanref_set(&(c), channel_alloc(sz, n)); \

#define chan_free(c) \
	channel_free(chanref_get(c));

#define chan_send(c, v) \
do { \
	Channel *__c_tmp = chanref_get(c); \
	typeof(v) __v_tmp = (v); \
	while (!channel_send(__c_tmp, &__v_tmp, sizeof(__v_tmp))) \
		RT_check_for_steal_requests(); \
	if (channel_unbuffered(__c_tmp)) { \
		/* Wait until value has been received */ \
		while (channel_peek(__c_tmp)) \
			RT_check_for_steal_requests(); \
	} \
} while (0)

#define chan_recv(c, p) \
do { \
	Channel *__c_tmp = chanref_get(c); \
 	while (!channel_receive(__c_tmp, p, sizeof(*(p)))) \
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
	chan c1, c2;
	int fib, i;

	chan_alloc(c1, 32, 0);
	chan_alloc(c2, sizeof(int), 7);

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
	chan c;
	bool done;

	TASKING_INIT(&argc, &argv);

	chan_alloc(c, 32, 0);

	ASYNC(print_numbers, (c, 42));

	// Wait for completion
	chan_recv(c, &done);
	assert(done == true);
	chan_free(c);

	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
