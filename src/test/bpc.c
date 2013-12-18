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

static int DEPTH; // For example, 10000
static int NUM_TASKS_PER_DEPTH; // For example, 9
// The total number of tasks in the BPC benchmark is 
// (NUM_TASKS_PER_DEPTH + 1) * DEPTH
static int NUM_TASKS_TOTAL;
static int TASK_GRANULARITY; // in microseconds

void print_usage(void)
{
	printf("Usage: bpc <depth> <number of tasks per depth> <task granularity (us)>\n");
}

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

	if (argc != 4) {
		print_usage();
		exit(0);
	}

	DEPTH = atoi(argv[1]);
	NUM_TASKS_PER_DEPTH = atoi(argv[2]);
	TASK_GRANULARITY = atoi(argv[3]);
	NUM_TASKS_TOTAL = (NUM_TASKS_PER_DEPTH + 1) * DEPTH;

	TASKING_INIT(&argc, &argv);

	start = Wtime_msec();

	//bpc_produce_seq(NUM_TASKS_PER_DEPTH, DEPTH);
	bpc_produce(NUM_TASKS_PER_DEPTH, DEPTH);
	TASKING_BARRIER();

	end = Wtime_msec();

	printf("Elapsed wall time (%dus/task): %.2lf ms\n", TASK_GRANULARITY, end-start);

	// This should be moved inside TASKING_EXIT()
	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
