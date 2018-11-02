#ifndef BOUNDED_QUEUE_H
#define BOUNDED_QUEUE_H

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef BOUNDED_QUEUE_ELEM_TYPE
#error "Define BOUNDED_QUEUE_ELEM_TYPE to some type"
#endif

typedef struct bounded_queue {
	unsigned int capacity;
	unsigned int head, tail;
	BOUNDED_QUEUE_ELEM_TYPE buffer[];
} BoundedQueue;

static inline BoundedQueue *bounded_queue_alloc(unsigned int capacity)
{
	// One extra entry to distinguish full and empty queues
	unsigned int buffer_size = (capacity + 1) * sizeof(BOUNDED_QUEUE_ELEM_TYPE);
	BoundedQueue *queue = (BoundedQueue *)malloc(sizeof(BoundedQueue) + buffer_size);
	if (!queue) {
		fprintf(stderr, "Warning: bounded_queue_alloc failed\n");
		return NULL;
	}

	queue->capacity = capacity + 1;
	queue->head = 0;
	queue->tail = 0;
	return queue;
}

static inline void bounded_queue_free(BoundedQueue *queue)
{
	free(queue);
}

static inline bool bounded_queue_empty(BoundedQueue *queue)
{
	assert(queue != NULL);

	return queue->head == queue->tail;
}

static inline bool bounded_queue_full(BoundedQueue *queue)
{
	assert(queue != NULL);

	return (queue->tail + 1) % queue->capacity == queue->head;
}

static inline void bounded_queue_enqueue(BoundedQueue *queue, BOUNDED_QUEUE_ELEM_TYPE elem)
{
	assert(!bounded_queue_full(queue));

	queue->buffer[queue->tail] = elem;
	queue->tail = (queue->tail + 1) % queue->capacity;
}

static inline BOUNDED_QUEUE_ELEM_TYPE *bounded_queue_dequeue(BoundedQueue *queue)
{
	assert(!bounded_queue_empty(queue));

	BOUNDED_QUEUE_ELEM_TYPE *elem = &queue->buffer[queue->head];
	queue->head = (queue->head + 1) % queue->capacity;

	return elem;
}

static inline BOUNDED_QUEUE_ELEM_TYPE *bounded_queue_head(BoundedQueue *queue)
{
	assert(!bounded_queue_empty(queue));

	return &queue->buffer[queue->head];
}

#endif // BOUNDED_QUEUE_H
