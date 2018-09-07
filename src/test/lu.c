//#define LOOPTASKS

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include "tasking.h"
#include "async.h"
#include "wtime.h"

static int DIM; 	// Matrix dimension
static int BSIZE; 	// Block size
static int NBD;		// Number of blocks in one dimension
static int NB;		// Total number of blocks
static int NEB;		// Number of matrix elements in one block
static int NE;		// Total number of matrix elements
static int SEED;	// Random number seed

static double **A;

// Block partitioned matrix
// Returns block A(i,j)
#define A(i,j) A[(i) * NBD + (j)]

static double *allocate_clean_block(void)
{
	double *block;
	int i;

	block = (double *)malloc(NEB * sizeof(double));

	for (i = 0; i < NEB; i++)
		block[i] = 0.0;

	return block;
}

//#pragma css task input(BSIZE) inout(diag)
static void __lu0(int BSIZE, double C[BSIZE][BSIZE])
{
	int i, j, k;

	for (k = 0; k < BSIZE; k++) {
		for (i = k + 1; i < BSIZE; i++) {
			C[i][k] = C[i][k] / C[k][k];
			for (j = k + 1; j < BSIZE; j++) {
				C[i][j] = C[i][j] - C[i][k] * C[k][j];
			}
		}
	}
}

static void lu0(int i)
{
	__lu0(BSIZE, (void *)A(i,i));
}

DEFINE_ASYNC(lu0, (int));

//#pragma css task input(BSIZE, diag) inout(row)
static void __bdiv(int BSIZE, double A[BSIZE][BSIZE], double C[BSIZE][BSIZE])
{
	int i, j, k;

	for (i = 0; i < BSIZE; i++) {
		for (k = 0; k < BSIZE; k++) {
			C[i][k] = C[i][k] / A[k][k];
			for (j = k + 1; j < BSIZE; j++) {
				C[i][j] = C[i][j] - C[i][k] * A[k][j];
			}
		}
		(void)RT_check_for_steal_requests();
	}
}

static void bdiv(int i, int j)
{
	__bdiv(BSIZE, (void *)A(i,i), (void *)A(j,i));
}

DEFINE_ASYNC(bdiv, (int, int));

//#pragma css task input(BSIZE, row, col) inout(inner)
static void __bmod(int BSIZE, double A[BSIZE][BSIZE], double B[BSIZE][BSIZE], double C[BSIZE][BSIZE])
{
	int i, j, k;

	for (i = 0; i < BSIZE; i++) {
		for (j = 0; j < BSIZE; j++) {
			for (k = 0; k < BSIZE; k++) {
				C[i][j] = C[i][j] - A[i][k] * B[k][j];
			}
		}
		(void)RT_check_for_steal_requests();
	}
}

static void bmod(int i, int j, int k)
{
	if (!A(i,j)) {
		A(i,j) = allocate_clean_block();
	}
	__bmod(BSIZE, (void *)A(i,k), (void *)A(k,j), (void *)A(i,j));
}

DEFINE_ASYNC(bmod, (int, int, int));

//#pragma css task input(BSIZE, diag) inout(col)
static void __fwd(int BSIZE, double A[BSIZE][BSIZE], double C[BSIZE][BSIZE])
{
	int i, j, k;

	for (j = 0; j < BSIZE; j++) {
		for (k = 0; k < BSIZE; k++) {
			for (i = k + 1; i < BSIZE; i++) {
				C[i][j] = C[i][j] - A[i][k] * C[k][j];
			}
		}
		(void)RT_check_for_steal_requests();
	}
}

static void fwd(int i, int j)
{
	__fwd(BSIZE, (void *)A(i,i), (void *)A(i,j));
}

DEFINE_ASYNC(fwd, (int, int));

