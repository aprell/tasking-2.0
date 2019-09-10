#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include "tasking.h"
#include "async.h"
#include "wtime.h"

unsigned long long seqfib(int n)
{
	unsigned long long x, y;

	if (n < 2) return n;

	x = seqfib(n-1);
	y = seqfib(n-2);

	return x + y;
}

unsigned long long parfib(int);

DEFINE_FUTURE(unsigned long long, parfib, (int));

unsigned long long parfib(int n)
{
	future x;
	unsigned long long y;

	if (n < 2) return n;

    x = FUTURE(parfib, (n-1));
	y = parfib(n-2);

	return AWAIT(x, unsigned long long) + y;
}

int main(int argc, char *argv[])
{
	double start, end;
	unsigned long long f;

	if (argc != 2) {
		exit(0);
	}

	int N = atoi(argv[1]);

	TASKING_INIT(&argc, &argv);

	start = Wtime_msec();
	//f = fib_like_seq(FIB_LIKE_N);
	f = parfib(N);
	end = Wtime_msec();

	printf("Elapsed wall time: %.2lf ms\n", end-start);
	printf("fib(%d) = %llu\n", N, f);

	// This should be moved inside TASKING_EXIT()
	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
