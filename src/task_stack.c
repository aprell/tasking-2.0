#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "task_stack.h"

struct task_stack {
	Task *top;
};

TaskStack *task_stack_new(void)
{
	TaskStack *stack = (TaskStack *)malloc(sizeof(TaskStack));
	if (!stack) {
		fprintf(stderr, "Warning: task_stack_new failed\n");
		return NULL;
	}

	stack->top = NULL;
	return stack;
}

void task_stack_delete(TaskStack *stack)
{
	Task *task;

	if (!stack)
		return;

	while ((task = task_stack_pop(stack)) != NULL) {
		// We assume all tasks are heap allocated
		free(task);
	}

	assert(task_stack_empty(stack));
	free(stack);
}

bool task_stack_empty(TaskStack *stack)
{
	assert(stack != NULL);

	return stack->top == NULL;
}

void task_stack_push(TaskStack *stack, Task *task)
{
	assert(stack != NULL);
	assert(task != NULL);

	task->next = stack->top;
	stack->top = task;
}

Task *task_stack_pop(TaskStack *stack)
{
	assert(stack != NULL);
	
	Task *task;

	if (task_stack_empty(stack))
		return NULL;

	task = stack->top;
	stack->top = stack->top->next;
	task->next = NULL;

	return task;
}

//==========================================================================//

#ifdef TEST_TASK_STACK

//==========================================================================//

#include "utest.h"

typedef struct {
	int a, b;
} Data;

UTEST()
{
	puts("Testing TaskStack");

	TaskStack *s;
	int i;
	
	s = task_stack_new();
	check_equal(task_stack_empty(s), true);
	check_equal(task_stack_pop(s), NULL);

	for (i = 0; i < 10; i++) {
		Task *t = task_new();
		check_not_equal(t, NULL);
		Data *d = (Data *)task_data(t);
		*d = (Data){ i, i+1 };
		task_stack_push(s, t);
	}

	check_equal(task_stack_empty(s), false);

	for (; i > 0; i--) {
		Task *t = task_stack_pop(s);
		Data *d = (Data *)task_data(t);
		check_equal(d->a, i-1);
		check_equal(d->b, i);
		task_delete(t);
	}

	check_equal(task_stack_empty(s), true);
	check_equal(task_stack_pop(s), NULL);
	task_stack_delete(s);

	puts("Done");
}

//==========================================================================//

#endif // TEST_TASK_STACK

//==========================================================================//
