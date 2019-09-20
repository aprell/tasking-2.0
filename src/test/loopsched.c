#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "async.h"
#include "tasking.h"
#include "wtime.h"

static int num_tasks;
static int granularity; // in microseconds
static int *tasks; // of random size
static PRIVATE unsigned int rand_seed;
static double increment;
static double decrement;

enum { plain, randomized, increasing, decreasing, looptasks };
static int benchmark = plain; // default
static bool use_looptasks;

static int compute_random_task_size(int base)
{
	int tsize = base;
	int r = rand_r(&rand_seed) % 15; // 0..14

	switch (r) {
		case  0 ...  4: break;					// ca. 33.3%
		case  5 ...  8: tsize *= 10; break;		// ca. 26.6%
		case  9 ... 11: tsize *= 100; break;	// ca. 20.0%
		case 12 ... 13: tsize *= 1000; break;	// ca. 13.3%
		case 14: tsize *= 10000; break;			// ca.  6.6%
	}
#if 0
	r = rand_r(&rand_seed) % 31; // 0..30
	switch (r) {
		case  0 ... 15: break;					// ca. 51.6%
		case 16 ... 23: tsize *= 10; break;		// ca. 25.8%
		case 24 ... 27: tsize *= 100; break;	// ca. 12.9%
		case 28 ... 29: tsize *= 1000; break;	// ca.  6.5%
		case 30: tsize *= 10000; break;			// ca.  3.2%
	}
#endif
#if 0
	r = rand_r(&rand_seed) % 5; // 0..4
	switch (r) {
		case 4: tsize *= 10;
		case 3: tsize *= 10;
		case 2: tsize *= 10;
		case 1: tsize *= 10;
		case 0: break;
	}
#endif

	return tsize;
}

// Returns a constant, randomized, linearly increasing or decreasing task size
static double task_size(double base, int i)
{
	double tsize = base;

	switch (benchmark) {
	case plain:
		break;
	case randomized:
		tsize = tasks[i];
		break;
	case increasing:
		tsize += i * increment;
		break;
	case decreasing:
		tsize += i * decrement;
		break;
	}

	// printf("%.2lf\n", tsize);

	return tsize;
}

#define POLL_INTERVAL 1 // in microseconds
static PRIVATE double poll_elapsed;

// Computes for the duration of usec microseconds
void consume(double usec)
{
	double start, end, elapsed;
	double RT_poll_elapsed = 0;
	poll_elapsed = POLL_INTERVAL;

	start = Wtime_usec();
	end = usec;

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

DEFINE_ASYNC(consume, (double));

void flat_loop_outlined(void)
{
	long i;

	ASYNC_FOR (i) {
		consume(task_size(granularity, i));
	}
}

DEFINE_ASYNC0(flat_loop_outlined, ());

void flat_loop(int n)
{
	ASYNC0(flat_loop_outlined, (0, n), ());
}

void flat(int n)
{
	int i;

	for (i = 0; i < n; i++) {
		ASYNC(consume, (task_size(granularity, i)));
	}
}

void flat_seq(int n)
{
	int i;

	for (i = 0; i < n; i++) {
		consume(task_size(granularity, i));
	}
}

static struct option options[] = {
	{ "looptasks", no_argument, 0, 'l' },
	{ "randomized", no_argument, 0, 'r' },
	{ "increasing", required_argument, 0, 'i' },
	{ "decreasing", required_argument, 0, 'd' },
	{ 0, 0, 0, 0 }
};

static void getoptions(int *argc, char **argv[])
{
	int option_idx, c, i;
	double arg = 0.0;

	while ((c = getopt_long(*argc, *argv, "lri:d:", options, &option_idx)) != -1) {
		switch (c) {
		case 'l':
			use_looptasks = true;
			break;
		case 'r':
			benchmark = randomized;
			rand_seed = 42;
			break;
		case 'i':
			benchmark = increasing;
			arg = atof(optarg);
			break;
		case 'd':
			benchmark = decreasing;
			arg = atof(optarg);
			break;
		case '?':
		default:
			break;
		}
	}

	// We expect two additional non-option arguments
	if (optind + 2 != *argc) {
		printf("Usage: %s [--looptasks] [--randomized] [--increasing <up to>] [--decreasing <down to>]\n"
			   "\t<number of tasks> <task size>\n", (*argv)[0]);
		exit(0);
	}

	num_tasks = atoi((*argv)[optind++]);
	granularity = atoi((*argv)[optind++]);

	switch (benchmark) {
	case plain:
		printf("%d tasks @ %d microseconds per task %s\n",
			   num_tasks, granularity, use_looptasks ? "(loop tasks)" : "" );
		break;
	case randomized:
		printf("%d tasks @ %d, %d, %d, %d, or %d microseconds per task %s\n",
			   num_tasks, granularity, 10 * granularity, 100 * granularity,
			   1000 * granularity, 10000 * granularity,
			   use_looptasks ? "(loop tasks)" : "" );
		// Generate workload
		tasks = malloc(num_tasks * sizeof(int));
		assert(tasks && "Out of memory");
		for (i = 0; i < num_tasks; i++) {
			tasks[i] = compute_random_task_size(granularity);
		}
		break;
	case increasing:
		increment = (arg-granularity) / (num_tasks-1);
		printf("%d tasks @ %d-%.0lf microseconds per task (increment is %.2lf) %s\n",
			   num_tasks, granularity, arg, increment, use_looptasks ? "(loop tasks)" : "" );
		break;
	case decreasing:
		decrement = (arg-granularity) / (num_tasks-1);
		printf("%d tasks @ %d-%.0lf microseconds per task (decrement is %.2lf) %s\n",
			   num_tasks, granularity, arg, decrement, use_looptasks ? "(loop tasks)" : "" );
		break;
	}
}

int main(int argc, char *argv[])
{
	double start, end;

	getoptions(&argc, &argv);

	TASKING_INIT(&argc, &argv);
	TASKING_BARRIER();

	start = Wtime_msec();

	if (use_looptasks)
		flat_loop(num_tasks);
	else
		flat(num_tasks);

	TASKING_BARRIER();

	end = Wtime_msec();

	printf("Elapsed wall time: %.2lf ms\n", end - start);

	TASKING_EXIT();

	free(tasks);

	return 0;
}
