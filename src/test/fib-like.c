#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include "tasking.h"
#include "async.h"
#include "chanref.h"
#include "wtime.h"

static int FIB_LIKE_N; // For example, 25
static int TASK_GRANULARITY; // in microseconds

void print_usage(void)
{
	printf("Usage: fib-like <N> <task granularity (us)>\n");
}

static int compute(int usec)
{
	double start, end, elapsed;
	start = Wtime_usec();
	end = usec;

	for (;;) {
		elapsed = Wtime_usec() - start;
		if (elapsed >= end)
			break;
		// Do some dummy computation
		// Calculate fib(30) iteratively
		// Hah! How twisted! (- -')
		int fib = 0, f2 = 0, f1 = 1, i;
		for (i = 2; i <= 30; i++) {
			fib = f1 + f2;
			f2 = f1;
			f1 = fib;
		}
		//(void)RT_check_for_steal_requests();
	}
	//printf("Elapsed: %.2lfus\n", elapsed);

	return 1;
}

int fib_like_seq(int n)
{
	int x, y;

	if (n < 2)
		return compute(TASK_GRANULARITY);

	x = fib_like_seq(n-1);
	y = fib_like_seq(n-2);

	return x + y + 1;
}

int fib_like(int);

FUTURE_DECL_FREELIST(int);
FUTURE_DECL(int, fib_like, int n, n);

// Taskwait based on channel operations
int fib_like(int n)
{
	future x;
	int y;

	if (n < 2)
		return compute(TASK_GRANULARITY);

    x = __ASYNC(fib_like, n-1);
	y = fib_like(n-2);

	return __AWAIT(x, int) + y + 1;
}

int fib_like_spawn(int);

TASK_DECL(int, fib_like_spawn, int n, n);

// Taskwait based on channel operations
int fib_like_spawn(int n)
{
	atomic_t num_children = 0;
	int x, y;

	if (n < 2)
		return compute(TASK_GRANULARITY);

	SPAWN(fib_like_spawn, n-1, &x);
	y = fib_like_spawn(n-2);

	SYNC;

	return x + y + 1;
}

static void verify_result(int n, int res)
{
	static int ntasks[44] = {
		0, 0, 2, 4, 8,
		14, 24, 40, 66, 108,
		176, 286, 464, 752, 1218,
		1972, 3192, 5166, 8360, 13528,
		21890, 35420, 57312, 92734, 150048,
		242784, 392834, 635620, 1028456, 1664078,
		2692536, 4356616, 7049154, 11405772, 18454928,
		29860702, 48315632, 78176336, 126491970, 204668308,
		331160280, 535828590, 866988872, 1402817464
	};

	if (n < 0 || n > 43) {
		printf("Cannot verify result: %d out of range\n", n);
		fflush(stdout);
		return;
	}

	if (res != ntasks[n]+1) {
		printf("Fibonacci-like failed: %d != %d\n", res, ntasks[n]+1);
		fflush(stdout);
	}
}

int main(int argc, char *argv[])
{
	double start, end;
	int f;

	if (argc != 3) {
		print_usage();
		exit(0);
	}

	FIB_LIKE_N = atoi(argv[1]);
	TASK_GRANULARITY = atoi(argv[2]);

	TASKING_INIT(&argc, &argv);

	start = Wtime_msec();
	//f = fib_like_seq(FIB_LIKE_N);
	f = fib_like(FIB_LIKE_N);
	end = Wtime_msec();
	verify_result(FIB_LIKE_N, f);

	printf("Elapsed wall time: %.2lf ms (%d us per task)\n", end-start, TASK_GRANULARITY);

	// This should be moved inside TASKING_EXIT()
	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
