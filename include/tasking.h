#ifndef TASKING_H
#define TASKING_H

#include "async.h"
#include "future.h"

#define TASKING_INIT(argc, argv) \
do { \
	tasking_init(argc, argv); \
} while (0)

#define TASKING_EXIT() \
do { \
	TASKING_BARRIER(); \
	tasking_exit(); \
} while (0)

// Only the master thread can execute a barrier
#define TASKING_BARRIER() \
do { \
	tasking_barrier(); \
} while (0)

int tasking_init(int *, char ***);
int tasking_exit(void);
int tasking_barrier(void);

#endif // TASKING_H
