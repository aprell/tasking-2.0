#include <stdio.h>
#include "async.h"
#include "tasking.h"
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
		//RT_check_for_steal_requests();
	}
}

DEFINE_ASYNC(consume, (int));

// Measures termination detection latency when all workers are still busy,
// right after starting execution
void time_td_delay(int argc, char *argv[])
{
	double start, end;

	TASKING_INIT(&argc, &argv);

	start = Wtime_usec();

	TASKING_BARRIER();

	end = Wtime_usec();

	printf("Elapsed wall time: %.2lf us\n", end - start);

	TASKING_EXIT();
}

#ifndef N
#define N 10000
#endif

// Measures execution time for N barriers, after becoming quiescent
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

	printf("Elapsed wall time: %.2lf us\n", (end - start) / N);

	TASKING_EXIT();
}

int main(int argc, char *argv[])
{
	time_td_delay(argc, argv);

	return 0;
}
