#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include "deque_list_tl.h"
#include "task_stack.h"

struct deque_list_tl {
	// List must be accessible from either end
	Task *head, *tail;
	// Number of tasks in the deque
	unsigned int num_tasks;
	// Record number of (successful) steals
	unsigned int num_steals;
	// Pool of free task objects
	TaskStack *freelist;
};

DequeListTL *deque_list_tl_new(void)
{
	DequeListTL *dq;

	dq = (DequeListTL *)malloc(sizeof(DequeListTL));
	if (!dq) {
		fprintf(stderr, "Warning: deque_list_tl_new failed\n");
		return NULL;
	}

	Task *dummy = task_new();
	dummy->fn = (void *)0xCAFE;
	dq->head = dummy;
	dq->tail = dummy;
	dq->num_tasks = 0;
	dq->num_steals = 0;
	dq->freelist = task_stack_new();

	return dq;
}

void deque_list_tl_delete(DequeListTL *dq)
{
	if (dq != NULL) {
		Task *task;
		// Free all remaining tasks
		while ((task = deque_list_tl_pop(dq)) != NULL) {
			task_delete(task);
		}
		assert(deque_list_tl_num_tasks(dq) == 0);
		assert(deque_list_tl_empty(dq));
		// Free dummy node
		task_delete(dq->head);
		// Free allocations that are still cached
		task_stack_delete(dq->freelist);
		free(dq);
	}
}

Task *deque_list_tl_task_new(DequeListTL *dq)
{
	assert(dq != NULL);

	if (task_stack_empty(dq->freelist))
		return task_new();

	return task_stack_pop(dq->freelist);
}

void deque_list_tl_task_cache(DequeListTL *dq, Task *task)
{
	assert(dq != NULL);
	assert(task != NULL);

	task_stack_push(dq->freelist, task_zero(task));
}

// Add list of tasks [head, tail] of length len to the front of dq
DequeListTL *deque_list_tl_prepend(DequeListTL *dq, Task *head, Task *tail,
		                          unsigned int len)
{
	assert(dq != NULL);
	assert(head != NULL && tail != NULL);
	assert(len > 0);

	// Link tail with dq->head
	assert(tail->next == NULL);
	tail->next = dq->head;
	dq->head->prev = tail;

	// Update state of deque
	dq->head = head;
	dq->num_tasks += len;

	return dq;
}

// Add list of tasks starting with head of length len to the front of dq
DequeListTL *deque_list_tl_prepend(DequeListTL *dq, Task *head, unsigned int len)
{
	assert(dq != NULL);
	assert(head != NULL);
	assert(len > 0);

	Task *tail = head;

	// Find the tail
	while (tail->next != NULL) {
		tail = tail->next;
	}

	// Link tail with dq->head
	assert(tail->next == NULL);
	tail->next = dq->head;
	dq->head->prev = tail;

	// Update state of deque
	dq->head = head;
	dq->num_tasks += len;

	return dq;
}

void deque_list_tl_push(DequeListTL *dq, Task *task)
{
	assert(dq != NULL);
	assert(task != NULL);

	task->next = dq->head;
	dq->head->prev = task;
	dq->head = task;

	dq->num_tasks++;
}

Task *deque_list_tl_pop(DequeListTL *dq)
{
	assert(dq != NULL);

	Task *task;

	if (deque_list_tl_empty(dq))
		return NULL;

	task = dq->head;
	dq->head = dq->head->next;
	dq->head->prev = NULL;
	task->next = NULL;

	dq->num_tasks--;

	return task;
}

Task *deque_list_tl_pop_child(DequeListTL *dq, Task *parent)
{
	assert(dq != NULL);
	assert(parent != NULL);

	Task *task;

	if (deque_list_tl_empty(dq))
		return NULL;

	task = dq->head;
	if (task->parent != parent) {
		// Not a child of parent, don't pop it
		return NULL;
	}
	dq->head = dq->head->next;
	dq->head->prev = NULL;
	task->next = NULL;

	dq->num_tasks--;

	return task;
}

