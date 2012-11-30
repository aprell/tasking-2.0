#ifndef TIMER_H
#define TIMER_H

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

#endif // TIMER_H
