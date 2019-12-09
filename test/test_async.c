#define EXPERIMENTAL

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "tasking.h"

// ASYNC procedures (have asynchronous side effects)

void nrt0(void)
{
	puts("nrt0");
}

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

// Declaration syntax mimics call syntax
DEFINE_ASYNC0 (nrt0, ());
DEFINE_ASYNC  (nrt1, (int));
DEFINE_ASYNC  (nrt2, (int, int));
DEFINE_ASYNC  (nrt3, (int, int, int));

// Splittable ASYNC procedures

void nrt0L(void)
{
	long i;

	ASYNC_FOR (i) {
		printf("nrt0L%ld\n", i);
	}
}

void nrt1L(int a1)
{
	long i;

	ASYNC_FOR (i) {
		printf("nrt1L%ld: %d\n", i, a1);
	}
}

void nrt2L(int a1, int a2)
{
	long i;

	ASYNC_FOR (i) {
		printf("nrt2L%ld: %d, %d\n", i, a1, a2);
	}
}

void nrt3L(int a1, int a2, int a3)
{
	long i;

	ASYNC_FOR (i) {
		printf("nrt3L%ld: %d, %d, %d\n", i, a1, a2, a3);
	}
}

DEFINE_ASYNC0 (nrt0L, ());
DEFINE_ASYNC  (nrt1L, (int));
DEFINE_ASYNC  (nrt2L, (int, int));
DEFINE_ASYNC  (nrt3L, (int, int, int));

// FUTURE functions (return values in the future)

int wrt0(void)
{
	puts("wrt0");
	return 0;
}

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

// Declaration syntax mimics call syntax
DEFINE_FUTURE0 (int, wrt0, ());
DEFINE_FUTURE  (int, wrt1, (int));
DEFINE_FUTURE  (int, wrt2, (int, int));
DEFINE_FUTURE  (int, wrt3, (int, int, int));

// Splittable FUTURE functions whose results can be combined

#define N 100000
static long array[N];

long wrt0L(void)
{
	long sum = 0, i;

	ASYNC_FOR (i) {
		sum += array[i];
	}

	printf("wrt0L: %ld\n", REDUCE(+, sum));
	return sum;
}

long wrt1L(long array[])
{
	long sum = 0, i;

	ASYNC_FOR (i) {
		sum += array[i];
	}

	printf("wrt1L: %ld\n", REDUCE(+, sum));
	return sum;
}

DEFINE_FUTURE0 (long, wrt0L, ());
DEFINE_FUTURE  (long, wrt1L, (long *));

int main(int argc, char *argv[])
{
	int i;

	TASKING_INIT(&argc, &argv);

	ASYNC0 (nrt0, ());
	ASYNC  (nrt1, (1));
	ASYNC  (nrt2, (1, 2));
	ASYNC  (nrt3, (1, 2, 3));

	ASYNC0 (nrt0L, (0, 3), ());
	ASYNC  (nrt1L, (0, 3), (1));
	ASYNC  (nrt2L, (0, 3), (1, 2));
	ASYNC  (nrt3L, (0, 3), (1, 2, 3));

	TASKING_BARRIER();

	future f0 = FUTURE0 (wrt0, ());
	future f1 = FUTURE  (wrt1, (1));
	future f2 = FUTURE  (wrt2, (1, 2));
	future f3 = FUTURE  (wrt3, (1, 2, 3));

	assert(AWAIT(f3, int) == 6);
	assert(AWAIT(f2, int) == 3);
	assert(AWAIT(f1, int) == 1);
	assert(AWAIT(f0, int) == 0);

	TASKING_BARRIER();

	int a, b;

	AWAIT_ALL {
		FUTURE0 (wrt0, (), &a);
		FUTURE  (wrt1, (1), &b);

		int c, d;

		AWAIT_ALL {
			FUTURE  (wrt2, (1, 2), &c);
			FUTURE  (wrt3, (1, 2, 3), &d);

			int e, f;

			AWAIT_ALL {
				FUTURE  (wrt3, (4, 5, 6), &e);
				FUTURE  (wrt3, (7, 8, 9), &f);
			}

			assert(e == 15);
			assert(f == 24);
		}

		assert(c == 3);
		assert(d == 6);
	}

	assert(a == 0);
	assert(b == 1);

	TASKING_BARRIER();

	for (i = 0; i < N; i++) {
		array[i] = 1;
	}

	// The fourth and last argument is used for "overload resolution".
	// There might be a better solution.
	future f4 = FUTURE0 (wrt0L, (0, N), (), 0);
	future f5 = FUTURE  (wrt1L, (0, N), (array), 0);

	assert(AWAIT(f5, long) == N);
	assert(AWAIT(f4, long) == N);

	TASKING_EXIT();

	return 0;
}
