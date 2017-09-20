#ifndef TASKING_H
#define TASKING_H

#include "runtime.h"

#define TASKING_INIT(argc, argv) \
do { \
	tasking_init(argc, argv); \
} while (0)

#define TASKING_EXIT() \
do { \
	tasking_exit_signal(); \
	tasking_exit(); \
} while (0)

// Only the master thread can enter a barrier
#define TASKING_BARRIER() \
do { \
	MASTER RT_barrier(); \
} while (0)

extern PRIVATE int ID;

int tasking_init(int *argc, char ***argv);
int tasking_exit_signal(void);
int tasking_exit(void);

#endif // TASKING_H
