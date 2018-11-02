#ifndef BOUNDED_STACK_H
#define BOUNDED_STACK_H

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef BOUNDED_STACK_ELEM_TYPE
#error "Define BOUNDED_STACK_ELEM_TYPE to some type"
#endif

typedef struct bounded_stack {
	unsigned int capacity;
	unsigned int top;
	BOUNDED_STACK_ELEM_TYPE buffer[];
} BoundedStack;

static inline BoundedStack *bounded_stack_alloc(unsigned int capacity)
{
	unsigned int buffer_size = capacity * sizeof(BOUNDED_STACK_ELEM_TYPE);
	BoundedStack *stack = (BoundedStack *)malloc(sizeof(BoundedStack) + buffer_size);
	if (!stack) {
		fprintf(stderr, "Warning: bounded_stack_alloc failed\n");
		return NULL;
	}

	stack->capacity = capacity;
	stack->top = 0;
	return stack;
}

static inline void bounded_stack_free(BoundedStack *stack)
{
	free(stack);
}

static inline bool bounded_stack_empty(BoundedStack *stack)
{
	assert(stack != NULL);

	return stack->top == 0;
}

static inline bool bounded_stack_full(BoundedStack *stack)
{
	assert(stack != NULL);

	return stack->top == stack->capacity;
}

static inline void bounded_stack_push(BoundedStack *stack, BOUNDED_STACK_ELEM_TYPE elem)
{
	assert(!bounded_stack_full(stack));

	stack->buffer[stack->top++] = elem;
}

static inline BOUNDED_STACK_ELEM_TYPE *bounded_stack_pop(BoundedStack *stack)
{
	assert(!bounded_stack_empty(stack));

	return &stack->buffer[--stack->top];
}

#endif // BOUNDED_STACK_H
