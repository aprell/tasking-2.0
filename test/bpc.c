#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "profile.h"
#include "tasking.h"
#include "wtime.h"

//#define LOOPTASKS

static int DEPTH; // For example, 10000
static int NUM_TASKS_PER_DEPTH; // For example, 9
// The total number of tasks in the BPC benchmark is
// (NUM_TASKS_PER_DEPTH + 1) * DEPTH
static int NUM_TASKS_TOTAL;
static int TASK_GRANULARITY; // in microseconds
static double POLL_INTERVAL; // in microseconds

void print_usage(void)
{
	// If not specified, the polling interval is set to the value of
	// TASK_GRANULARITY, which effectively disables polling
	printf("Usage: bpc <depth> <number of tasks per depth> <task granularity (us)> "
		   "[polling interval (us)]\n");
}

PROFILE_EXTERN_DECL(RUN_TASK);
PROFILE_EXTERN_DECL(ENQ_DEQ_TASK);

static PRIVATE double poll_elapsed;

void bpc_consume(int usec)
{
	double start, end, elapsed;
	double RT_poll_elapsed = 0;
	start = Wtime_usec();
	end = usec;
	poll_elapsed = POLL_INTERVAL;

	for (;;) {
		elapsed = Wtime_usec() - start;
		elapsed -= RT_poll_elapsed;
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
		if (elapsed >= poll_elapsed) {
			double RT_poll_start = Wtime_usec();
			POLL();
			RT_poll_elapsed += Wtime_usec() - RT_poll_start;
			poll_elapsed += POLL_INTERVAL;
		}
	}
	//printf("Elapsed: %.2lfus\n", elapsed);
}

void bpc_consume_nopoll(int usec)
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

void bpc_produce(int, int);

DEFINE_ASYNC(bpc_produce, (int, int));

#ifdef LOOPTASKS
void bpc_produce_loop(void)
{
	long i;

	ASYNC_FOR (i) {
		bpc_consume(TASK_GRANULARITY);
	}
}

DEFINE_ASYNC0(bpc_produce_loop, ());

void bpc_produce(int n, int d)
{
	if (d > 0)
		// Create producer task...
		ASYNC(bpc_produce, (n, d-1));
	else
		return;

	// followed by n consumer tasks (in the form of a loop task)
	ASYNC0(bpc_produce_loop, (0, n), ());
}

#else

DEFINE_ASYNC(bpc_consume, (int));

void bpc_produce(int n, int d)
{
	int i;

	if (d > 0)
		// Create producer task...
		ASYNC(bpc_produce, (n, d-1));
	else
		return;

	// followed by n consumer tasks
	for (i = 0; i < n; i++) {
		ASYNC(bpc_consume, (TASK_GRANULARITY));
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
		bpc_consume_nopoll(TASK_GRANULARITY);
	}
}

int main(int argc, char *argv[])
{
	double start, end;

	if (argc != 4 && argc != 5) {
		print_usage();
		exit(0);
	}

	DEPTH = atoi(argv[1]);
	NUM_TASKS_PER_DEPTH = atoi(argv[2]);
	TASK_GRANULARITY = atoi(argv[3]);
	POLL_INTERVAL = argc == 5 ? atof(argv[4]) : TASK_GRANULARITY;
	NUM_TASKS_TOTAL = (NUM_TASKS_PER_DEPTH + 1) * DEPTH;

	TASKING_INIT(&argc, &argv);

	start = Wtime_msec();

	//bpc_produce_seq(NUM_TASKS_PER_DEPTH, DEPTH);
	bpc_produce(NUM_TASKS_PER_DEPTH, DEPTH);
	TASKING_BARRIER();

	end = Wtime_msec();

	printf("Elapsed wall time: %.2lf ms (%d us per task)\n", end-start, TASK_GRANULARITY);

	TASKING_EXIT();

	return 0;
}
