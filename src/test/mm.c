//#define VERIFY
#define LOOPTASKS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>
#include "tasking.h"
#include "async.h"
#include "wtime.h"
#ifdef VERIFY
#include "test/mm.ref"
#endif

static int DIM; 	// Matrix dimension
static int BSIZE; 	// Block size
static int NBD;		// Number of blocks in one dimension
static int NB;		// Total number of blocks
static int NEB;		// Number of matrix elements in one block
static int NE;		// Total number of matrix elements
static int SEED;

static double *A, *B, *C;
//static PRIVATE double *localA, *localB, *localC;

static double **BA, **BB, **BC;
//static PRIVATE double **localBA, **localBB, **localBC;

// Returns element X(i,j)
#define A(i,j) A[(i) * DIM + (j)]
#define B(i,j) B[(i) * DIM + (j)]
#define C(i,j) C[(i) * DIM + (j)]

// Returns element localX(i,j)
#define localA(i,j) localA[(i) * DIM + (j)]
#define localB(i,j) localB[(i) * DIM + (j)]
#define localC(i,j) localC[(i) * DIM + (j)]

// Block partitioned matrices
// Returns block BX(i,j)
#define BA(i,j) BA[(i) * NBD + (j)]
#define BB(i,j) BB[(i) * NBD + (j)]
#define BC(i,j) BC[(i) * NBD + (j)]

// Block partitioned matrices
// Returns block localBX(i,j)
#define localBA(i,j) localBA[(i) * NBD + (j)]
#define localBB(i,j) localBB[(i) * NBD + (j)]
#define localBC(i,j) localBC[(i) * NBD + (j)]

static void copy_elements(double BA[BSIZE][BSIZE], double BB[BSIZE][BSIZE], 
						  double BC[BSIZE][BSIZE], int i, int j)
{
	int m, n;

	for (m = 0; m < BSIZE; m++) {
		for (n = 0; n < BSIZE; n++) {
			BA[m][n] = A(i+m,j+n);
			BB[m][n] = B(i+m,j+n);
			BC[m][n] = C(i+m,j+n);
		}
	}
}

static void print_matrix(int DIM, double M[DIM][DIM])
{
	int i, j;

	for (i = 0; i < DIM; i++) {
		for (j = 0; j < DIM; j++) {
			printf("%5.2lf ", M[i][j]);
		}
		printf("\n");
	}
}

static void print_linear_matrix(double *M)
{
	print_matrix(DIM, (void *)M);
}

static void print_block_matrix(double **M)
{
	int i, j;

	for (i = 0; i < NBD; i++) {
		for (j = 0; j < NBD; j++) {
			//printf("Block (%d,%d):\n", i, j);
			print_matrix(BSIZE, (void *)M[i * NBD + j]);
		}
	}
}

static void mm_init(int argc, char *argv[])
{
	int i, j;

	if (argc != 3) {
		printf("Usage: %s <DIM> <BSIZE>\n", argv[0]);
		fflush(stdout);
		exit(0);
	}

	DIM = atoi(argv[1]);
	BSIZE = atoi(argv[2]);
	// Error checking...
	assert(DIM > 0 && BSIZE > 0);
	assert(DIM % BSIZE == 0);
	
	NBD = DIM / BSIZE;
	NB  = NBD * NBD;
	NEB = BSIZE * BSIZE;
	NE  = NB * NEB;
	SEED = 100;

	A = (double *)malloc(NE * sizeof(double));
	B = (double *)malloc(NE * sizeof(double));
	C = (double *)malloc(NE * sizeof(double));

	// Setup matrices A and B with random values
	for (i = 0; i < NE; i++) {
		A[i] = (double)rand() / (double)RAND_MAX;
		B[i] = (double)rand() / (double)RAND_MAX;
		C[i] = 0.0;
	}

	// Allocate and setup block matrices
	BA = (double **)malloc(NB * sizeof(double *));
	BB = (double **)malloc(NB * sizeof(double *));
	BC = (double **)malloc(NB * sizeof(double *));

	for (i = 0; i < NB; i++) {
		BA[i] = (double *)malloc(NEB * sizeof(double));
		BB[i] = (double *)malloc(NEB * sizeof(double));
		BC[i] = (double *)malloc(NEB * sizeof(double));
	}

	for (i = 0; i < NBD; i++)
		for (j = 0; j < NBD; j++)
			copy_elements((void *)BA(i,j), (void *)BB(i,j), (void *)BC(i,j),
						  i*BSIZE, j*BSIZE);

}

