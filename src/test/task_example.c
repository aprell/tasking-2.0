#include <assert.h>
#include <stdio.h>
#include "tasking.h"
#include "async.h"

// ASYNC_DECL and FUTURE_DECL expand into data structures and wrapper functions
// for implementing tasks and futures

ASYNC_DECL (
	// Ignore return value of puts
	puts, const char *s, s
);

int sum(int a, int b)
{
	return a + b;
}

FUTURE_DECL (
	int, sum, int a; int b, a, b
);

int main(int argc, char *argv[])
{
	// Initializes runtime system
	TASKING_INIT(&argc, &argv);

	ASYNC(puts, "Hello World!");

	// ASYNC expands into:
	//
	// do {
	//     Task *__task = task_alloc();
	//     struct puts_task_data *__d;
	//     __task->parent = current_task();
	//     __task->fn = (void (*)(void *))puts_task_fn;
	//     __d = (struct puts_task_data *)__task->data;
	//     *(__d) = (typeof(*(__d))){ "Hello World!" };
	//     rts_push(__task);
	// } while (0);

	// Inserts a task barrier
	TASKING_BARRIER();

	future f = __ASYNC(sum, 1, 2);
	
	// FUTURE expands into:
	//
	// future f = ({
	//     Task *__task = task_alloc();
	//     struct puts_task_data *__d;
	//     future __f = sum_future_alloc();
	//     __task->parent = current_task();
	//     __task->fn = (void (*)(void *))sum_task_fn;
	//     __d = (struct puts_task_data *)__task->data;
	//     *(__d) = (typeof(*(__d))){ 1, 2, __f };
	//     rts_push(__task);
	//     __f;
	// });

	int n = __AWAIT(f, int);
	
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
