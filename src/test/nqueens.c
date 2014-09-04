/**********************************************************************************************/
/*  This program is part of the Barcelona OpenMP Tasks Suite                                  */
/*  Copyright (C) 2009 Barcelona Supercomputing Center - Centro Nacional de Supercomputacion  */
/*  Copyright (C) 2009 Universitat Politecnica de Catalunya                                   */
/*                                                                                            */
/*  This program is free software; you can redistribute it and/or modify                      */
/*  it under the terms of the GNU General Public License as published by                      */
/*  the Free Software Foundation; either version 2 of the License, or                         */
/*  (at your option) any later version.                                                       */
/*                                                                                            */
/*  This program is distributed in the hope that it will be useful,                           */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of                            */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                             */
/*  GNU General Public License for more details.                                              */
/*                                                                                            */
/*  You should have received a copy of the GNU General Public License                         */
/*  along with this program; if not, write to the Free Software                               */
/*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA            */
/**********************************************************************************************/

/*
 * Original code from the Cilk project (by Keith Randall)
 *
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2000 Matteo Frigo
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "tasking.h"
#include "async.h"
#include "wtime.h"

static char *example_solution = NULL;

/*
 * <a> contains array of <n> queen positions.
 * Returns 1 if none of the queens conflict, and 0 otherwise.
 */
bool ok(int n, char *a)
{
     int i, j;
     char p, q;

     for (i = 0; i < n; i++) {
		 p = a[i];

		 for (j = i + 1; j < n; j++) {
			 q = a[j];
			 if (q == p || q == p - (j - i) || q == p + (j - i)) {
				 return false;
			 }
		 }
	 }

     return true;
}

int nqueens_seq(int n, int j, char *a)
{
	int count = 0, i;

	if (n == j) {
		/* Good solution, count it */
		example_solution = malloc(n * sizeof(char));
		memcpy(example_solution, a, n * sizeof(char));
		return 1;
	}

	/* Try each possible position for queen <j> */
	for (i = 0; i < n; i++) {
		a[j] = (char)i;
		if (ok(j + 1, a)) {
			count += nqueens_seq(n, j + 1, a);
		}
	}

	return count;
}

int nqueens_async(int, int, char *);

FUTURE_DECL_FREELIST(int);
FUTURE_DECL(int, nqueens_async, int n; int j; char *a, n, j, a);

int nqueens_async(int n, int j, char *a)
{
	int count = 0, i;
	future local_counts[n];

	if (n == j) {
		/* Good solution, count it */
		return 1;
	}

	memset(local_counts, 0, n * sizeof(future));

	/* Try each possible position for queen <j> */
	for (i = 0; i < n; i++) {
		char *b = alloca((j + 1) * sizeof(char));
		memcpy(b, a, j * sizeof(char));
		b[j] = (char)i;
		if (ok(j + 1, b)) {
			local_counts[i] = __ASYNC(nqueens_async, n, j + 1, b);
		}
	}

	for (i = 0; i < n; i++) {
		if (local_counts[i] != NULL) {
			count += __AWAIT(local_counts[i], int);
		}
	}

	return count;
}

int nqueens_spawn(int, int, char *);

TASK_DECL(int, nqueens_spawn, int n; int j; char *a, n, j, a);

int nqueens_spawn(int n, int j, char *a)
{
	int count = 0, i;
	int local_counts[n];
	atomic_t num_children = 0;

	if (n == j) {
		/* Good solution, count it */
		return 1;
	}

	memset(local_counts, 0, n * sizeof(int));

	/* Try each possible position for queen <j> */
	for (i = 0; i < n; i++) {
		char *b = alloca((j + 1) * sizeof(char));
		memcpy(b, a, j * sizeof(char));
		b[j] = (char)i;
		if (ok(j + 1, b)) {
			SPAWN(nqueens_spawn, n, j + 1, b, &local_counts[i]);
		}
	}

	SYNC;

	for (i = 0; i < n; i++) {
		count += local_counts[i];
	}

	return count;
}

void verify_queens(int n, int res)
{
	// Number of solutions for placing n queens on an n x n board
	static int solutions[] = {
        1,    // 1
        0,
        0,
        2,
        10,   // 2
        4,
        40,
        92,
        352,
        724,  // 10
        2680,
        14200,
        73712,
        365596,
	};

#define MAX_SOLUTIONS ((int)(sizeof(solutions)/sizeof(int)))

	if (n > MAX_SOLUTIONS) {
		printf("Cannot verify result: n out of range, %d\n", n);
		return;
	}

	if (res != solutions[n-1]) {
		printf("N-Queens failed: %d != %d\n",res, solutions[n-1]);
		return;
	}
}

int main(int argc, char *argv[])
{
	double start, end;
	int count = 0, n;

	if (argc != 2) {
		printf("Usage: %s <number of queens>\n", argv[0]);
		exit(0);
	}

	n = atoi(argv[1]);
	if (n <= 0) {
		printf("Number of queens must be greater than 0\n");
		exit(0);
	}

	TASKING_INIT(&argc, &argv);

	start = Wtime_msec();
	count = nqueens_async(n, 0, alloca(n * sizeof(char)));
	end = Wtime_msec();
	verify_queens(n, count);

	if (example_solution != NULL) {
		int i;
		printf("Example solution: ");
		for (i = 0; i < n; i++) {
			printf("%2d ", example_solution[i]);
		}
		printf("\n");
	}

	printf("Elapsed wall time: %.2lf ms\n", end-start);

	// This should be moved inside TASKING_EXIT()
	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
