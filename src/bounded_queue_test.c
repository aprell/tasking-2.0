// gcc -Wall -Wextra -fsanitize=address,undefined bounded_queue_test.c -o bounded_queue_test && ./bounded_queue_test

typedef struct integer { int n; } Integer;

#define BOUNDED_QUEUE_ELEM_TYPE Integer
#include "bounded_queue.h"

int main(void)
{
	BoundedQueue *q;
	Integer numbers[10];
	int i;

	q = bounded_queue_alloc(10);
	assert(bounded_queue_empty(q));

	for (i = 0; i < 10; i++) {
		numbers[i] = (Integer){ i };
		bounded_queue_enqueue(q, numbers[i]);
	}

	assert(bounded_queue_full(q));

	for (; i > 0; i--) {
		Integer *n = bounded_queue_dequeue(q);
		assert(*(int *)n == *(int *)&numbers[10-i]);
	}

	assert(bounded_queue_empty(q));
	bounded_queue_free(q);

	return 0;
}
