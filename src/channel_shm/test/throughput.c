#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include "wtime.h"
#include "channel.h"
#include "utest.h"
UTEST_MAIN() {}

#define NUM_WORKERS 2
#define N 1000000

static pthread_barrier_t barrier;
static Channel *chan;
typedef struct data { int d[8]; } Data;

static void *thread_func(void *args)
{
	int ID = *(int *)args, i;
	double start, end;

	pthread_barrier_wait(&barrier);

	if (ID == 0) {
		// Produce
		start = Wtime_msec();
		for (i = 0; i < N; i++) {
			Data d = { .d[0] = i };
			while (!channel_send(chan, &d, sizeof(d))) pthread_yield();
		}
		end = Wtime_msec();
		printf("Producer throughput: %.2lf\n", N / (end - start));
	}

	if (ID == 1) {
		// Consume
		start = Wtime_msec();
		for (i = 0; i < N; i++) {
			Data d;
			while (!channel_receive(chan, &d, sizeof(d))) pthread_yield();
			assert(d.d[0] == i);
		}
		end = Wtime_msec();
		printf("Consumer throughput: %.2lf\n", N / (end - start));
	}

	return NULL;
}

int main(void)
{
	pthread_t threads[NUM_WORKERS];
	int IDs[NUM_WORKERS], i;

	pthread_barrier_init(&barrier, NULL, NUM_WORKERS);
	chan = channel_alloc(sizeof(Data), 100, SPSC);

	IDs[0] = 0;

	for (i = 1; i < NUM_WORKERS; i++) {
		IDs[i] = i;
		pthread_create(&threads[i], NULL, thread_func, &IDs[i]);
	}

	thread_func(&IDs[0]);

	for (i = 1; i < NUM_WORKERS; i++) {
		pthread_join(threads[i], NULL);
	}

	pthread_barrier_destroy(&barrier);
	channel_free(chan);

	return 0;
}
