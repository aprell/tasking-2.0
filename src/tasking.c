#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "tasking_internal.h"
#include "tasking.h"

int tasking_init(int *argc, char ***argv)
{
	tasking_internal_init(argc, argv);
	RT_init();
	tasking_internal_barrier();

	return 0;
}

int tasking_exit_signal(void)
{
	return tasking_internal_exit_signal();
}

int tasking_exit(void)
{
	tasking_internal_barrier();
	tasking_internal_statistics();
	RT_exit();
	tasking_internal_exit();

	return 0;
}
