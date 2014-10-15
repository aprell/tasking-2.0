#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include "tasking.h"
#include "async.h"
#include "wtime.h"

#define N 100
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
	//printf("Elapsed: %.2lfus\n", elapsed);
}

ASYNC_DECL(consume, int usec, usec);

int main(int argc, char *argv[])
{
	TASKING_INIT(&argc, &argv);

	int i;
	for (i = 0; i < N; i++) {
		ASYNC(consume, TASK_GRANULARITY);
	}

	TASKING_BARRIER();
	TASKING_BARRIER();
	TASKING_BARRIER();

	sleep(3);
	//consume(5000000);

	ASYNC(consume, TASK_GRANULARITY*10000);
	ASYNC(consume, TASK_GRANULARITY*10000);
	ASYNC(consume, TASK_GRANULARITY*10000);

	TASKING_BARRIER();
	TASKING_BARRIER();
	TASKING_BARRIER();
	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
