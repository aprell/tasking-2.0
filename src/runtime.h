#ifndef RUNTIME_H
#define RUNTIME_H

#include "future_internal.h"
#include "tasking_internal.h"

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
void RT_force_future(future f, void *data, unsigned int size);

// These functions implement the load balancing between workers
void push(Task *task);
Task *pop(void);
Task *pop_child(void);

Task *task_alloc(void);

// For user code: poll for incoming steal requests and handle them if possible
void RT_poll(void);

#endif // RUNTIME_H
