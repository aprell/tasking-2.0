#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "channel.h"

#define WORKER(id) if (ID == (id))
#define MASTER WORKER(0)
#define NUM_WORKERS 8

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

static Channel *chans[NUM_WORKERS];
static pthread_barrier_t barrier;

static void *thread_func(void *args)
{
	int ID = *(int *)args;
	int next, prev;
	int buf;

	next = (ID + 1) % NUM_WORKERS;
	prev = (ID - 1 + NUM_WORKERS) % NUM_WORKERS;
	chans[ID] = channel_alloc(sizeof(int), 1);
	pthread_barrier_wait(&barrier);

	channel_send(chans[next], &ID, sizeof(ID));
	channel_receive(chans[ID], &buf, sizeof(buf));
	printf("Worker %d: %d\n", ID, buf);
	
	channel_free(chans[ID]);

	return NULL;
}

int main(void)
{
	pthread_t threads[NUM_WORKERS];
	int IDs[NUM_WORKERS], i;

	pthread_barrier_init(&barrier, NULL, NUM_WORKERS);

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

	return 0;
}
