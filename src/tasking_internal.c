#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include "tasking_internal.h"
#include "tasking.h"
#include "profile.h"
#include "affinity.h"

#define UNUSED __attribute__((unused))

// Shared state
atomic_t *tasking_finished;
atomic_t *num_tasks_exec;
int num_workers;

// Private state
PRIVATE int ID;
PRIVATE int num_tasks_exec_worker;
PRIVATE int worker_state; // currently unused

// Pointer to the task that is currently running
PRIVATE Task *current_task;

static int *IDs;
static pthread_t *worker_threads;
static pthread_barrier_t global_barrier;

static void *worker_entry_fn(void *args)
{
	ID = *(int *)args;
	set_current_task(NULL);
	num_tasks_exec_worker = 0;

	RT_init();
	tasking_internal_barrier();
	RT_schedule();
	tasking_internal_barrier();
	tasking_internal_statistics();
	RT_exit();

	return NULL;
}

int tasking_internal_init(int *argc UNUSED, char ***argv UNUSED)
{
	char *envval;
	int num_cpus, i;

	envval = getenv("NUM_THREADS");
	if (envval) {
		num_workers = abs(atoi(envval));
	} else {
		// May not be standard
		num_workers = sysconf(_SC_NPROCESSORS_ONLN);
	}

	num_cpus = cpu_count();
	printf("Number of CPUs: %d\n", num_cpus);

	// Probability of introducing false sharing
	//tasking_finished = (int *)malloc(sizeof(int));
	//num_tasks_exec   = (int *)malloc(sizeof(int));

	tasking_finished = (atomic_t *)malloc(64 * sizeof(atomic_t));
	num_tasks_exec   = (atomic_t *)malloc(64 * sizeof(atomic_t));

	atomic_set(tasking_finished, 0);
	atomic_set(num_tasks_exec, 0);

	IDs = (int *)malloc(num_workers * sizeof(int));
	worker_threads = (pthread_t *)malloc(num_workers * sizeof(pthread_t));

	pthread_barrier_init(&global_barrier, NULL, num_workers);

	// Master thread
	ID = IDs[0] = 0;

	// Bind master thread to CPU 0
	// XXX Calling this more than once results in a significant slowdown! Why?
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
	current_task->is_loop = false;
	current_task->start = 0;
	current_task->cur = 0;
	current_task->end = 0;

	num_tasks_exec_worker = 0;

	return 0;
}

int tasking_internal_exit_signal(void)
{
	atomic_set(tasking_finished, 1);

	return 0;
}

// To profile different parts of the runtime
PROFILE_EXTERN_DECL(RUN_TASK);
PROFILE_EXTERN_DECL(ENQ_DEQ_TASK);
PROFILE_EXTERN_DECL(SEND_RECV_TASK);
PROFILE_EXTERN_DECL(SEND_RECV_REQ);
PROFILE_EXTERN_DECL(IDLE);

extern PRIVATE unsigned int requests_sent, requests_handled;
extern PRIVATE unsigned int requests_declined, tasks_sent;
#ifdef STEAL_BACKOFF
extern PRIVATE unsigned int requests_resent;
#endif
#ifdef STEAL_ADAPTIVE
extern PRIVATE unsigned int requests_steal_one, requests_steal_half;
#endif

int tasking_internal_statistics(void)
{
	MASTER {
		printf("\n");
		printf("+========================================+\n");
		printf("|  Per-worker statistics                 |\n");
		printf("+========================================+\n");
	}

	tasking_internal_barrier();

	printf("Worker %d: %u steal requests sent\n", ID, requests_sent);
	printf("Worker %d: %u steal requests handled\n", ID, requests_handled);
	printf("Worker %d: %u steal requests declined\n", ID, requests_declined);
	printf("Worker %d: %u tasks executed\n", ID, num_tasks_exec_worker);
	printf("Worker %d: %u tasks sent\n", ID, tasks_sent);
#ifdef STEAL_BACKOFF
	printf("Worker %d: %u steal requests resent\n", ID, requests_resent);
#endif
#ifdef STEAL_ADAPTIVE
	assert(requests_steal_one + requests_steal_half == requests_sent);
	printf("Worker %d: %.2f %% steal-one\n", ID, requests_sent > 0
			? ((double)requests_steal_one/requests_sent) * 100
			: 0);
	printf("Worker %d: %.2f %% steal-half\n", ID, requests_sent > 0
			? ((double)requests_steal_half/requests_sent) * 100
			: 0);
#endif

	PROFILE_RESULTS();

	fflush(stdout);

	return 0;
}

int tasking_internal_exit(void)
{
	int i;

	// Join worker threads
	for (i = 1; i < num_workers; i++) {
		pthread_join(worker_threads[i], NULL);
	}

	pthread_barrier_destroy(&global_barrier);
	free(worker_threads);
	free(IDs);
	free(tasking_finished);
	free(num_tasks_exec);

	// Deallocate root task
	assert(is_root_task(current_task));
	free(current_task);

	return 0;
}

int tasking_internal_barrier(void)
{
	return pthread_barrier_wait(&global_barrier);
}

int tasking_tasks_exec(void)
{
	return atomic_read(num_tasks_exec);
}

bool tasking_done(void)
{
	return atomic_read(tasking_finished) == 1;
}