Task *deque_list_tl_steal(DequeListTL *dq)
{
	assert(dq != NULL);

	Task *task;

	if (deque_list_tl_empty(dq))
		return NULL;

	task = dq->tail;
	assert(task->fn == (void *)0xCAFE);
	task = task->prev;
	task->next = NULL;
	dq->tail->prev = task->prev;
	task->prev = NULL;
	if (dq->tail->prev == NULL) {
		// Stealing the last task in the deque
		assert(dq->head == task);
		dq->head = dq->tail;
	} else {
		dq->tail->prev->next = dq->tail;
	}

	dq->num_tasks--;
	dq->num_steals++;

	return task;
}

// Steal up to half of the deque's tasks, but at most max tasks
// tail will point to the last task in the returned list (head is returned)
// stolen will contain the number of transferred tasks
Task *deque_list_tl_steal_many(DequeListTL *dq, Task **tail, int max, int *stolen)
{
	assert(dq != NULL);
	assert(stolen != NULL);

	Task *task;
	int n, i;

	if (deque_list_tl_empty(dq))
		return NULL;

	// Make sure to steal at least one task
	n = dq->num_tasks / 2;
	if (n == 0) n = 1;
	if (n > max) n = max;

	task = dq->tail;
	*tail = task->prev;
	assert(task->fn == (void *)0xCAFE);

	// Walk backwards
	for (i = 0; i < n; i++) {
		task = task->prev;
	}

	dq->tail->prev->next = NULL;
	dq->tail->prev = task->prev;
	task->prev = NULL;
	if (dq->tail->prev == NULL) {
		// Stealing the last task in the deque
		assert(dq->head == task);
		dq->head = dq->tail;
	} else {
		dq->tail->prev->next = dq->tail;
	}

	dq->num_tasks -= n;
	dq->num_steals++;
	*stolen = n;

	return task;
}

// Steal up to half of the deque's tasks, but at most max tasks
// stolen will contain the number of transferred tasks
Task *deque_list_tl_steal_many(DequeListTL *dq, int max, int *stolen)
{
	assert(dq != NULL);
	assert(stolen != NULL);

	Task *task;
	int n, i;

	if (deque_list_tl_empty(dq))
		return NULL;

	// Make sure to steal at least one task
	n = dq->num_tasks / 2;
	if (n == 0) n = 1;
	if (n > max) n = max;

	task = dq->tail;
	assert(task->fn == (void *)0xCAFE);

	// Walk backwards
	for (i = 0; i < n; i++) {
		task = task->prev;
	}

	dq->tail->prev->next = NULL;
	dq->tail->prev = task->prev;
	task->prev = NULL;
	if (dq->tail->prev == NULL) {
		// Stealing the last task in the deque
		assert(dq->head == task);
		dq->head = dq->tail;
	} else {
		dq->tail->prev->next = dq->tail;
	}

	dq->num_tasks -= n;
	dq->num_steals++;
	*stolen = n;

	return task;
}

// Steal half of the deque's tasks
// tail will point to the last task in the returned list (head is returned)
// stolen will contain the number of transferred tasks
Task *deque_list_tl_steal_half(DequeListTL *dq, Task **tail, int *stolen)
{
	assert(dq != NULL);
	assert(stolen != NULL);

	Task *task;
	int n, i;

	if (deque_list_tl_empty(dq))
		return NULL;

	// Make sure to steal at least one task
	n = dq->num_tasks / 2;
	if (n == 0) n = 1;

	task = dq->tail;
	*tail = task->prev;
	assert(task->fn == (void *)0xCAFE);

	// Walk backwards
	for (i = 0; i < n; i++) {
		task = task->prev;
	}

	dq->tail->prev->next = NULL;
	dq->tail->prev = task->prev;
	task->prev = NULL;
	if (dq->tail->prev == NULL) {
		// Stealing the last task in the deque
		assert(dq->head == task);
		dq->head = dq->tail;
	} else {
		dq->tail->prev->next = dq->tail;
	}

	dq->num_tasks -= n;
	dq->num_steals++;
	*stolen = n;

	return task;
}

