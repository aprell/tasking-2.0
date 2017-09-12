#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "tasking.h"
#include "async.h"

#define N 10

void sum(int a, int b)
{
	printf("%2d: %2d + %2d = %2d\n", ID, a, b, a+b);
}

DEFINE_ASYNC(sum, (int, int));

void sum_loop(void)
{
	long i;

	ASYNC_FOR (i) {
		printf("%2d: %2ld + %2ld = %2ld\n", ID, i, i+1, i+i+1);
	}
}

DEFINE_ASYNC0(sum_loop, ());

int main(int argc, char *argv[])
{
	TASKING_INIT(&argc, &argv);

	ASYNC0(sum_loop, (0, N), ());

	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
