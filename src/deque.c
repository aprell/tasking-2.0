// gcc -Wall -Wextra -Wno-sign-compare -fsanitize=address,undefined -DTEST deque.c -o deque && ./deque
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include "deque.h"

struct deque {
	// List must be accessible from either end
	Task *head, *tail;
	// Number of tasks in the deque
	unsigned int num_tasks;
	// Record number of (successful) steals
	unsigned int num_steals;
	// Pool (stack) of free task objects
	Task *freelist;
};

static inline void freelist_push(Deque *dq, Task *task)
{
	task->next = dq->freelist;
	dq->freelist = task;
}

static inline Task *freelist_pop(Deque *dq)
{
	if (!dq->freelist)
		return NULL;

	Task *task = dq->freelist;
	dq->freelist = dq->freelist->next;
	task->next = NULL;

	return task;
}

Deque *deque_new(void)
{
	Deque *dq;

	dq = (Deque *)malloc(sizeof(Deque));
	if (!dq) {
		fprintf(stderr, "Warning: deque_new failed\n");
		return NULL;
	}

	Task *dummy = task_new();
	dummy->fn = (void *)0xCAFE;
	dq->head = dummy;
	dq->tail = dummy;
	dq->num_tasks = 0;
	dq->num_steals = 0;
	dq->freelist = NULL;

	return dq;
}

void deque_delete(Deque *dq)
{
	if (dq != NULL) {
		Task *task;
		// Free all remaining tasks
		while ((task = deque_pop(dq)) != NULL) {
			task_delete(task);
		}
		assert(deque_num_tasks(dq) == 0);
		assert(deque_empty(dq));
		// Free dummy node
		task_delete(dq->head);
		// Free allocations that are still cached
		while ((task = freelist_pop(dq)) != NULL) {
			free(task);
		}
		free(dq);
	}
}

Task *deque_task_new(Deque *dq)
{
	assert(dq != NULL);

	if (!dq->freelist)
		return task_new();

	return freelist_pop(dq);
}

void deque_task_cache(Deque *dq, Task *task)
{
	assert(dq != NULL);
	assert(task != NULL);

	freelist_push(dq, task_zero(task));
}

// Add list of tasks [head, tail] of length len to the front of dq
Deque *deque_prepend(Deque *dq, Task *head, Task *tail, unsigned int len)
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
Deque *deque_prepend(Deque *dq, Task *head, unsigned int len)
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

void deque_push(Deque *dq, Task *task)
{
	assert(dq != NULL);
	assert(task != NULL);

	task->next = dq->head;
	dq->head->prev = task;
	dq->head = task;

	dq->num_tasks++;
}

Task *deque_pop(Deque *dq)
{
	assert(dq != NULL);

	Task *task;

	if (deque_empty(dq))
		return NULL;

	task = dq->head;
	dq->head = dq->head->next;
	dq->head->prev = NULL;
	task->next = NULL;

	dq->num_tasks--;

	return task;
}

Task *deque_pop_child(Deque *dq, Task *parent)
{
	assert(dq != NULL);
	assert(parent != NULL);

	Task *task;

	if (deque_empty(dq))
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

Task *deque_steal(Deque *dq)
{
	assert(dq != NULL);

	Task *task;

	if (deque_empty(dq))
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
Task *deque_steal_many(Deque *dq, Task **tail, int max, int *stolen)
{
	assert(dq != NULL);
	assert(stolen != NULL);

	Task *task;
	int n, i;

	if (deque_empty(dq))
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
Task *deque_steal_many(Deque *dq, int max, int *stolen)
{
	assert(dq != NULL);
	assert(stolen != NULL);

	Task *task;
	int n, i;

	if (deque_empty(dq))
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
Task *deque_steal_half(Deque *dq, Task **tail, int *stolen)
{
	assert(dq != NULL);
	assert(stolen != NULL);

	Task *task;
	int n, i;

	if (deque_empty(dq))
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
Task *deque_steal_half(Deque *dq, int *stolen)
{
	assert(dq != NULL);

	Task *task;
	int n, i;

	if (deque_empty(dq))
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

bool deque_empty(Deque *dq)
{
	assert(dq != NULL);

	return dq->head == dq->tail && dq->num_tasks == 0;
}

unsigned int deque_num_tasks(Deque *dq)
{
	assert(dq != NULL);

	return dq->num_tasks;
}

//==========================================================================//

#ifdef TEST

//==========================================================================//

#include "utest.h"

#define N 1000000 	// Number of tasks to push/pop/steal
#define M 100		// Max. number of tasks to steal in one swoop

typedef struct {
	int a, b;
} Data;

int main(void)
{
	UTEST_INIT;

	Deque *deq;
	int i, m;

	deq = deque_new();

	check_equal(deque_empty(deq), true);
	check_equal(deque_num_tasks(deq), 0);

	for (i = 0; i < N; i++) {
		Task *t = deque_task_new(deq);
		check_not_equal(t, NULL);
		Data *d = (Data *)task_data(t);
		*d = (Data){ i, i+1 };
		deque_push(deq, t);
	}

	check_equal(deque_empty(deq), false);
	check_equal(deque_num_tasks(deq), N);

	for (; i > 0; i--) {
		Task *t = deque_pop(deq);
		Data *d = (Data *)task_data(t);
		check_equal(d->a, i-1);
		check_equal(d->b, i);
		deque_task_cache(deq, t);
	}

	check_equal(deque_pop(deq), NULL);
	check_equal(deque_empty(deq), true);
	check_equal(deque_num_tasks(deq), 0);

	for (i = 0; i < N; i++) {
		Task *t = deque_task_new(deq);
		check_not_equal(t, NULL);
		Data *d = (Data *)task_data(t);
		*d = (Data){ i+24, i+42 };
		deque_push(deq, t);
	}

	check_equal(deq->freelist, NULL);
	check_equal(deque_empty(deq), false);
	check_equal(deque_num_tasks(deq), N);

	for (i = 0; i < N; i += m) {
		Deque *s;
		Task *h, *t;
		int a, b, j;

		h = deque_steal_many(deq, &t, M, &m);
		check_not_equal(h, NULL);
		check_equal(m >= 1 && m <= M, true);

		s = deque_prepend(deque_new(), h, t, m);
		check_not_equal(s, NULL);
		check_equal(deque_empty(s), false);
		check_equal(deque_num_tasks(s), m);

		t = deque_pop(s);
		Data *d = (Data *)task_data(t);
		deque_task_cache(s, t);
		a = d->a;
		b = d->b;

		for (j = 1; j < m; j++) {
			t = deque_pop(s);
			d = (Data *)task_data(t);
			check_equal(d->a, a-j);
			check_equal(d->b, b-j);
			deque_task_cache(s, t);
		}

		check_equal(deque_pop(s), NULL);
		check_equal(deque_steal(s), NULL);
		check_equal(deque_empty(s), true);
		check_equal(deque_num_tasks(s), 0);
		deque_delete(s);
	}

	check_equal(deque_steal(deq), NULL);
	check_equal(deque_empty(deq), true);
	check_equal(deque_num_tasks(deq), 0);

	deque_delete(deq);

	UTEST_DONE;

	return 0;
}

//==========================================================================//

#endif // TEST

//==========================================================================//