// Steal half of the deque's tasks
// stolen will contain the number of transferred tasks
Task *deque_list_tl_steal_half(DequeListTL *dq, int *stolen)
{
	assert(dq != NULL);

	Task *task;
	int n, i;

	if (deque_list_tl_empty(dq))
		return NULL;

	// Make sure to steal at least one task
	n = dq->num_tasks / 2;
	if (n == 0) n = 1;

	task = dq->tail;
	assert(task->fn == (void *)0xCAFE);

	// Walk backwards
	for (i = 0; i < n; i++) {
		task = task->prev;
	}

	dq->tail->prev->next = NULL;
	dq->tail->prev = task->prev;
	task->prev = NULL;
	if (dq->tail->prev == NULL) {
		// Stealing the last task in the deque
		assert(dq->head == task);
		dq->head = dq->tail;
	} else {
		dq->tail->prev->next = dq->tail;
	}

	dq->num_tasks -= n;
	dq->num_steals++;
	*stolen = n;

	return task;
}

bool deque_list_tl_empty(DequeListTL *dq)
{
	assert(dq != NULL);

	return dq->head == dq->tail && dq->num_tasks == 0;
}

unsigned int deque_list_tl_num_tasks(DequeListTL *dq)
{
	assert(dq != NULL);

	return dq->num_tasks;
}

//==========================================================================//

#ifdef TEST_DEQUE_LIST_TL

//==========================================================================//

#include "utest.h"

#define N 1000000 	// Number of tasks to push/pop/steal
#define M 100		// Max. number of tasks to steal in one swoop

typedef struct {
	int a, b;
} Data;

UTEST()
{
	puts("Testing DequeListTL");

	DequeListTL *deq;
	int i, m;

	deq = deque_list_tl_new();

	check_equal(deque_list_tl_empty(deq), true);
	check_equal(deque_list_tl_num_tasks(deq), 0);

	for (i = 0; i < N; i++) {
		Task *t = deque_list_tl_task_new(deq);
		check_not_equal(t, NULL);
		Data *d = (Data *)task_data(t);
		*d = (Data){ i, i+1 };
		deque_list_tl_push(deq, t);
	}

	check_equal(deque_list_tl_empty(deq), false);
	check_equal(deque_list_tl_num_tasks(deq), N);

	for (; i > 0; i--) {
		Task *t = deque_list_tl_pop(deq);
		Data *d = (Data *)task_data(t);
		check_equal(d->a, i-1);
		check_equal(d->b, i);
		deque_list_tl_task_cache(deq, t);
	}

	check_equal(deque_list_tl_pop(deq), NULL);
	check_equal(deque_list_tl_empty(deq), true);
	check_equal(deque_list_tl_num_tasks(deq), 0);

	for (i = 0; i < N; i++) {
		Task *t = deque_list_tl_task_new(deq);
		check_not_equal(t, NULL);
		Data *d = (Data *)task_data(t);
		*d = (Data){ i+24, i+42 };
		deque_list_tl_push(deq, t);
	}

	check_equal(task_stack_empty(deq->freelist), true);
	check_equal(deque_list_tl_empty(deq), false);
	check_equal(deque_list_tl_num_tasks(deq), N);

	for (i = 0; i < N; i += m) {
		DequeListTL *s;
		Task *h, *t;
		int a, b, j;

		h = deque_list_tl_steal_many(deq, &t, M, &m);
		check_not_equal(h, NULL);
		check_equal(m >= 1 && m <= M, true);

		s = deque_list_tl_prepend(deque_list_tl_new(), h, t, m);
		check_not_equal(s, NULL);
		check_equal(deque_list_tl_empty(s), false);
		check_equal(deque_list_tl_num_tasks(s), m);

		t = deque_list_tl_pop(s);
		Data *d = (Data *)task_data(t);
		deque_list_tl_task_cache(s, t);
		a = d->a;
		b = d->b;

		for (j = 1; j < m; j++) {
			t = deque_list_tl_pop(s);
			d = (Data *)task_data(t);
			check_equal(d->a, a-j);
			check_equal(d->b, b-j);
			deque_list_tl_task_cache(s, t);
		}

		check_equal(deque_list_tl_pop(s), NULL);
		check_equal(deque_list_tl_steal(s), NULL);
		check_equal(deque_list_tl_empty(s), true);
		check_equal(deque_list_tl_num_tasks(s), 0);
		deque_list_tl_delete(s);
	}

	check_equal(deque_list_tl_steal(deq), NULL);
	check_equal(deque_list_tl_empty(deq), true);
	check_equal(deque_list_tl_num_tasks(deq), 0);

	deque_list_tl_delete(deq);

	puts("Done");
}

//==========================================================================//

#endif // TEST_DEQUE_LIST_TL

//==========================================================================//
