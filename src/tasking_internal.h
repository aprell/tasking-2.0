#ifndef TASKING_INTERNAL_H
#define TASKING_INTERNAL_H

#include <stdbool.h>
#include "platform.h"
#include "atomic.h"
#include "task.h"

#define MASTER_ID 0
#define MASTER if (ID == MASTER_ID)
#define WORKER if (ID != MASTER_ID)

// Shared state
extern atomic_t *tasking_finished;
extern atomic_t *num_tasks_exec;
extern int num_workers;

// Private state
extern PRIVATE int ID;
extern PRIVATE int num_tasks_exec_worker;
extern PRIVATE int worker_state; // currently unused

// Pointer to the task that is currently running
extern PRIVATE Task *current_task;

static inline void set_current_task(Task *task)
{
	current_task = task;
}

static inline Task *get_current_task(void)
{
	return current_task;
}

static inline bool is_root_task(Task *task)
{
	return task->parent == NULL ? true : false;
}

// Workers are either working or idle
enum worker_state {
	WORKING,
	IDLE
};

static inline int get_worker_state(void)
{
	return worker_state;
}

static inline void set_worker_state(int state)
{
	worker_state = state;
}

#define is_idle() 		(get_worker_state() == IDLE ? 1 : 0)
#define is_working()	(get_worker_state() == WORKING ? 1 : 0)
#define set_idle()		(set_worker_state(IDLE))
#define set_working() 	(set_worker_state(WORKING))

// Wrapper function for running a new task
static inline void run_task(Task *task)
{
	//if (task->is_loop)
	//	fprintf(stderr, "%2d: Running [%2ld,%2ld)\n", ID, task->start, task->end);

	Task *this_ = get_current_task();
	set_current_task(task);
	task->fn(task->data);
	set_current_task(this_);
	if (task->is_loop) {
		// We have executed |end-start| iterations
		int n = abs(task->end - task->start);
		//atomic_add(n, num_tasks_exec);
		num_tasks_exec_worker += n;
	} else {
		//atomic_inc(num_tasks_exec);
		num_tasks_exec_worker++;
	}
}

int tasking_internal_init(int *, char ***);
int tasking_internal_exit_signal(void);
int tasking_internal_exit(void);
int tasking_internal_barrier(void);
int tasking_internal_statistics(void);
int tasking_tasks_exec(void);
bool tasking_done(void);

#endif // TASKING_INTERNAL_H
