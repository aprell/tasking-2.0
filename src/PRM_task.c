#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "list.h"
#include "task.h"

#define INIT_TASK_QUEUE(tq, type) { \
	(tq)->type = type; \
	(tq)->ntasks = 0; \
	INIT_LIST_HEAD(&(tq)->list); }

#define INIT_TASK_NODE(node, fn, data) { \
	INIT_TASK(&(node)->task, fn, data); \
	INIT_LIST_HEAD(&(node)->list); }

#define INIT_TASK(task, fn, data) { \
	(task)->fn = fn; \
	/*(task)->data = data;*/ \
	if (data && data != (task)->data) { \
		/*printf("Copying task data\n");*/ \
		memcpy((task)->data, data, TASK_DATA_SIZE); \
	} }

static inline void task_add(PRM_task_node *task, PRM_task_queue *head)
{
	list_add(&task->list, &head->list); 
}

static inline void task_add_tail(PRM_task_node *task, PRM_task_queue *head)
{
	list_add_tail(&task->list, &head->list);
}

static void (*task_add_fn[2])(PRM_task_node *, PRM_task_queue *) = 
{
	&task_add, &task_add_tail 
};

PRM_task_queue *PRM_task_queue_init(int type)
{
	assert(type == task_LIFO || type == task_FIFO);

	PRM_task_queue *head = (PRM_task_queue *)malloc(sizeof(PRM_task_queue));
	if (!head)
		return NULL;

	INIT_TASK_QUEUE(head, type);	

	return head;
}

void PRM_task_queue_destroy(PRM_task_queue *tq)
{
	assert(tq != NULL);

	struct list_head *p, *q;

	// Free all remaining nodes
	list_for_each_safe(p, q, &tq->list) {
		PRM_task_node *n = list_entry(p, PRM_task_node, list);
		list_del(&n->list);
		//free(n->task.data);
		free(n);
	}

	assert(list_empty(&tq->list));
	free(tq);
}

void PRM_task_queue_add_task_node(PRM_task_queue *tq, PRM_task_node *task)
{
	assert(tq != NULL);
	assert(task != NULL);

	(*task_add_fn[tq->type])(task, tq);
	tq->ntasks++;
}

PRM_task_node *PRM_task_queue_remove_task_node(PRM_task_queue *tq)
{
	assert(tq != NULL);

	PRM_task_node *task;

	if (list_empty(&tq->list))
		return NULL;

	task = list_first_entry(&tq->list, PRM_task_node, list);
	list_del(&task->list);
	tq->ntasks--;

	return task;
}

PRM_task_node *PRM_task_queue_steal_task_node(PRM_task_queue *tq)
{
	assert(tq != NULL);

	PRM_task_node *task;

	if (list_empty(&tq->list))
		return NULL;

	task = list_last_entry(&tq->list, PRM_task_node, list);
	list_del(&task->list);
	tq->ntasks--;

	return task;
}

bool PRM_task_queue_is_empty(PRM_task_queue *tq)
{
	assert(tq != NULL);

	return list_empty(&tq->list);
}

unsigned int PRM_task_queue_num_tasks(PRM_task_queue *tq)
{
	return tq->ntasks;
}

Task *PRM_task_new(void (*fn)(void *), void *data)
{
	PRM_task_node *task = (PRM_task_node *)malloc(sizeof(PRM_task_node));
	if (!task)
		return NULL;

	INIT_TASK_NODE(task, fn, data);

	return (Task *)task;
}

void PRM_task_delete(Task *task)
{
	PRM_task_node *node = (PRM_task_node *)task;
	//free(node->task.data);
	free(node);
}

Task *PRM_task_reuse(Task *task, void (*fn)(void *), void *data)
{
	assert(task != NULL);

	PRM_task_node *node = (PRM_task_node *)task;
	
	INIT_TASK_NODE(node, fn, data);

	return (Task *)node;
}

Task *PRM_task_clear(Task *task)
{
	assert(task != NULL);

	//free(task->data);
	//memset(task->data, 0, TASK_DATA_SIZE);

	return PRM_task_reuse(task, NULL, NULL);
}

void PRM_task_join(PRM_task_queue *tq)
{
	assert(tq != NULL);

	Task *task;

	while ((task = PRM_task_remove(tq)) != NULL) {
		(*task->fn)(task->data);
		PRM_task_delete(task);
	}
}

void PRM_task_join_free_list(PRM_task_queue *tq, PRM_task_queue *fl)
{
	assert(tq != NULL);

	Task *task;

	while ((task = PRM_task_remove(tq)) != NULL) {
		(*task->fn)(task->data);
		PRM_task_dispatch(PRM_task_clear(task), fl);
	}
}
