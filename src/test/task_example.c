#include <assert.h>
#include <stdio.h>
#include "tasking.h"
#include "async.h"

// DEFINE_ASYNC and DEFINE_FUTURE expand into data structures and wrapper
// functions for implementing tasks and futures

// Ignore return value of puts
DEFINE_ASYNC0(puts, (const char *));

int sum(int a, int b)
{
	return a + b;
}

DEFINE_FUTURE(int, sum, (int, int));

int main(int argc, char *argv[])
{
	// Initializes runtime system
	TASKING_INIT(&argc, &argv);

	ASYNC(puts, ("Hello World!"));

	// ASYNC expands into:
	//
	// do {
	//     Task *__task = task_alloc();
	//     struct puts_task_data __d;
	//     __task->parent = current_task();
	//     __task->fn = (void (*)(void *))puts_task_func;
	//     __d = (typeof(__d)){ "Hello World!" };
	//     memcpy(__task->data, &__d, sizeof(__d));
	//     rts_push(__task);
	// } while (0);

	// Inserts a task barrier
	TASKING_BARRIER();

	future f = FUTURE(sum, (1, 2));
	
	// FUTURE expands into:
	//
	// future f = ({
	//     Task *__task = task_alloc();
	//     struct sum_task_data __d;
	//     future __f = sum_channel();
	//     __task->parent = current_task();
	//     __task->fn = (void (*)(void *))sum_task_func;
	//     __d = (typeof(__d)){ __f, 1, 2 };
	//     memcpy(__task->data, &__d, sizeof(__d));
	//     rts_push(__task);
	//     __f;
	// });

	int n = AWAIT(f, int);
	
	// AWAIT expands into:
	//
	// int n = ({
	//     int __tmp;
	//     rts_force_future(f, &__tmp, sizeof(__tmp));
	//     future_free(f);
	//     __tmp;
	// });
	
	assert(n == 3);

	// Finalizes runtime system
	TASKING_EXIT();

	return 0;
}
