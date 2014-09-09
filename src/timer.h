#ifndef TIMER_H
#define TIMER_H

#ifdef NTIME

#define timer_new(...)		((void)0)
#define timer_start(x)		((void)0)
#define timer_end(x)		((void)0)
#define timer_elapsed(...) 	((void)0)
#define timers_elapsed(...) ((void)0)

#else

#include <stdarg.h>

#define CYCLES_PER_SEC(t)	((t) * 1e9)
#define CYCLES_PER_MSEC(t)	((t) * 1e6)
#define CYCLES_PER_USEC(t)	((t) * 1e3)

typedef struct timer {
	unsigned long long start, end;
	unsigned long long elapsed;
	double GHZ;
} mytimer_t;

enum {
	timer_us, timer_ms, timer_s
};

static inline unsigned long long getticks(void)
{
	unsigned int lo, hi;
	// RDTSC copies contents of 64-bit TSC into EDX:EAX
	asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
	return (unsigned long long)hi << 32 | lo;
}

// Resets the timer
static inline void timer_new(mytimer_t *timer, double GHZ)
{
	timer->elapsed = 0;
	timer->GHZ = GHZ;
}

#define timer_reset(t, f) timer_new(t, f)

static inline void timer_start(mytimer_t *timer)
{
	timer->start = getticks();
}

static inline void timer_end(mytimer_t *timer)
{
	timer->end = getticks();
	timer->elapsed += timer->end - timer->start;
}

// Returns elapsed time in microseconds, milliseconds, or seconds
static inline double timer_elapsed(mytimer_t *timer, int opt)
{
	double elapsed = -1.0;

	switch (opt) {
	case timer_us: // cycles -> microseconds
		elapsed = timer->elapsed / CYCLES_PER_USEC(timer->GHZ);
		break;
	case timer_ms: // cycles -> milliseconds
		elapsed = timer->elapsed / CYCLES_PER_MSEC(timer->GHZ);
		break;
	case timer_s:  // cycles -> seconds
		elapsed = timer->elapsed / CYCLES_PER_SEC(timer->GHZ);
		break;
	default:
		break;
	}

	return elapsed;
}

// Collects elapsed time from a variable number of timers
// The list of arguments is expected to end with NULL
static inline double timers_elapsed(mytimer_t *timer, int opt, ...)
{
	double elapsed;
	va_list args;
	mytimer_t *t;

	va_start(args, opt);

	elapsed = timer_elapsed(timer, opt);
	while ((t = va_arg(args, mytimer_t *)) != NULL) {
		elapsed += timer_elapsed(t, opt);
	}

	va_end(args);

	return elapsed;
}

// Returns elapsed time in number of clock cycles
static inline unsigned long long timer_cycles(mytimer_t *timer)
{
	return timer->elapsed;
}

#endif // NTIME

#endif // TIMER_H
