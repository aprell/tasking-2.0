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

#define NUM_TASKS_PER_DEPTH 9
#define DEPTH 10000
// The total number of tasks in the BPC benchmark is 
// (NUM_TASKS_PER_DEPTH + 1) * DEPTH
#define NUM_TASKS_TOTAL ((NUM_TASKS_PER_DEPTH + 1) * DEPTH)
#define TASK_GRANULARITY 10 // in microseconds

#ifndef NTIME
extern PRIVATE mytimer_t timer_run_tasks;
extern PRIVATE mytimer_t timer_enq_deq_tasks;
#endif

void bpc_consume(int usec)
{
	double start, end, elapsed;
	start = Wtime_usec();
	end = usec;
	int x = 0;
	
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
		x++;
		if (x == 10) {
			(void)RT_check_for_steal_requests();
			x = 0;
		}
	}
	//printf("Elapsed: %.2lfus\n", elapsed);
}

void bpc_produce(int, int);
ASYNC_DECL(bpc_produce, int n; int d, n, d);

#ifdef LOOPTASKS
void bpc_produce_loop(void *v __attribute__((unused)))
{
	long i, s, e;

	RT_loop_init(&s, &e);

	for (i = s; i < e; i++) {
		bpc_consume(TASK_GRANULARITY);
		RT_loop_split(i+1, &e);
	}
}

ASYNC_DECL(bpc_produce_loop, void *v, v);

void bpc_produce(int n, int d)
{
	if (d > 0)
		// Create producer task...
		ASYNC(bpc_produce, n, d-1);
	else 
		return;

	// followed by n consumer tasks (in the form of a loop task)
	ASYNC_FOR(bpc_produce_loop, 0, n, NULL);
}

#else

ASYNC_DECL(bpc_consume, int usec, usec);

void bpc_produce(int n, int d)
{
	int i;

	if (d > 0)
		// Create producer task...
		ASYNC(bpc_produce, n, d-1);
	else 
		return;

	// followed by n consumer tasks
	for (i = 0; i < n; i++) {
		ASYNC(bpc_consume, TASK_GRANULARITY);
	}
}
#endif

void bpc_produce_seq(int n, int d)
{
	int i;

	if (d > 0)
		bpc_produce_seq(n, d-1);
	else 
		return;

	for (i = 0; i < n; i++) {
		bpc_consume(TASK_GRANULARITY);
	}
}

int main(int argc, char *argv[])
{
	double start, end;
	int i;

	TASKING_INIT(&argc, &argv);

	start = Wtime_msec();
	
	//bpc_produce_seq(NUM_TASKS_PER_DEPTH, DEPTH);

	for (i = 0; i < 1; i++) {
		bpc_produce(NUM_TASKS_PER_DEPTH, DEPTH);
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