// TODO Make it possible to have per-thread "workspaces"

#if 0
static void initfn(void)
{
	// Replicate matrices
	localA = (double *)malloc(NE * sizeof(double));
	localB = (double *)malloc(NE * sizeof(double));
	localC = (double *)malloc(NE * sizeof(double));

	memcpy(localA, A, NE * sizeof(double));
	memcpy(localB, B, NE * sizeof(double));
	memcpy(localC, C, NE * sizeof(double));

	// Replicate block matrices
	localBA = (double **)malloc(NB * sizeof(double *));
	localBB = (double **)malloc(NB * sizeof(double *));
	localBC = (double **)malloc(NB * sizeof(double *));

	for (i = 0; i < NB; i++) {
		localBA[i] = (double *)malloc(NEB * sizeof(double));
		localBB[i] = (double *)malloc(NEB * sizeof(double));
		localBC[i] = (double *)malloc(NEB * sizeof(double));
		
		memcpy(localBA[i], BA[i], NEB * sizeof(double));
		memcpy(localBB[i], BB[i], NEB * sizeof(double));
		memcpy(localBC[i], BC[i], NEB * sizeof(double));
	}
}
#endif

static void mm_exit(void)
{
	int i;

	free(A);
	free(B);
	free(C);

	for (i = 0; i < NB; i++) {
		free(BA[i]);
		free(BB[i]);
		free(BC[i]);
	}

	free(BA);
	free(BB);
	free(BC);
}

#if 0
static void exitfn(void)
{
	free(localA);
	free(localB);
	free(localC);

	for (i = 0; i < NB; i++) {
		free(localBA[i]);
		free(localBB[i]);
		free(localBC[i]);
	}

	free(localBA);
	free(localBB);
	free(localBC);
}
#endif

//============================================================================= 
// Simple multiplication
//============================================================================= 

void matmul(int i, int j)
{
	int k;

	for (k = 0; k < DIM; k++)
		//localC(i,j) += localA(i,k) * localB(k,j);
		C(i,j) += A(i,k) * B(k,j);
		
	//C(i,j) = localC(i,j);
}

#ifdef LOOPTASKS
void matmul_loop(int i)
{
	long j, s, e;

	RT_loop_init(&s, &e);

	for (j = s; j < e; j++) {
		matmul(i, j);
		RT_loop_split(j+1, &e);
	}
}

ASYNC_DECL(matmul_loop, int i, i);

void mm(void)
{
	int i;

	for (i = 0; i < DIM; i++)
		// Create a loop task for each matrix row 
		ASYNC_FOR(matmul_loop, 0, DIM, i);
}

#else

ASYNC_DECL(matmul, int i; int j, i, j);

void mm(void)
{
	int i, j;

	for (i = 0; i < DIM; i++)
		for (j = 0; j < DIM; j++)
			// Create a task for each matrix element
			ASYNC(matmul, i, j);
}
#endif

void mm_seq(void)
{
	int i, j;

	for (i = 0; i < DIM; i++)
		for (j = 0; j < DIM; j++)
			matmul(i, j);
}

//============================================================================= 
// Block multiplication
//============================================================================= 

void __block_matmul(double C[BSIZE][BSIZE], double A[BSIZE][BSIZE], double B[BSIZE][BSIZE])
{
	int i, j, k;

	for (i = 0; i < BSIZE; i++)
		for (j = 0; j < BSIZE; j++)
			for (k = 0; k < BSIZE; k++)
				C[i][j] += A[i][k] * B[k][j];
}

