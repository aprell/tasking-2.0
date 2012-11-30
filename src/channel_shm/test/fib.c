#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include "channel.h"
#include "utest.h"
UTEST_MAIN() {}

#define WORKER(id) if (ID == (id))
#define MASTER WORKER(0)
#define NUM_WORKERS 3

#define channel_send(c, d, s) \
{ \
	while (!channel_send(c, d, s)) pthread_yield(); \
	if (channel_unbuffered(c)) { \
		/* Wait until value has been received */ \
		while (channel_peek(c)) pthread_yield(); \
	} \
}

#define channel_receive(c, d, s) \
{ \
	while (!channel_receive(c, d, s)) pthread_yield(); \
}

static Channel *chan1, *chan2, *chan3;

static void produce_numbers(void)
{
	int fib = 0, f1 = 1, f2 = 0, i;
	int n;

	channel_receive(chan2, &n, sizeof(n));

	for (i = 0; i <= n; i++) {
		if (i < 2) {
			channel_send(chan3, &i, sizeof(i));
			continue;
		}
		fib = f1 + f2;
		f2 = f1;
		f1 = fib;
		channel_send(chan3, &fib, sizeof(fib));
	}
}

static void print_numbers(int n)
{
	int fib, i;
	bool done = true;

	channel_send(chan2, &n, sizeof(n));

	for (i = 0; i <= n; i++) {
		channel_receive(chan3, &fib, sizeof(fib));
		printf("fib(%d) = %d\n", i, fib);
	}

	channel_send(chan1, &done, sizeof(done));
}

static void *thread_func(void *args)
{
	int ID = *(int *)args;
	
	WORKER(1) print_numbers(42);

	WORKER(2) produce_numbers();

	return NULL;
}

int main(void)
{
	pthread_t threads[NUM_WORKERS];
	int IDs[NUM_WORKERS], i;
	bool done = false;

	chan1 = channel_alloc(sizeof(bool), 0);
	chan2 = channel_alloc(sizeof(int), 0);
	chan3 = channel_alloc(sizeof(int), 5);

	IDs[0] = 0;

	for (i = 1; i < NUM_WORKERS; i++) {
		IDs[i] = i;
		pthread_create(&threads[i], NULL, thread_func, &IDs[i]);
	}

	channel_receive(chan1, &done, sizeof(done));
	assert(done);

	for (i = 1; i < NUM_WORKERS; i++) {
		pthread_join(threads[i], NULL);
	}

	channel_free(chan1);
	channel_free(chan2);
	channel_free(chan3);

	return 0;
}
