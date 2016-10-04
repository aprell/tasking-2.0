#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include "tasking.h"
#include "async.h"
#include "wtime.h"

#define TASK_GRANULARITY 10 // in microseconds

void consume(int usec)
{
	double start, end, elapsed;
	start = Wtime_usec();
	end = usec;

	for (;;) {
		elapsed = Wtime_usec() - start;
		if (elapsed >= end)
			break;
		int fib = 0, f2 = 0, f1 = 1, i;
		for (i = 2; i <= 30; i++) {
			fib = f1 + f2;
			f2 = f1;
			f1 = fib;
		}
		//(void)RT_check_for_steal_requests();
	}
}

ASYNC_DECL(consume, int usec, usec);

#define N 1000

// Measures termination detection latency when all workers are idle
// Don't forget to modify runtime.c!
void time_td_delay_min(int argc, char *argv[])
{
	double start, end;
	int i;

	TASKING_INIT(&argc, &argv);

	TASKING_BARRIER();

	start = Wtime_usec();

	for (i = 0; i < N; i++) {
		TASKING_BARRIER();
	}

	end = Wtime_usec();

	printf("Elapsed wall time: %.2lf us\n", (end - start) / N);

	TASKING_EXIT();
}

pthread_barrier_t td_sync;

// Measures termination detection latency when all workers are busy
// Don't forget to modify runtime.c!
void time_td_delay_max(int argc, char *argv[])
{
	double start, end;
	int i;

	TASKING_INIT(&argc, &argv);

#if 0
	if (num_workers > 1) {
		pthread_barrier_wait(&td_sync);
	}
#endif

	start = Wtime_usec();

	TASKING_BARRIER();

	end = Wtime_usec();

	printf("Elapsed wall time: %.2lf us\n", end - start);

	TASKING_EXIT();

#if 0
	if (num_workers > 1) {
		pthread_barrier_destroy(&td_sync);
	}
#endif
}

#if 0
extern double td_start, td_end;
extern double td_elapsed;

// Measures additional latency when termination detection serves as a barrier
void td_additional_delay(int argc, char *argv[])
{
	int i;

	TASKING_INIT(&argc, &argv);

	for (i = 0; i < N; i++) {
		TASKING_BARRIER();
	}

	printf("Elapsed wall time: %.2lf us\n", td_elapsed / N);

	TASKING_EXIT();
}
#endif

// Measures execution time for N barriers
void time_barriers(int argc, char *argv[])
{
	double start, end;
	int i;

	TASKING_INIT(&argc, &argv);

	TASKING_BARRIER();

	start = Wtime_usec();

	for (i = 0; i < N; i++) {
		TASKING_BARRIER();
	}

	end = Wtime_usec();

	printf("Elapsed wall time: %.2lf us\n", (end - start)/N);

	TASKING_EXIT();
}

int main(int argc, char *argv[])
{
	time_barriers(argc, argv);

	return 0;
}
