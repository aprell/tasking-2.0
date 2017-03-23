#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "tasking.h"
#include "async.h"

int sum(int a, int b)
{
	printf("Hello from thread %d!\n", ID);
	return a + b;
}

FUTURE_DECL_FREELIST(int);
FUTURE_DECL(int, sum, int a; int b, a, b);
TUPLE_DECL(int);

int main(int argc, char *argv[])
{
	TASKING_INIT(&argc, &argv);

	//========================================================================

	future f1, f2, f3;

	f1 = __ASYNC(sum, 0, 1);
	f2 = __ASYNC(sum, 1, 2);
	f3 = __ASYNC(sum, 2, 3);

	assert(__AWAIT(f1, int) == 1);
	assert(__AWAIT(f2, int) == 3);
	assert(__AWAIT(f3, int) == 5);

	//========================================================================

	f1 = __ASYNC(sum, 0, 1);
	f2 = __ASYNC(sum, 1, 2);
	f3 = __ASYNC(sum, 2, 3);

	// Requires TUPLE_DECL(int)
	struct tuple_3_int sums = __AWAIT(f1, f2, f3, int);

	assert(sums._0 == 1);
	assert(sums._1 == 3);
	assert(sums._2 == 5);

	//========================================================================

	sums = __AWAIT (
		__ASYNC(sum, 4, 5),
		__ASYNC(sum, 6, 7),
		__ASYNC(sum, 8, 9),
		int
	);

	assert(sums._0 ==  9);
	assert(sums._1 == 13);
	assert(sums._2 == 17);

	//========================================================================

	// This should be moved inside TASKING_EXIT()
	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
