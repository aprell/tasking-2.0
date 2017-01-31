#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include "tasking.h"
#include "async.h"
#include "chanref.h"

#define LOG(...) { printf(__VA_ARGS__); fflush(stdout); }

#define REDUCE_CHAN(ch, v, op) \
do { \
	typeof(v) __tmp; int __n = 0; \
	while (channel_receive(ch, &__tmp, sizeof(__tmp))) { \
		(v) op##= __tmp; \
		__n++; \
	} \
	LOG("Reduced %d value%s\n", __n, __n > 1 ? "s" : ""); \
} while (0)

int loop_test(void *_ __attribute__((unused)))
{
	long i;
	int res = 0;

	for_each_task (i) {
		res += i;
		RT_loop_split();
	}

	return res;
}

FUTURE_DECL(int, loop_test, void *_, _);

#define N 100

int main(int argc, char *argv[])
{
	int total = 0;
    chan c;

	TASKING_INIT(&argc, &argv);

    chanref_set(&c, channel_alloc(sizeof(int), 31, MPSC));

	ASYNC_FOR(loop_test, 0, N+1, NULL, c);

	// Wait for completion
	TASKING_BARRIER();

	REDUCE_CHAN(chanref_get(c), total, +);
	channel_free(chanref_get(c));

	LOG("%d\n", total);
	assert(total == (N*(N+1))/2);
	//assert(tasking_tasks_exec() == N+1);

	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
