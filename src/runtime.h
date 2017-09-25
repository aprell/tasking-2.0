#ifndef RUNTIME_H
#define RUNTIME_H

#include "tasking_internal.h"
#include "channel.h"
#include "async.h"

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
#ifdef LAZY_FUTURES
void RT_force_lazy_future(lazy_future *f, void *data, unsigned int size);
#endif

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