void block_matmul(int i, int j, int k)
{
	//__block_matmul((void *)localBC(i,j), (void *)localBA(i,k), (void *)localBB(k,j));
	//memcpy(BC(i,j), localBC(i,j), NEB * sizeof(double));
	__block_matmul((void *)BC(i,j), (void *)BA(i,k), (void *)BB(k,j));
}

#ifdef LOOPTASKS
void block_matmul_loop(int i, int k)
{
	long j, s, e;

	RT_loop_init(&s, &e);

	for (j = s; j < e; j++) {
		block_matmul(i, j, k);
		RT_loop_split(j+1, &e);
		(void)RT_check_for_steal_requests();
	}
}

ASYNC_DECL(block_matmul_loop, int i; int k, i, k);

void block_mm(void)
{
	int i, k;

	for (k = 0; k < NBD; k++) {
		for (i = 0; i < NBD; i++) {
			// Create a loop task for all blocks in a row
			ASYNC_FOR(block_matmul_loop, 0, NBD, i, k);
		}
		TASKING_BARRIER();
	}
}

#else

ASYNC_DECL(block_matmul, int i; int j; int k, i, j, k);

void block_mm(void)
{
	int i, j, k;

	for (k = 0; k < NBD; k++) {
		for (i = 0; i < NBD; i++) {
			for (j = 0; j < NBD; j++) {
				// Create a task for each block (i,j)
				ASYNC(block_matmul, i, j, k);
			}
		}
		TASKING_BARRIER();
	}
}
#endif

void block_mm_seq(void)
{
	int i, j, k;

	for (k = 0; k < NBD; k++) {
		for (i = 0; i < NBD; i++) {
			for (j = 0; j < NBD; j++) {
				block_matmul(i, j, k);
			}
		}
	}
}

#ifndef VERIFY
static void write_result(const char *fname)
{
	FILE *file;
	int i;

	file = fopen(fname, "w");
	if (!file) {
		fprintf(stderr, "Cannot open file %s for writing\n", fname);
		return;
	}

	fprintf(file, "#define REF_DIM %d\n", DIM);	
	fprintf(file, "#define REF_BSIZE %d\n", BSIZE);	
	fprintf(file, "#define REF_SEED %d\n\n", SEED);

	fprintf(file, "static double REF_C[] = {\n");
	
	// One element per line
	for (i = 0; i < NE-1; i++)
		fprintf(file, "\t%lf,\n", C[i]);
	fprintf(file, "\t%lf\n", C[i]);

	fprintf(file, "};\n");
	fclose(file);
}
#else
static void write_result(const char *fname __attribute__((unused))) { }
#endif

#ifdef VERIFY
static void verify_result(void)
{
	int i;

	if (DIM != REF_DIM) {
		printf("Cannot verify result: dimension %d != %d\n", DIM, REF_DIM);
		return;
	}

	if (SEED != REF_SEED) {
		printf("Cannot verify result: rand seed %d != %d\n", SEED, REF_SEED);
		return;
	}

#define ERROR 1e-6
	for (i = 0; i < NE; i++) {
		if (fabs(C[i] - REF_C[i]) > ERROR)
			printf("Matrix multiplication failed: %lf != %lf\n", C[i], REF_C[i]);
	}
}
#else
static void verify_result(void) { }
#endif

int main(int argc, char *argv[])
{
	double start, end;

	mm_init(argc, argv);

	TASKING_INIT(&argc, &argv);

	start = Wtime_msec();
	//mm_seq();
	//mm();
	//block_mm_seq();
	block_mm();
	TASKING_BARRIER();
	end = Wtime_msec();

	printf("Elapsed wall time: %.2lfms\n", end - start);
	
	// Result matrices
	//print_linear_matrix(C);
	//print_block_matrix(BC);

	//write_result("test/mm.ref");
	//verify_result();

	// This should be moved inside TASKING_EXIT()
	TASKING_BARRIER();
	TASKING_EXIT();

	mm_exit();

	return 0;
}
