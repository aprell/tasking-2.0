// gcc -Wall -Wextra -fsanitize=address,undefined bounded_stack_test.c -o bounded_stack_test && ./bounded_stack_test

typedef struct integer { int n; } Integer;

#define BOUNDED_STACK_ELEM_TYPE Integer
#include "bounded_stack.h"

int main(void)
{
	BoundedStack *s;
	Integer numbers[10];
	int i;

	s = bounded_stack_alloc(10);
	assert(bounded_stack_empty(s));

	for (i = 0; i < 10; i++) {
		numbers[i] = (Integer){ i };
		bounded_stack_push(s, numbers[i]);
	}

	assert(bounded_stack_full(s));

	for (; i > 0; i--) {
		Integer *n = bounded_stack_pop(s);
		assert(*(int *)n == *(int *)&numbers[i-1]);
	}

	assert(bounded_stack_empty(s));
	bounded_stack_free(s);

	return 0;
}
