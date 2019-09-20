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

static int NUM_TASKS_TOTAL;
static int TASK_GRANULARITY; // in microseconds
static double POLL_INTERVAL; // in microseconds

void print_usage(void)
{
	// If not specified, the polling interval is set to the value of
	// TASK_GRANULARITY, which effectively disables polling
	printf("Usage: spc <number of tasks> <task granularity (us)> "
		   "[polling interval (us)]\n");
}

PROFILE_EXTERN_DECL(RUN_TASK);
PROFILE_EXTERN_DECL(ENQ_DEQ_TASK);

static PRIVATE double poll_elapsed;

void spc_consume(int usec)
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

void spc_consume_nopoll(int usec)
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
void spc_produce_loop(void)
{
	long i;

	ASYNC_FOR (i) {
		spc_consume(TASK_GRANULARITY);
	}
}

DEFINE_ASYNC0(spc_produce_loop, ());

void spc_produce(int n)
{
	ASYNC0(spc_produce_loop, (0, n), ());
}

#else

DEFINE_ASYNC(spc_consume, (int));

void spc_produce(int n)
{
	int i;

	for (i = 0; i < n; i++) {
		ASYNC(spc_consume, (TASK_GRANULARITY));
	}
}
#endif

void spc_produce_seq(int n)
{
	int i;

	for (i = 0; i < n; i++) {
		spc_consume_nopoll(TASK_GRANULARITY);
	}
}

int main(int argc, char *argv[])
{
	double start, end;

	if (argc != 3 && argc != 4) {
		print_usage();
		exit(0);
	}

	NUM_TASKS_TOTAL = atoi(argv[1]);
	TASK_GRANULARITY = atoi(argv[2]);
	POLL_INTERVAL = argc == 4 ? atof(argv[3]) : TASK_GRANULARITY;

	TASKING_INIT(&argc, &argv);

	start = Wtime_msec();

	//spc_produce_seq(NUM_TASKS_TOTAL);
	spc_produce(NUM_TASKS_TOTAL);
	TASKING_BARRIER();

	end = Wtime_msec();

	printf("Elapsed wall time: %.2lf ms (%d us per task)\n", end-start, TASK_GRANULARITY);

	TASKING_EXIT();

	return 0;
}
