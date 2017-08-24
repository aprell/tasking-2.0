#ifndef TASK_H
#define TASK_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "list.h"

#define TASK_SUCCESS 0
#define TASK_ERROR_BASE 400
#define TASK_DATA_SIZE (128 - 3 * sizeof(long))
#define TASK_SIZE sizeof(Task)

typedef struct task Task;
typedef struct PRM_task_queue PRM_task_queue;

struct task {
	// Only used in taskwait
	// Shouldn't be interpreted if task was created by a different worker
	struct task *parent;
	struct {
		struct task *prev;
		struct task *next;
	};
	void (*fn)(void *);
	int batch, victim;
	long start;
	long cur;
	long end;
	long chunks;
	long sst;
	bool is_loop;
	// Task body carrying user data
	char data[TASK_DATA_SIZE];
};

static inline Task *task_zero(Task *task)
{
	task->parent = NULL;
	task->prev = NULL;
	task->next = NULL;
	task->fn = NULL;

	task->batch = 0;
	task->victim = 0;

	task->is_loop = false;
	task->start = 0;
	task->cur = 0;
	task->end = 0;
	task->chunks = 0;
	task->sst = 0;

	return task;
}

static inline Task *task_new(void)
{
	Task *task = (Task *)malloc(sizeof(Task));
	if (!task) {
		fprintf(stderr, "Warning: task_new failed\n");
		return NULL;
	}

	return task_zero(task);
}

static inline void task_delete(Task *task)
{
	free(task);
}

static inline char *task_data(Task *task)
{
	return task->data;
}

/*****************************************************************************
 * Private memory (PRM) task queue
 * Circular doubly-linked list
 * (Linux kernel implementation, adapted for user-space)
 *****************************************************************************/

typedef struct PRM_task_node PRM_task_node;

struct PRM_task_node {
	struct task task;
	struct list_head list;
};

struct PRM_task_queue {
	int type;
	unsigned int ntasks;
	struct list_head list;
};

enum {
	task_LIFO,
	task_FIFO
};

PRM_task_queue *PRM_task_queue_init(int type);
void PRM_task_queue_destroy(PRM_task_queue *tq);
void PRM_task_queue_add_task_node(PRM_task_queue *tq, PRM_task_node *task);
PRM_task_node *PRM_task_queue_remove_task_node(PRM_task_queue *tq);
PRM_task_node *PRM_task_queue_steal_task_node(PRM_task_queue *tq);
bool PRM_task_queue_is_empty(PRM_task_queue *tq);
unsigned int PRM_task_queue_num_tasks(PRM_task_queue *tq);

Task *PRM_task_new(void (*fn)(void *), void *data);
void PRM_task_delete(Task *task);
Task *PRM_task_reuse(Task *task, void (*fn)(void *), void *data);
Task *PRM_task_clear(Task *task);
void PRM_task_join(PRM_task_queue *tq);
void PRM_task_join_free_list(PRM_task_queue *tq, PRM_task_queue *fl);

#define PRM_task_dispatch(task, tq) \
	PRM_task_queue_add_task_node(tq, (PRM_task_node *)(task))

#define PRM_task_remove(tq) \
	(Task *)PRM_task_queue_remove_task_node(tq)

#define PRM_task_steal(tq) \
	(Task *)PRM_task_queue_steal_task_node(tq)

#endif // TASK_H
