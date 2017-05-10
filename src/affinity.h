#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <syscall.h>

#include "overload_set_thread_affinity.h"

static inline void set_thread_affinity(pthread_t t, int cpu)
{
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);
	pthread_setaffinity_np(t, sizeof(cpu_set_t), &cpuset);
}

static inline void set_thread_affinity(int cpu)
{
	return set_thread_affinity(pthread_self(), cpu);
}

static inline int get_thread_affinity(void)
{
	cpu_set_t cpuset;
	int i;

	pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	for (i = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, &cpuset))
			break;
	}

	return i;
}

static inline int cpu_count(void)
{
	cpu_set_t cpuset;

	// Requires unrestricted affinity!
	pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	return CPU_COUNT(&cpuset);
}

static inline void print_thread_affinity(void)
{
	cpu_set_t cpuset;
	char buf[1000];
	int i;

	pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	// Thread-ID (Linux)
	sprintf(buf, "Thread %lu: [ ", syscall(SYS_gettid));

	for (i = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, &cpuset))
			sprintf(buf + strlen(buf), "%d ", i);
	}

	sprintf(buf + strlen(buf), "]");
	printf("%s\n", buf);
}
