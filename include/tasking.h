#ifndef TASKING_H
#define TASKING_H

#include "runtime.h"

#define TASKING_INIT(argc, argv) \
do { \
	tasking_init(argc, argv); \
	/* RT_helper_create(); */\
} while (0)

#define TASKING_EXIT() \
do { \
	tasking_exit_signal(); \
	/* RT_helper_join(); */\
	tasking_exit(); \
} while (0)

// Only the master thread can enter a barrier
#define TASKING_BARRIER() \
do { \
	MASTER RT_barrier(); \
} while (0)

#define TASKING_WAIT() \
do { \
	fprintf(stderr, "Warning: TASKING_WAIT() not implemented!\n"); \
	abort(); \
} while (0)

// r is a pointer to the result value
#define TASKING_FORCE_FUTURE(c, r) \
do { \
	typeof(*(r)) __tmp; \
	RT_force_future_channel(c, &__tmp, sizeof(__tmp)); \
	*(r) = __tmp; \
} while (0)

extern PRIVATE int ID;

int tasking_init(int *argc, char ***argv);
int tasking_exit_signal(void);
int tasking_exit(void);

#endif // TASKING_H
