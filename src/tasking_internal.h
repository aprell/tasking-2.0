#ifndef TASKING_INTERNAL_H
#define TASKING_INTERNAL_H

#include <stdbool.h>
#ifdef DISABLE_MANAGER
#include <assert.h>
#endif
#include "platform.h"
#include "atomic.h"
#include "task.h"

#define MASTER_ID 0
#define MASTER if (ID == MASTER_ID)
#define WORKER if (ID != MASTER_ID)

// Shared state
#ifdef DISABLE_MANAGER
extern atomic_t *td_count;
#endif

extern int num_workers;

// Private state
extern PRIVATE int ID;
extern PRIVATE int num_tasks_exec_worker;
extern PRIVATE int num_tasks_exec_recently;
extern PRIVATE int worker_state; // unused unless manager is disabled
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

// Workers are either working or idle
enum worker_state {
	WORKING,
	IDLE
};

static inline int get_worker_state(void)
{
	return worker_state;
}

#ifdef DISABLE_MANAGER

static inline void set_worker_state(int state)
{
	assert(worker_state != state);

	switch (worker_state = state) {
	case WORKING:
		atomic_dec(td_count);
		break;
	case IDLE:
		atomic_inc(td_count);
		break;
	default:
		assert(false && "Invalid worker state");
		break;
	}
}

#else

static inline void set_worker_state(int state)
{
	worker_state = state;
}

#endif

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
		num_tasks_exec_worker += n;
#ifdef STEAL_ADAPTIVE
		num_tasks_exec_recently += n;
#endif
	} else {
		num_tasks_exec_worker++;
#ifdef STEAL_ADAPTIVE
		num_tasks_exec_recently++;
#endif
	}
}

int tasking_internal_init(int *, char ***);
int tasking_internal_exit_signal(void);
int tasking_internal_exit(void);
int tasking_internal_barrier(void);
int tasking_internal_statistics(void);
#ifdef DISABLE_MANAGER
bool tasking_all_idle(void);
#endif
bool tasking_done(void);

#endif // TASKING_INTERNAL_H
