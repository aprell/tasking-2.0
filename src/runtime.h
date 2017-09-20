#ifndef RUNTIME_H
#define RUNTIME_H

#include "tasking_internal.h"
#include "channel.h"
#include "overload_RT_force_future_channel.h"
#include "async.h"

typedef struct partition {
	int number;			// index of partition: 0 <= number < num_partitions
	int manager;		// ID of partition manager
	int *workers;		// pointer to statically defined workers
	int num_workers;	// number of statically defined workers
	int num_workers_rt; // number of workers at runtime
} partition_t;

// Set up static storage for up to n partitions
#define PARTITION_INIT(n) \
static PRIVATE partition_t partitions[n]; \
static PRIVATE int num_partitions, max_num_partitions = n; \
static PRIVATE partition_t *my_partition; \
static PRIVATE bool is_manager; \
/* Only defined for managers: */ \
/* The next manager in the logical chain of managers */ \
static PRIVATE int next_manager; \
static PRIVATE int next_worker; \
\
/* Determine the "home partition" of worker ID */ \
/* We assume that a manager is also contained in its partition, so this */ \
/* routine works for both managers and regular workers */ \
static void PARTITION_SET(void) \
{ \
	int i; \
	for (i = 0; i < num_partitions; i++) { \
		partition_t *p = &partitions[i]; \
		int *w; \
		foreach_worker(w, p) { \
			if (*w == ID) { \
				my_partition = p; \
				if (*w == p->manager) { \
					is_manager = true; \
					if (i == num_partitions-1) \
						next_manager = partitions[0].manager; \
					else \
						next_manager = partitions[i+1].manager; \
				} \
				if (*(w + 1) == -1) { \
					next_worker = p->workers[0]; \
					return; \
				} \
				if (*(w + 1) < num_workers) \
					next_worker = *(w + 1); \
				else \
					next_worker = p->workers[0]; \
				return; \
			} \
		} \
	} \
} \

// Requires that PARTITION_INIT() was called
#define PARTITION_RESET() \
	num_partitions = 0; \
	my_partition = NULL; \
	is_manager = false; \
	next_manager = 0; \
	next_worker = 0;

// Create a partition of n workers with identifier id
// For example:
// PARTITION_CREATE(abc, 4) = { 2, 4, 6, 8 };
//
// At runtime, we can assign the partition to a manager:
// PARTITION_ASSIGN_abc(4);
// But we need to make sure the manager is contained in the partition!
// If the manager happens to be not available at runtime, we try to find
// another worker that can fill this role within the same partition.
// If no worker is available, the partition is unused.
#define PARTITION_CREATE(id, n) \
static PRIVATE int partition_##id[]; \
\
static void PARTITION_ASSIGN_##id(int manager) \
{ \
	if (num_partitions < max_num_partitions) { \
		if (manager < num_workers) { \
			partition_t *p = &partitions[num_partitions]; \
			p->number = num_partitions; \
			p->manager = manager; \
			p->workers = partition_##id; \
			p->num_workers = n; \
			p->num_workers_rt = 0; \
			num_partitions++; \
		} else { \
			int i; \
			for (i = 0; i < n; i++) { \
				if (partition_##id[i] < num_workers) { \
					partition_t *p = &partitions[num_partitions]; \
					p->number = num_partitions; \
					p->manager = partition_##id[i]; \
					p->workers = partition_##id; \
					p->num_workers = n; \
					p->num_workers_rt = 0; \
					num_partitions++; \
					/*LOG("Changing manager from %d to %d\n", manager, p->manager);*/ \
					return; \
				} \
			} \
			/*LOG("Partition %d is empty\n", num_partitions+1);*/ \
		} \
	} \
} \
\
static PRIVATE int partition_##id[]

// For each worker w in partition p
// For example:
// int *w;
// foreach_worker(w, my_partition)
//     printf("Worker %d\n", *w);
#define foreach_worker(w, p) \
	assert((p) != NULL); \
	int i__; \
	for (i__ = 0, w = (p)->workers; i__ < (p)->num_workers; i__++, w++)

#ifndef max
#define max(a, b) \
({ \
       typeof(a) __a = (a); \
       typeof(b) __b = (b); \
       __a > __b ? __a : __b; \
})
#endif

#ifndef min
#define min(a, b) \
({ \
       typeof(a) __a = (a); \
       typeof(b) __b = (b); \
       __a < __b ? __a : __b; \
})
#endif

int RT_init();
int RT_exit(void);
int RT_schedule(void);
int RT_barrier(void);
void RT_force_future(Channel *chan, void *data, unsigned int size);
void RT_force_future_channel(Channel *chan, void *data, unsigned int size);
void RT_force_future_channel(Channel *chan);
#ifdef LAZY_FUTURES
void RT_force_lazy_future(lazy_future *f, void *data, unsigned int size);
#endif
void RT_taskwait(atomic_t *num_children);

// These functions implement the load balancing between workers
void push(Task *task);
Task *pop(void);
Task *pop_child(void);

Task *task_alloc(void);

// This function implements support for loop tasks and work-splitting
bool RT_loop_split(void);

// For user code: poll for incoming steal requests and handle them if possible
int RT_check_for_steal_requests(void);

#endif // RUNTIME_H
