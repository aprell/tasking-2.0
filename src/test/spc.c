#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include "tasking.h"
#include "async.h"
#include "wtime.h"
#include "timer.h"

//#define LOOPTASKS

#define NUM_TASKS_TOTAL  100000
#define TASK_GRANULARITY 10 // in microseconds

#ifndef NTIME
extern PRIVATE mytimer_t timer_run_tasks;
extern PRIVATE mytimer_t timer_enq_deq_tasks;
#endif

void spc_consume(int usec)
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
		int fib = 0, f2 = 0, f1 = 1, i;
		for (i = 2; i <= 30; i++) {
			fib = f1 + f2; 
			f2 = f1;
			f1 = fib;
		}
	}
	//printf("Elapsed: %.2lfus\n", elapsed);
}

#ifdef LOOPTASKS
void spc_produce_loop(void *v __attribute__((unused)))
{
	long i, s, e;

	RT_loop_init(&s, &e);

	for (i = s; i < e; i++) {
		spc_consume(TASK_GRANULARITY);
		RT_loop_split(i+1, &e);
	}
}

ASYNC_DECL(spc_produce_loop, void *v, v);

void spc_produce(int n)
{
	ASYNC_FOR(spc_produce_loop, 0, n, NULL);
}

#else

ASYNC_DECL(spc_consume, int usec, usec);

void spc_produce(int n)
{
	int i;

	for (i = 0; i < n; i++) {
		ASYNC(spc_consume, TASK_GRANULARITY);
	}
}
#endif

void spc_produce_seq(int n)
{
	int i;

	for (i = 0; i < n; i++) {
		spc_consume(TASK_GRANULARITY);
	}
}

int main(int argc, char *argv[])
{
	double start, end;
	int i;

	TASKING_INIT(&argc, &argv);

	start = Wtime_msec();
	
	//spc_produce_seq(NUM_TASKS_TOTAL);

	for (i = 0; i < 1; i++) {
		spc_produce(NUM_TASKS_TOTAL);
		TASKING_BARRIER();
#if 0
		if (tasking_tasks_exec() != NUM_TASKS_TOTAL * (i+1)) {
			printf("Tasks executed: %d\n", tasking_tasks_exec());
			printf("Warning: Barrier failed!\n");
			exit(1);
		}
#endif
	}
	
	end = Wtime_msec();

	printf("Elapsed wall time: %.2lfms\n", end - start);

	// This should be moved inside TASKING_EXIT()
	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
