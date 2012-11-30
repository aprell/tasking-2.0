#ifndef WTIME_H
#define WTIME_H

#include <sys/time.h>
#include <time.h>

// Number of seconds since the Epoch
static inline double Wtime_sec(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

// Number of milliseconds since the Epoch
static inline double Wtime_msec(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1e3 + tv.tv_usec / 1e3;
}

// Number of microseconds since the Epoch
static inline double Wtime_usec(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1e6 + tv.tv_usec;
}

// Read time stamp counter on x86
static inline unsigned long long readtsc(void)
{
	unsigned int lo, hi;
	// RDTSC copies contents of 64-bit TSC into EDX:EAX
	asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
 	return (unsigned long long)hi << 32 | lo;
}

#endif // WTIME_H
