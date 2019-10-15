#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "affinity.h"
#include "profile.h"
#include "runtime.h"
#include "tasking_internal.h"

// Shared state
int num_workers;

// Private state
PRIVATE int ID;
PRIVATE int num_tasks_exec;
PRIVATE bool tasking_finished;

// Pointer to the task that is currently running
PRIVATE Task *current_task;

static int *IDs;
static pthread_t *worker_threads;
static pthread_barrier_t global_barrier;

static int tasking_statistics(void);

static void *worker_entry_fn(void *args)
{
	ID = *(int *)args;
	set_current_task(NULL);
	num_tasks_exec = 0;
	tasking_finished = false;

	RT_init();
	pthread_barrier_wait(&global_barrier);
	// -----------------------------------

	RT_schedule();
	pthread_barrier_wait(&global_barrier);
	// -----------------------------------

	tasking_statistics();
	// -----------------------------------

	RT_exit();

	return NULL;
}

int tasking_init(UNUSED(int *argc), UNUSED(char ***argv))
{
	static int num_cpus;

	char *envval;
	int i;

	envval = getenv("NUM_THREADS");
	if (envval) {
		num_workers = atoi(envval);
		if (num_workers <= 0) {
			printf("NUM_THREADS must be > 0\n");
			exit(0);
		} else if (num_workers > MAXWORKERS) {
			printf("NUM_THREADS is truncated to %d\n", MAXWORKERS);
			num_workers = MAXWORKERS;
		}
	} else {
		// May not be standard
		num_workers = sysconf(_SC_NPROCESSORS_ONLN);
	}

	// Call cpu_count() only once, before changing the affinity of thread 0!
	// After set_thread_affinity(0), cpu_count() would return 1, and every
	// thread would end up being pinned to processor 0.
	num_cpus = (num_cpus == 0) ? cpu_count() : num_cpus;
	printf("Number of CPUs: %d\n", num_cpus);

	// Beware of false sharing!
	// int *shared_var_a = (int *)malloc(sizeof(int));
	// int *shared_var_b = (int *)malloc(sizeof(int));

	IDs = (int *)malloc(num_workers * sizeof(int));
	worker_threads = (pthread_t *)malloc(num_workers * sizeof(pthread_t));

	pthread_barrier_init(&global_barrier, NULL, num_workers);

	// Master thread
	ID = IDs[0] = 0;

	// Bind master thread to CPU 0
	set_thread_affinity(0);

	// Create num_workers-1 worker threads
	for (i = 1; i < num_workers; i++) {
		IDs[i] = i;
		pthread_create(&worker_threads[i], NULL, worker_entry_fn, &IDs[i]);
		// Bind worker threads to available CPUs in a round-robin fashion
#ifdef __MIC__
		// Take four-way hyper-threading into account (our MIC has 60 cores)
		set_thread_affinity(worker_threads[i], (i * 4) % num_cpus + (i / 60));
#else
		set_thread_affinity(worker_threads[i], i % num_cpus);
#endif
	}

	set_current_task((Task *)malloc(sizeof(Task)));
	current_task->parent = NULL;
	current_task->fn = NULL;
	current_task->start = 0;
	current_task->cur = 0;
	current_task->end = 0;
	current_task->chunks = 0;
	current_task->splittable = false;

	num_tasks_exec = 0;
	tasking_finished = false;

	RT_init();
	pthread_barrier_wait(&global_barrier);
	// -----------------------------------

	return 0;
}

int tasking_exit(void)
{
	int i;

	RT_async_action(RT_EXIT);
	pthread_barrier_wait(&global_barrier);
	// -----------------------------------

	tasking_statistics();
	// -----------------------------------

	RT_exit();

	// Join worker threads
	for (i = 1; i < num_workers; i++) {
		pthread_join(worker_threads[i], NULL);
	}

	pthread_barrier_destroy(&global_barrier);
	free(worker_threads);
	free(IDs);

	// Deallocate root task
	assert(is_root_task(current_task));
	free(current_task);

	return 0;
}

int tasking_barrier(void)
{
	return RT_barrier();
}

// To profile different parts of the runtime
PROFILE_EXTERN_DECL(RUN_TASK);
PROFILE_EXTERN_DECL(ENQ_DEQ_TASK);
PROFILE_EXTERN_DECL(SEND_RECV_TASK);
PROFILE_EXTERN_DECL(SEND_RECV_REQ);
PROFILE_EXTERN_DECL(IDLE);

extern PRIVATE unsigned int requests_sent, requests_handled;
extern PRIVATE unsigned int requests_declined, tasks_sent;
extern PRIVATE unsigned int tasks_split;
#if STEAL == adaptive
extern PRIVATE unsigned int requests_steal_one, requests_steal_half;
#endif
#ifdef LAZY_FUTURES
extern PRIVATE unsigned int futures_converted;
#endif

static int tasking_statistics(void)
{
	MASTER {
		printf("\n");
		printf("+========================================+\n");
		printf("|  Per-worker statistics                 |\n");
		printf("+========================================+\n");
	}

	pthread_barrier_wait(&global_barrier);
	// -----------------------------------

	printf("Worker %d: %u steal requests sent\n", ID, requests_sent);
	printf("Worker %d: %u steal requests handled\n", ID, requests_handled);
	printf("Worker %d: %u steal requests declined\n", ID, requests_declined);
	printf("Worker %d: %u tasks executed\n", ID, num_tasks_exec);
	printf("Worker %d: %u tasks sent\n", ID, tasks_sent);
	printf("Worker %d: %u tasks split\n", ID, tasks_split);
#if STEAL == adaptive
	assert(requests_steal_one + requests_steal_half == requests_sent);
	printf("Worker %d: %.2f %% steal-one\n", ID, requests_sent > 0
			? ((double)requests_steal_one/requests_sent) * 100
			: 0);
	printf("Worker %d: %.2f %% steal-half\n", ID, requests_sent > 0
			? ((double)requests_steal_half/requests_sent) * 100
			: 0);
#endif
#ifdef LAZY_FUTURES
	printf("Worker %d: %u futures converted\n", ID, futures_converted);
#endif

	PROFILE_RESULTS();

	fflush(stdout);

	return 0;
}
