#ifndef TASKING_INTERNAL_H
#define TASKING_INTERNAL_H

#include <stdbool.h>
#include "atomic.h"
#include "platform.h"
#include "task.h"
#ifdef USE_COZ
#include "coz.h"
#endif

#define MASTER_ID 0
#define MASTER if (ID == MASTER_ID)
#define WORKER if (ID != MASTER_ID)

// Shared state
extern int num_workers;

// Private state
extern PRIVATE int ID;
extern PRIVATE int num_tasks_exec;
#if STEAL == adaptive
extern PRIVATE int num_tasks_exec_recently;
#endif
extern PRIVATE bool tasking_finished;

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

// Wrapper function for running a new task
static inline void run_task(Task *task)
{
	//if (task->splittable)
	//	fprintf(stderr, "%2d: Running [%2ld,%2ld)\n", ID, task->start, task->end);

	Task *this_ = get_current_task();
	set_current_task(task);
	task->fn(task->data);
	set_current_task(this_);
	if (task->splittable) {
		// We have executed |end-start| iterations
		int n = abs(task->end - task->start);
		num_tasks_exec += n;
#if STEAL == adaptive
		num_tasks_exec_recently += n;
#endif
	} else {
		num_tasks_exec++;
#if STEAL == adaptive
		num_tasks_exec_recently++;
#endif
	}

#ifdef USE_COZ
	COZ_PROGRESS_NAMED("task executed");
#endif
}

#endif // TASKING_INTERNAL_H
