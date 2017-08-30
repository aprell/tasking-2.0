#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "tasking.h"
#include "async.h"

// ASYNC procedures (have asynchronous side effects)

void nrt1(int a1)
{
	printf("nrt1: %d\n", a1);
}

void nrt2(int a1, int a2)
{
	printf("nrt2: %d, %d\n", a1, a2);
}

void nrt3(int a1, int a2, int a3)
{
	printf("nrt3: %d, %d, %d\n", a1, a2, a3);
}

DEFINE_ASYNC(nrt1, (int));
DEFINE_ASYNC(nrt2, (int, int));
DEFINE_ASYNC(nrt3, (int, int, int));

// FUTURE functions (return values in the future)

int wrt1(int a1)
{
	printf("wrt1: %d\n", a1);
	return a1;
}

int wrt2(int a1, int a2)
{
	printf("wrt2: %d, %d\n", a1, a2);
	return a1 + a2;
}

int wrt3(int a1, int a2, int a3)
{
	printf("wrt3: %d, %d, %d\n", a1, a2, a3);
	return a1 + a2 + a3;
}

#ifdef LAZY_FUTURES
DEFINE_LAZY_FUTURE(int, wrt1, (int));
DEFINE_LAZY_FUTURE(int, wrt2, (int, int));
DEFINE_LAZY_FUTURE(int, wrt3, (int, int, int));
#else
DEFINE_FUTURE(int, wrt1, (int));
DEFINE_FUTURE(int, wrt2, (int, int));
DEFINE_FUTURE(int, wrt3, (int, int, int));
#endif

int main(int argc, char *argv[])
{
	TASKING_INIT(&argc, &argv);

	ASYNC(nrt1, 1);
	ASYNC(nrt2, 1, 2);
	ASYNC(nrt3, 1, 2, 3);

	TASKING_BARRIER();

#ifdef LAZY_FUTURES
	lazy_future *f1 = __LAZY_ASYNC(wrt1, 1);
	lazy_future *f2 = __LAZY_ASYNC(wrt2, 1, 2);
	lazy_future *f3 = __LAZY_ASYNC(wrt3, 1, 2, 3);

	assert(__LAZY_AWAIT(f3, int) == 6);
	assert(__LAZY_AWAIT(f2, int) == 3);
	assert(__LAZY_AWAIT(f1, int) == 1);
#else
	future f1 = __ASYNC(wrt1, 1);
	future f2 = __ASYNC(wrt2, 1, 2);
	future f3 = __ASYNC(wrt3, 1, 2, 3);

	assert(__AWAIT(f3, int) == 6);
	assert(__AWAIT(f2, int) == 3);
	assert(__AWAIT(f1, int) == 1);
#endif

	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
