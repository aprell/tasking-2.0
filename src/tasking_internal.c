#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include "tasking_internal.h"
#include "tasking.h"
#include "timer.h"
#include "affinity.h"

#define UNUSED __attribute__((unused))

// Shared state
int *tasking_finished;
int *num_tasks_exec;
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

	printf("Worker %2d bound to CPU %2d\n", ID, get_thread_affinity());

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

	tasking_finished = (int *)malloc(64 * sizeof(int));
	num_tasks_exec   = (int *)malloc(64 * sizeof(int));

	atomic_set(tasking_finished, 0);
	atomic_set(num_tasks_exec, 0);

	IDs = (int *)malloc(num_workers * sizeof(int));
	worker_threads = (pthread_t *)malloc(num_workers * sizeof(pthread_t));

	pthread_barrier_init(&global_barrier, NULL, num_workers);

	// Master thread
	ID = IDs[0] = 0; 

	// Bind master thread to CPU 0
	set_thread_affinity(0);
	printf("Worker %2d bound to CPU %2d\n", ID, get_thread_affinity());
	
	// Create num_workers-1 worker threads
	for (i = 1; i < num_workers; i++) {
		IDs[i] = i;
		pthread_create(&worker_threads[i], NULL, worker_entry_fn, &IDs[i]);
		// Bind worker threads to available CPUs in a round-robin fashion
		set_thread_affinity(worker_threads[i], i % num_cpus);
	}

	set_current_task((Task *)malloc(sizeof(Task)));
	current_task->parent = NULL;
	current_task->fn = NULL;
	current_task->created_by = -1;
	current_task->mpb_offset = -1;
	current_task->is_loop = false;
	current_task->start = 0;
	current_task->end = 0;

	num_tasks_exec_worker = 0;

	return 0;
}

int tasking_internal_exit_signal(void)
{
	atomic_set(tasking_finished, 1);

	return 0;
}

extern PRIVATE mytimer_t timer_idle, timer_handle, timer_decline, timer_steal, timer_async, timer_manage;
// To measure the impact of lock contention
extern PRIVATE mytimer_t timer_chan_mpsc, timer_chan_spsc, timer_lock;

int tasking_internal_statistics(void)
{
	MASTER {
		printf("\n");
		printf("/========================================\\\n");
		//printf("| Idle workers: %d\n", tasking_idle_workers());
		printf("| Tasks executed: %d\n", tasking_tasks_exec());
		printf("\\========================================/\n");
		//fflush(stdout);
	}

	tasking_internal_barrier();

	printf("Worker %d: %u tasks\n", ID, num_tasks_exec_worker);

#if 0
	printf("Worker %d: %u tasks, comp (ms): %.2lf, "
			"handle (ms): %.2lf, "
			"decline (ms): %.2lf, "
			"steal (ms): %.2lf, "
			"create (ms): %.2lf, "
			"idle (ms): %.2lf, "
			"manage (ms): %.2lf\n",
			ID, num_tasks_exec_worker,
			num_tasks_exec_worker * 1.0, /* assuming a task size of 1000us */
			timer_elapsed(&timer_handle, timer_ms),
			timer_elapsed(&timer_decline, timer_ms),
			timer_elapsed(&timer_steal, timer_ms),
			timer_elapsed(&timer_async, timer_ms),
			timer_elapsed(&timer_idle, timer_ms),
			timer_elapsed(&timer_manage, timer_ms));
#endif

#if 0
	printf("Worker %d: %u tasks, comp (ms): %.2lf, "
			"channel send/recv MPSC (ms): %.2lf, "
			"channel send/recv SPSC (ms): %.2lf, "
			"lock (ms): %.2lf\n",
			ID, num_tasks_exec_worker,
			num_tasks_exec_worker * 1.0, /* assuming a task size of 1000us */
			timer_elapsed(&timer_chan_mpsc, timer_ms),
			timer_elapsed(&timer_chan_spsc, timer_ms),
			timer_elapsed(&timer_lock, timer_ms));
#endif

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
