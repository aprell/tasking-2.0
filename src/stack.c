#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "stack.h"

struct stack {
	Task *top;
};

Stack *stack_new(void)
{
	Stack *stack = (Stack *)malloc(sizeof(Stack));
	if (!stack) {
		fprintf(stderr, "Warning: stack_new failed\n");
		return NULL;
	}

	stack->top = NULL;
	return stack;
}

void stack_delete(Stack *stack)
{
	Task *task;

	if (!stack)
		return;

	while ((task = stack_pop(stack)) != NULL) {
		// We assume all tasks are heap allocated
		free(task);
	}

	assert(stack_empty(stack));
	free(stack);
}

bool stack_empty(Stack *stack)
{
	assert(stack != NULL);

	return stack->top == NULL;
}

void stack_push(Stack *stack, Task *task)
{
	assert(stack != NULL);
	assert(task != NULL);

	task->next = stack->top;
	stack->top = task;
}

Task *stack_pop(Stack *stack)
{
	assert(stack != NULL);
	
	Task *task;

	if (stack_empty(stack))
		return NULL;

	task = stack->top;
	stack->top = stack->top->next;
	task->next = NULL;

	return task;
}

//==========================================================================//

#ifdef TEST_STACK

//==========================================================================//

#include "utest.h"

typedef struct {
	int a, b;
} Data;

UTEST()
{
	puts("Testing Stack");

	Stack *s;
	int i;
	
	s = stack_new();
	check_equal(stack_empty(s), true);
	check_equal(stack_pop(s), NULL);

	for (i = 0; i < 10; i++) {
		Task *t = task_new();
		check_not_equal(t, NULL);
		Data *d = (Data *)task_data(t);
		*d = (Data){ i, i+1 };
		stack_push(s, t);
	}

	check_equal(stack_empty(s), false);

	for (; i > 0; i--) {
		Task *t = stack_pop(s);
		Data *d = (Data *)task_data(t);
		check_equal(d->a, i-1);
		check_equal(d->b, i);
		task_delete(t);
	}

	check_equal(stack_empty(s), true);
	check_equal(stack_pop(s), NULL);
	stack_delete(s);

	puts("Done");
}

//==========================================================================//

#endif // TEST_STACK

//==========================================================================//
