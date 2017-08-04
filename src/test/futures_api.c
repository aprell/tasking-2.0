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

#define concat_(x, y) x##y
#define concat(x, y) concat_(x, y)

struct future_ {
	future f;
	void *ret;
	void (*await)(struct future_ *);
};

// Must be generated
static void await_sum(struct future_ *this)
{
	AWAIT(this->f, (int *)this->ret);
}

struct node {
	struct future_ f;
	struct node *next;
};

static inline void await_tasks(struct node *hd)
{
	struct node *n;
	for (n = hd; n != NULL; n = n->next) {
		n->f.await(&n->f);
	}
}

#define ASYNC_(fun, ...) fun, __VA_ARGS__

#define SCOPED(async, addr) SCOPED_ASYNC(addr, async)

#define SCOPED_ASYNC(ret, fun, ...) \
	struct node *concat(hd_, __LINE__) = alloca(sizeof(struct node)); \
	*concat(hd_, __LINE__) = (struct node){ (struct future_){ __ASYNC(fun, __VA_ARGS__), ret, await_##fun }, hd }; \
	hd = concat(hd_, __LINE__)

#define SCOPED_AWAIT \
	for (struct node *hd = NULL; hd == NULL || (await_tasks(hd), 0);)

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

	int x, y, z;

	SCOPED_AWAIT {
		SCOPED(ASYNC_(sum, 0, 1), &x);
		SCOPED(ASYNC_(sum, 1, 2), &y);
		SCOPED(ASYNC_(sum, 2, 3), &z);
	}

	assert(x == 1);
	assert(y == 3);
	assert(z == 5);

	//========================================================================

	// This should be moved inside TASKING_EXIT()
	TASKING_BARRIER();
	TASKING_EXIT();

	return 0;
}
