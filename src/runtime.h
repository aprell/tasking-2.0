#ifndef RUNTIME_H
#define RUNTIME_H

#include "future.h"
#include "task.h"

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

enum RT_async_action_t {
	RT_EXIT // Ask workers to shut down
};

void RT_async_action(enum RT_async_action_t);

Task *RT_task_alloc(void);
void RT_push(Task *task);
void RT_force_future(future f, void *data, unsigned int size);

// Poll for incoming steal requests and handle them if possible
// Example: Polling on loop back edges with
// for (i = 0; i < n; i++, POLL()) ...
#define POLL() RT_poll()
void RT_poll(void);

#endif // RUNTIME_H
