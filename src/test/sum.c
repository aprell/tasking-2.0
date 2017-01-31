#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "tasking.h"
#include "async.h"
#include "chanref.h"

#define N 10

void sum(int a, int b)
{
	printf("%2d: %2d + %2d = %2d\n", ID, a, b, a+b);
}

ASYNC_DECL(sum, int a; int b, a, b);

void sum_loop(void *v __attribute__((unused)))
{
	long i;

	for_each_task (i) {
		printf("%2d: %2ld + %2ld = %2ld\n", ID, i, i+1, i+i+1);
		RT_loop_split();
	}
}

ASYNC_DECL(sum_loop, void *v, v);

int main(int argc, char *argv[])
{
	TASKING_INIT(&argc, &argv);

	ASYNC_FOR(sum_loop, 0, N, NULL);

	TASKING_BARRIER();
	//assert(tasking_tasks_exec() == N);

	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
