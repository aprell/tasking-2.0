#ifndef WORKER_TREE_H
#define WORKER_TREE_H

#include <assert.h>

typedef struct worker_tree WorkerTree;

struct worker_tree {
	int left_child;
	int right_child;
	int parent;
	int num_children;
	bool left_subtree_is_idle;
	bool right_subtree_is_idle;
	// When a worker has become quiescent and backs off from stealing after
	// both left_subtree_is_idle == true and right_subtree_is_idle == true, it
	// waits for tasks from its parent.
	bool waiting_for_tasks;
};

static inline int left_child(int ID, int maxID)
// requires ID >= 0 && maxID >= 0
// requires ID <= maxID
{
	assert(ID >= 0 && maxID >= 0);
	assert(ID <= maxID);

	int child = 2*ID + 1;

	return child <= maxID ? child : -1;
}

static inline int right_child(int ID, int maxID)
// requires ID >= 0 && maxID >= 0
// requires ID <= maxID
{
	assert(ID >= 0 && maxID >= 0);
	assert(ID <= maxID);

	int child = 2*ID + 2;

	return child <= maxID ? child : -1;
}

static inline int parent(int ID)
// requires ID >= 0
// ensures ID == 0 ==> \result == -1
// ensures ID > 0 ==> \result >= 0
{
	assert(ID >= 0);

	return ID % 2 != 0 ? (ID - 1) / 2 : (ID - 2) / 2;
}

static inline void worker_tree_init(struct worker_tree *tree, int ID, int maxID)
// requires worker_tree != NULL
// requires ID >= 0 && maxID >= 0
// requires ID <= maxID
{
	assert(tree != NULL);
	assert(ID >= 0 && maxID >= 0);
	assert(ID <= maxID);

	tree->left_child = left_child(ID, maxID);
	tree->right_child = right_child(ID, maxID);
	tree->parent = parent(ID);
	tree->num_children = 0;
	tree->left_subtree_is_idle = false;
	tree->right_subtree_is_idle = false;
	tree->waiting_for_tasks = false;

	if (tree->left_child != -1) tree->num_children++;
	else tree->left_subtree_is_idle = true; // always

	if (tree->right_child != -1) tree->num_children++;
	else tree->right_subtree_is_idle = true; // always

	// A few sanity checks
	assert(tree->left_child != ID && tree->right_child != ID);
	assert(0 <= tree->num_children && tree->num_children <= 2);
	assert((tree->parent == -1 && ID == 0) || (tree->parent >= 0 && ID > 0));
}

#endif // WORKER_TREE_H