static void lu_init(int argc, char *argv[])
{
	bool null_entry;
	int zeros = 0, nonzeros = 0;
	int init_val, i, j, k;

	if (argc != 3) {
		printf("Usage: %s <dimension> <blocksize>\n", argv[0]);
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

	/* Allocate sparse matrix */
	A = (double **)malloc(NB * sizeof(double *));

	for (i = 0; i < NBD; i++) {
		for (j = 0; j < NBD; j++) {
			null_entry = false;
			if ((i < j) && (i % 3 != 0))
				null_entry = true;
			if ((i > j) && (j % 3 != 0))
				null_entry = true;
			if (i % 2 == 1)
				null_entry = true;
			if (j % 2 == 1)
				null_entry = true;
			if (i == j)
				null_entry = false;
			if (i == j-1)
				null_entry = false;
			if (i-1 == j)
				null_entry = false;

			if (null_entry == false)
				A(i,j) = (double *)malloc(NEB * sizeof(double));
			else A(i,j) = NULL;

			if (null_entry) {
				zeros++;
			} else {
				nonzeros++;
			}
		}
	}

	assert(zeros + nonzeros == NB);

	printf("Zero blocks: %d (%.0f%%), Nonzero blocks: %d (%.0f%%)\n",
			zeros, (float)zeros * 100 / NB, nonzeros, (float)nonzeros * 100 / NB);

	/* Initialize matrix */
	init_val = 1325;

	for (i = 0; i < NBD; i++) {
		for (j = 0; j < NBD; j++) {
			double *block = A(i,j);
			if (block) {
				for (k = 0; k < NEB; k++) {
					init_val = (3125 * init_val) % 65536;
					block[k] = (double)((init_val - 32768.0) / 16384.0);
				}
			}
		}
	}
}

static void lu_exit(void)
{
	int i, j;

	for (i = 0; i < NBD; i++)
		for (j = 0; j < NBD; j++)
			free(A(i,j));

	free(A);
}

static void print_structure(double *A[NBD][NBD])
{
	double *p;
	int i, j;

	printf("Matrix A\n");

	for (i = 0; i < NBD; i++) {
		for (j = 0; j < NBD; j++) {
			p = A[i][j];
			if (p)
				printf("1");
			else
				printf("0");
		}
		printf("\n");
	}
	printf("\n");
}

static void write_to_file(const char *filename)
{
	FILE *file = fopen(filename, "w");
	int i;

	for (i = 0; i < NB; i++) {
		double *block = A[i];
		int j, k;
		fprintf(file, "Block %5d:\n", i);
		if (!block) {
			fprintf(file, "NULL\n\n");
			continue;
		}
		for (j = 0; j < BSIZE; j++) {
			for (k = 0; k < BSIZE; k++) {
				fprintf(file, "%5.2f ", block[j*BSIZE+k]);
				fflush(file);
			}
			fprintf(file, "\n");
		}
		fprintf(file, "\n");
	}

	fclose(file);
}

static void lu_decompose(void)
{
	int i, j, k;

	for (k = 0; k < NBD; k++) {
		lu0(k);

		// sync

		for (j = k + 1; j < NBD; j++) {
			if (A(k,j)) {
				fwd(k, j);
			}
		}

		for (i = k + 1; i < NBD; i++) {
			if (A(i,k)) {
				bdiv(k, i);
			}
		}

		// sync

		for (i = k + 1; i < NBD; i++) {
			if (A(i,k)) {
				for (j = k + 1; j < NBD; j++) {
					if (A(k,j)) {
						bmod(i, j, k);
					}
				}
			}
		}

		// sync
	}
}

#ifdef LOOPTASKS

static void fwd_loop(int k)
{
	long j;

	ASYNC_FOR (j) {
		if (A(k,j)) {
			fwd(k, j);
		}
	}
}

DEFINE_ASYNC(fwd_loop, (int));

static void bdiv_loop(int k)
{
	long i;

	ASYNC_FOR (i) {
		if (A(i,k)) {
			bdiv(k, i);
		}
	}
}

DEFINE_ASYNC(bdiv_loop, (int));

static void bmod_loop(int k)
{
	long i, j;

	ASYNC_FOR (i) {
		if (A(i,k)) {
			for (j = k + 1; j < NBD; j++) {
				if (A(k,j)) {
					ASYNC(bmod, (i, j, k));
				}
			}
		}
	}
}

DEFINE_ASYNC(bmod_loop, (int));

static void par_lu_decompose(void)
{
	int k;

	for (k = 0; k < NBD; k++) {
		lu0(k);

		ASYNC(fwd_loop, (k+1, NBD), (k));

		ASYNC(bdiv_loop, (k+1, NBD), (k));

		TASKING_BARRIER();

		ASYNC(bmod_loop, (k+1, NBD), (k));

		TASKING_BARRIER();
	}
}

#else

static void par_lu_decompose(void)
{
	int i, j, k;

	for (k = 0; k < NBD; k++) {
		lu0(k);

		for (j = k + 1; j < NBD; j++) {
			if (A(k,j)) {
				ASYNC(fwd, (k, j));
			}
		}

		for (i = k + 1; i < NBD; i++) {
			if (A(i,k)) {
				ASYNC(bdiv, (k, i));
			}
		}

		TASKING_BARRIER();

		for (i = k + 1; i < NBD; i++) {
			if (A(i,k)) {
				for (j = k + 1; j < NBD; j++) {
					if (A(k,j)) {
						ASYNC(bmod, (i, j, k));
					}
				}
			}
		}

		TASKING_BARRIER();
	}
}

#endif // LOOPTASKS for par_lu_decompose

#if 0
static void lu_decompose(void)
{
	LU_task task = { .dim = DIM, .bsize = BSIZE };
	int i, j, k;

	for (k = 0; k < NBD; k++) {
		task.C = ULL(A[k*NBD+k]);
		tpool_put_task(tpool, &task, sizeof(LU_task), lu0());
		//lu0(BSIZE, (void *)A[k*NBD+k]);
		//printf("Scheduling lu0 task for C[%d][%d]\n", k, k);

		tpool_wait_for_completion(tpool);

		for (j = k + 1; j < NBD; j++) {
			if (A[k*NBD+j]) {
				task.A = ULL(A[k*NBD+k]);
				task.C = ULL(A[k*NBD+j]);
				tpool_put_task(tpool, &task, sizeof(LU_task), fwd());
				//fwd(BSIZE, (void *)A[k*NBD+k], (void *)A[k*NBD+j]);
				//printf("Scheduling fwd task for C[%d][%d]\n", k, j);
			}
		}

		for (i = k + 1; i < NBD; i++) {
			if (A[i*NBD+k]) {
				task.A = ULL(A[k*NBD+k]);
				task.C = ULL(A[i*NBD+k]);
				tpool_put_task(tpool, &task, sizeof(LU_task), bdiv());
				//bdiv(BSIZE, (void *)A[k*NBD+k], (void *)A[i*NBD+k]);
				//printf("Scheduling bdiv task for C[%d][%d]\n", i, k);
			}
		}

		tpool_wait_for_completion(tpool);

		for (i = k + 1; i < NBD; i++) {
			if (A[i*NBD+k]) {
				for (j = k + 1; j < NBD; j++) {
					if (A[k*NBD+j]) {
						if (!A[i*NBD+j])
							A[i*NBD+j] = allocate_clean_block();
						task.A = ULL(A[i*NBD+k]);
						task.B = ULL(A[k*NBD+j]);
						task.C = ULL(A[i*NBD+j]);
						tpool_put_task(tpool, &task, sizeof(LU_task), bmod());
						//bmod(BSIZE, (void *)A[i*NBD+k], (void *)A[k*NBD+j],
						//	  (void *)A[i*NBD+j]);
						//printf("Scheduling bmod task for C[%d][%d]\n", i, j);
					}
				}
			}
		}

		tpool_wait_for_completion(tpool);

#if 0
		for (i = k + 1; i < NBD; i++) {
			if (A[i*NBD+k]) {
				task.A = ULL(A[k*NBD+k]);
				task.C = ULL(A[i*NBD+k]);
				tpool_put_task(tpool, &task, sizeof(task), bdiv());
				tpool_wait_for_completion(tpool);
				//bdiv(BSIZE, (void *)A[k*NBD+k], (void *)A[i*NBD+k]);
				for (j = k + 1; j < NBD; j++) {
					if (A[k*NBD+j]) {
						if (!A[i*NBD+j])
							A[i*NBD+j] = allocate_clean_block();
						task.A = ULL(A[i*NBD+k]);
						task.B = ULL(A[k*NBD+j]);
						task.C = ULL(A[i*NBD+j]);
						tpool_put_task(tpool, &task, sizeof(task), bmod());
						//bmod(BSIZE, (void *)A[i*NBD+k], (void *)A[k*NBD+j],
						//		(void *)A[i*NBD+j]);
					}
				}
				tpool_wait_for_completion(tpool);
			}
		}
#endif
	}
}
#endif

#if 0
static void decompose_offloaded(void)
{
	LU_task task;
	int task_creats = NSPES/2 + NSPES%2;
	int i, j, k;

	task.A_base = ULL(A);
	task.ptr_size = sizeof(double *);
	task.dim = DIM;
	task.bsize = BSIZE;
	task.nb = NBD;

	start_timer();

	for (k = 0; k < NBD; k++) {
		task.k = k;
		tpool_offload_prepare(tpool, &task, sizeof(task), lu0_prepare());
		tpool_offload_execute(tpool, 1, task_creats, lu0());
		//task.C = ULL(A[k*NBD+k]);
		//tpool_put_task(tpool, &task, sizeof(task), lu0());

		tpool_wait_for_completion(tpool);

		tpool_offload_prepare(tpool, &task, sizeof(task), fwd_prepare());
		tpool_offload_execute(tpool, NBD-(k+1), task_creats, fwd());

		//XXX
		/* Not really necessary, but we need to make sure that we don't
		 * just overwrite the previous offload information
		 * There might be a better way of doing this, e.g. blocking until the
		 * offload information has been cleared by the SPEs */
		tpool_wait_for_completion(tpool);

		tpool_offload_prepare(tpool, &task, sizeof(task), bdiv_prepare());
		tpool_offload_execute(tpool, NBD-(k+1), task_creats, bdiv());

		for (i = k + 1; i < NBD; i++) {
			if (A[i*NBD+k]) {
				for (j = k + 1; j < NBD; j++) {
					if (A[k*NBD+j]) {
						if (!A[i*NBD+j])
							A[i*NBD+j] = allocate_clean_block();
					}
				}
			}
		}

		tpool_wait_for_completion(tpool);

		tpool_offload_prepare(tpool, &task, sizeof(task), bmod_prepare());
		tpool_offload_execute(tpool, (NBD-(k+1)) * (NBD-(k+1)), task_creats, bmod());

		tpool_wait_for_completion(tpool);

		//printf("==================================================\n");
	}

	end_timer();
}

static void decompose_splitting(void)
{
	LU_task task;
	int i, j, k;

	task.A_base = ULL(A);
	task.ptr_size = sizeof(double *);
	task.dim = DIM;
	task.bsize = BSIZE;
	task.nb = NBD;

	start_timer();

	for (k = 0; k < NBD; k++) {
		task.k = k;

		tpool_put_task_bundle(tpool, 0, 1, &task, sizeof(task),
				              lu0_prepare(), lu0());
		tpool_wait_for_completion(tpool);

		tpool_put_task_bundle(tpool, 0, NBD-(k+1), &task, sizeof(task),
				              fwd_prepare(), fwd());
		tpool_put_task_bundle(tpool, 0, NBD-(k+1), &task, sizeof(task),
				              bdiv_prepare(), bdiv());

		for (i = k + 1; i < NBD; i++) {
			if (A[i*NBD+k]) {
				for (j = k + 1; j < NBD; j++) {
					if (A[k*NBD+j]) {
						if (!A[i*NBD+j])
							A[i*NBD+j] = allocate_clean_block();
					}
				}
			}
		}

		tpool_wait_for_completion(tpool);

		tpool_put_task_bundle(tpool, 0, (NBD-(k+1)) * (NBD-(k+1)), &task,
				              sizeof(task), bmod_prepare(), bmod());
		tpool_wait_for_completion(tpool);
	}

	end_timer();
}
#endif

int main(int argc, char *argv[])
{
	double start, end;

	lu_init(argc, argv);

	TASKING_INIT(&argc, &argv);

	start = Wtime_msec();
	//lu_decompose();
	par_lu_decompose();
	end = Wtime_msec();

	printf("Elapsed wall time: %.2lf ms\n", end - start);

	//print_structure((void *)A);

	// This should be moved inside TASKING_EXIT()
	TASKING_BARRIER();
	TASKING_EXIT();

	lu_exit();

	return 0;
}
