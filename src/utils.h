#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <pthread.h>
#include "platform.h"

extern PRIVATE int ID;

static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

static inline void print_field(int *a, int len)
{
	int i;

	pthread_mutex_lock(&print_mutex);

	printf("Worker %2d: ", ID);

	printf("[");

	for (i = 0; i < len; i++) {
		if (i > 0) printf(", ");
		printf("%d", a[i]);
	}

	printf("]\n");

	pthread_mutex_unlock(&print_mutex);
}

#endif // UTILS_H
