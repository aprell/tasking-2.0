#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "tasking.h"
#include "async.h"
#include "wtime.h"

static int DIM;       // Matrix dimension
static int THRESHOLD; // Matrix dimension that ends subdivision
static int SEED;

#define A(i,j) A[(i) * DIM + (j)] // A: DIM x DIM
#define B(i,j) B[(i) * DIM + (j)] // B: DIM x DIM
#define C(i,j) C[(i) * DIM + (j)] // C: DIM x DIM
#define D(i,j) D[(i) * DIM + (j)] // D: DIM x DIM

static double *A, *B, *C, *D;

#if 0
static void print_matrix(double *A)
{
	int i, j;

	printf("\n%p\n", A);

	for (i = 0; i < DIM; i++) {
		for (j = 0; j < DIM; j++) {
			printf("%5.2lf ", A(i,j));
		}
		printf("\n");
	}
}
#endif

static bool is_power_of_two(int n)
{
	return n != 0 && (n & (n-1)) == 0;
}

static void mm_init(int argc, char *argv[])
{
	int i, j;

	if (argc != 3) {
		printf("Usage: %s <DIM> <THRESHOLD>\n", argv[0]);
		fflush(stdout);
		exit(0);
	}

	DIM = atoi(argv[1]);
	THRESHOLD = atoi(argv[2]);
	assert(is_power_of_two(DIM));
	assert(1 <= THRESHOLD && THRESHOLD <= DIM);

	SEED = 100;

	A = (double *)malloc(DIM * DIM * sizeof(double));
	B = (double *)malloc(DIM * DIM * sizeof(double));
	C = (double *)malloc(DIM * DIM * sizeof(double));
	D = (double *)malloc(DIM * DIM * sizeof(double));
	assert(A && B && C && D);

	// Setup matrices A and B with random values; zero C and D
	for (i = 0; i < DIM; i++) {
		for (j = 0; j < DIM; j++) {
			A(i,j) = (double)rand() / (double)RAND_MAX;
			B(i,j) = (double)rand() / (double)RAND_MAX;
			C(i,j) = D(i,j) = 0.0;
		}
	}
}

static void mm_exit(void)
{
	free(A);
	free(B);
	free(C);
	free(D);
}

static void mm_base(double *A, double *B, double *C, int n)
{
	int i, j, k;

	for (i = 0; i < n; i++) {
		for (k = 0; k < n; k++) {
			for (j = 0; j < n; j++) {
				C(i,j) += A(i,k) * B(k,j);
			}
		}
	}
}

// Recursive divide-and-conquer algorithm (sequential version)
// http://web.mit.edu/neboat/www/6.S898-sp17/mm.pdf
static bool mm_dac_seq(double *A, double *B, double *C, int size)
{
	if (size <= THRESHOLD) {
		mm_base(A, B, C, size);
	} else {
		// Submatrix offsets
		int s11 = 0;                         // Upper left
		int s12 = size/2;                    // Upper right
		int s21 = (size/2) * DIM;            // Lower left
		int s22 = (size/2) * DIM + size/2;   // Lower right

		// Recursive calls
		mm_dac_seq(A+s11, B+s11, C+s11, size/2); // C11  = A11 * B11
		mm_dac_seq(A+s11, B+s12, C+s12, size/2); // C12  = A11 * B12
		mm_dac_seq(A+s21, B+s11, C+s21, size/2); // C21  = A21 * B11
		mm_dac_seq(A+s21, B+s12, C+s22, size/2); // C22  = A21 * B12

		mm_dac_seq(A+s12, B+s21, C+s11, size/2); // C11 += A12 * B21
		mm_dac_seq(A+s12, B+s22, C+s12, size/2); // C12 += A12 * B22
		mm_dac_seq(A+s22, B+s21, C+s21, size/2); // C21 += A22 * B21
		mm_dac_seq(A+s22, B+s22, C+s22, size/2); // C22 += A22 * B22
	}

	return true;
}

static bool mm_dac(double *, double *, double *, int);

DEFINE_FUTURE (bool, mm_dac, (double *, double *, double *, int));

// Recursive divide-and-conquer algorithm (task-parallel version)
// http://web.mit.edu/neboat/www/6.S898-sp17/mm.pdf
static bool mm_dac(double *A, double *B, double *C, int size)
{
	if (size <= THRESHOLD) {
		mm_base(A, B, C, size);
	} else {
		// Submatrix offsets
		int s11 = 0;                         // Upper left
		int s12 = size/2;                    // Upper right
		int s21 = (size/2) * DIM;            // Lower left
		int s22 = (size/2) * DIM + size/2;   // Lower right

		bool a, b, c, d;

		// Recursive calls
		AWAIT_ALL {
			FUTURE(mm_dac, (A+s11, B+s11, C+s11, size/2), &a); // C11  = A11 * B11
			FUTURE(mm_dac, (A+s11, B+s12, C+s12, size/2), &b); // C12  = A11 * B12
			FUTURE(mm_dac, (A+s21, B+s11, C+s21, size/2), &c); // C21  = A21 * B11
			FUTURE(mm_dac, (A+s21, B+s12, C+s22, size/2), &d); // C22  = A21 * B12
		}

		AWAIT_ALL {
			FUTURE(mm_dac, (A+s12, B+s21, C+s11, size/2), &a); // C11 += A12 * B21
			FUTURE(mm_dac, (A+s12, B+s22, C+s12, size/2), &b); // C12 += A12 * B22
			FUTURE(mm_dac, (A+s22, B+s21, C+s21, size/2), &c); // C21 += A22 * B21
			FUTURE(mm_dac, (A+s22, B+s22, C+s22, size/2), &d); // C22 += A22 * B22
		}
	}

	return true;
}

static bool equals(double *A, double *B)
{
	int i, j;

	for (i = 0; i < DIM; i++) {
		for (j = 0; j < DIM; j++) {
			if (A(i,j) != B(i,j)) {
				return false;
			}
		}
	}

	return true;
}

int main(int argc, char *argv[])
{
	double start, end;

	mm_init(argc, argv);

	TASKING_INIT(&argc, &argv);

#if 0
	print_matrix(A);
	print_matrix(B);
	print_matrix(C);
#endif

	start = Wtime_msec();
	mm_dac_seq(A, B, C, DIM);
	end = Wtime_msec();

	printf("Elapsed wall time: %.2lf ms\n", end - start);

#if 0
	print_matrix(C);
#endif

	TASKING_EXIT();

	mm_base(A, B, D, DIM);
	assert(equals(C, D));

	mm_exit();

	return 0;
}
