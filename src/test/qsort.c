#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include "tasking.h"
#include "async.h"
#include "chanref.h"
#include "wtime.h"

typedef long ELM;

#define NUM_ELEMS 100000000
#define INS_SORT_THRESHOLD 100

static ELM *A;
//static ELM localA[INS_SORT_THRESHOLD];
static PRIVATE unsigned int seed = 100;

static void insertionsort(ELM *A, int length)
{
	int i, j;

	for (i = 0; i < length; i++) {
		ELM v = A[i];
		for (j = i - 1; j >= 0; j--) {
			if (A[j] <= v)
				break;
			A[j+1] = A[j];
		}
		A[j+1] = v;
	}
}

static inline void swap(ELM *A, int i, int j)
{
	ELM tmp = A[i];
	A[i] = A[j];
	A[j] = tmp;
}

static inline int median_of_three(ELM *A, int left, int right)
{
	int mid = (right - left) / 2;
	ELM a = A[left], b = A[left + mid], c = A[right];

	if ((a - b) * (c - a) >= 0) return 0;
	if ((b - a) * (c - b) >= 0) return mid;
	return right - left;
}

//#define PIVOT (rand_r(&seed) % n)
#define PIVOT (median_of_three(A, left, right-1))

bool quicksort_seq(int left, int right)
{
	int i, last, n;

	n = right - left + 1;
	if (n <= 1)
		return true;

	// User-defined cutoff
	if (n <= INS_SORT_THRESHOLD) {
		//memcpy(localA, &A[left], n * sizeof(ELM));
		//insertionsort(localA, n);
		//memcpy(&A[left], localA, n * sizeof(ELM));
		insertionsort(&A[left], n);
		return true;
	}

	// Move pivot to A[left]
	swap(A, left, left + PIVOT);
	last = left;
	for (i = left + 1; i <= right; i++)
		if (A[i] < A[left])
			// A[i] has to come before pivot
			swap(A, ++last, i);
	// Move pivot to its final place
	swap(A, left, last);

	quicksort_seq(left, last - 1);
	quicksort_seq(last + 1, right);

	return true;
}

bool quicksort(int, int);

FUTURE_DECL_FREELIST(bool);
FUTURE_DECL(bool, quicksort, int left; int right, left, right);

bool quicksort(int left, int right)
{
	int i, last, n;
	future is_sorted;

	n = right - left + 1;
	if (n <= 1)
		return true;

	// User-defined cutoff
	if (n <= INS_SORT_THRESHOLD) {
		//memcpy(localA, &A[left], n * sizeof(ELM));
		//insertionsort(localA, n);
		//memcpy(&A[left], localA, n * sizeof(ELM));
		insertionsort(&A[left], n);
		return true;
	}

	// Move pivot to A[left]
	swap(A, left, left + PIVOT);
	last = left;
	for (i = left + 1; i <= right; i++)
		if (A[i] < A[left])
			// A[i] has to come before pivot
			swap(A, ++last, i);
	// Move pivot to its final place
	swap(A, left, last);

	is_sorted = __ASYNC(quicksort, left, last - 1);
	quicksort(last + 1, right);

	return __AWAIT(is_sorted, bool);
}

static void verify_result(void)
{
	int i;

	for (i = 0; i < NUM_ELEMS-1; i++) {
		if (A[i+1] < A[i]) {
			printf("Quicksort failed: %ld < %ld\n", (long)A[i+1], (long)A[i]);
		}
	}
}

int main(int argc, char *argv[])
{
	int i;
	double start, end;

	A = (ELM *)malloc(NUM_ELEMS * sizeof(ELM));

	TASKING_INIT(&argc, &argv);

	for (i = 0; i < NUM_ELEMS; i++)
		A[i] = rand_r(&seed);

	start = Wtime_msec();
	//quicksort_seq(0, NUM_ELEMS-1);
	quicksort(0, NUM_ELEMS-1);
	end = Wtime_msec();
	verify_result();

	printf("Elapsed wall time: %.2lf ms\n", end - start);

	// This should be moved inside TASKING_EXIT()
	TASKING_BARRIER();
	TASKING_EXIT();

	free(A);

	return 0;
}
