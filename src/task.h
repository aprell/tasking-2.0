#ifndef TASK_H
#define TASK_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define TASK_SUCCESS 0
#define TASK_ERROR_BASE 400
#define TASK_DATA_SIZE (192 - 96)
#define TASK_SIZE sizeof(Task)

typedef struct task Task;

struct task {
	// Required to pop child tasks:
	// if (child->parent == this) ...
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
	bool has_future;
	// List of futures required by the current task
	void *futures;
	// Task body carrying user data
	char data[TASK_DATA_SIZE] __attribute__((aligned(8)));
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

	task->has_future = false;
	task->futures = NULL;

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

#endif // TASK_H
