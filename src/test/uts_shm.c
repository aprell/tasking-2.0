#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tasking.h"
#include "uts.h"

#define MAX_THREADS		  	256
#define GET_NUM_THREADS		num_workers
#define GET_THREAD_NUM 		ID

/***********************************************************
 *  Parallel execution parameters                          *
 ***********************************************************/

int doSteal   = 1; 		  // 1 => use work stealing
int chunkSize = -1;       // number of nodes to move to/from shared area

// Worker threads accumulate counted nodes locally

struct node_count {
	unsigned long long n;
	char _[128 - sizeof(unsigned long long)];
};

static struct node_count numNodes[MAX_THREADS];

/***********************************************************
 *  UTS Implementation Hooks                               *
 ***********************************************************/

// Return a string describing this implementation
char * impl_getName()
{
	return "Pthreads";
}

// construct string with all parameter settings
int impl_paramsToStr(char *strBuf, int ind)
{
	ind += sprintf(strBuf+ind, "Execution strategy:  ");
	ind += sprintf(strBuf+ind, "Parallel search using %d threads\n", GET_NUM_THREADS);

	return ind;
}

int impl_parseParam(char *param, char *value)
{
	int err = 0;  // Return 0 on a match, nonzero on an error

	switch (param[1]) {
		case 'c':
			chunkSize = atoi(value); break;
		case 's':
			doSteal = atoi(value);
			if (doSteal != 1 && doSteal != 0)
				err = 1;
			break;
		default:
			err = 1;
			break;
	}

	return err;
}

void impl_helpMessage()
{
	printf("   -s  int   zero/nonzero to disable/enable work stealing\n");
	printf("   -c  int   chunksize for work stealing\n");
}

void impl_abort(int err)
{
	exit(err);
}

void initNode(Node * child)
{
	child->type = -1;
	child->height = -1;
	child->numChildren = -1;    // not yet determined
}

void initRootNode(Node * root, int type)
{
	uts_initRoot(root, type);
}

// Display search statistics
void showStats(double elapsed)
{
	int tnodes = 0, tleaves = 0, mheight = 0;
	int i;

	// combine measurements from all threads
	for (i = 0; i < GET_NUM_THREADS; i++) {
		tnodes += numNodes[i].n;
	}

	uts_showStats(GET_NUM_THREADS, chunkSize, elapsed, tnodes, tleaves, mheight);
}

// Sequential search of UTS trees
// Note: tree size is measured by the number of push operations
void seqTreeSearch(Node parent)
{
	Node child;
	int numChildren;
	int i, j;

	numChildren = uts_numChildren(&parent);

	// Create numChildren tasks
	for (i = 0; i < numChildren; i++) {
		initNode(&child);
		child.type = uts_childType(&parent);
		child.height = parent.height + 1;

		// The following line is the work (one or more SHA-1 ops)
		for (j = 0; j < computeGranularity; j++) {
			rng_spawn(parent.state.state, child.state.state, i);
		}

		seqTreeSearch(child);
	}

	numNodes[GET_THREAD_NUM].n += numChildren;
}

void parTreeSearch(Node);

DEFINE_ASYNC(parTreeSearch, (Node));

// Parallel search of UTS trees using work-stealing
// Note: tree size is measured by the number of push operations
void parTreeSearch(Node parent)
{
	Node child;
	int numChildren;
	int i, j;

	numChildren = uts_numChildren(&parent);

	// Create numChildren tasks
	for (i = 0; i < numChildren; i++) {
		initNode(&child);
		child.type = uts_childType(&parent);
		child.height = parent.height + 1;

		// The following line is the work (one or more SHA-1 ops)
		for (j = 0; j < computeGranularity; j++) {
			rng_spawn(parent.state.state, child.state.state, i);
		}

		ASYNC(parTreeSearch, (child));
	}

	numNodes[GET_THREAD_NUM].n += numChildren;
}

int main(int argc, char *argv[])
{
	Node root;
	double t1, t2;

	TASKING_INIT(&argc, &argv);

	uts_parseParams(argc, argv);
	uts_printParams();

	uts_initRoot(&root, type);
	numNodes[MASTER_ID].n = 1;

	t1 = uts_wctime();

	parTreeSearch(root);

	TASKING_BARRIER();

	t2 = uts_wctime();

	showStats(t2-t1);

	TASKING_EXIT();

	return 0;
}
