#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdbool.h>
#include "overload_channel_alloc.h"

typedef struct SHM_channel Channel;

struct SHM_channel;

enum {
	channel_UNBUF = 0, channel_B = 0,	// Unbuffered (blocking) channel
	channel_BUF = 1, channel_NB = 1		// Buffered (nonblocking) channel
};

// Different channel implementations, in order of decreasing generality
enum { 
	MPMC, // multiple producer, multiple consumer
	MPSC, // multiple producer, single consumer
	SPSC  // single producer, single consumer
};

/*****************************************************************************
 * Channel API
 *****************************************************************************/

Channel *channel_alloc(unsigned int size, unsigned int n, int impl);
Channel *channel_alloc(unsigned int size, unsigned int n); // MPMC

void channel_free(Channel *chan);

// Send size bytes of data over chan 
bool channel_send(Channel *chan, void *data, unsigned int size);

// Receive size bytes of data from chan
bool channel_receive(Channel *chan, void *data, unsigned int size);

int channel_owner(Channel *chan);

// Is the channel buffered or unbuffered?
bool channel_buffered(Channel *chan);
#define channel_unbuffered(chan) (!channel_buffered(chan))

// Returns the number of buffered items
// A value of 1 for unbuffered channels means that the sender is blocked
unsigned int channel_peek(Channel *chan);

// Returns the channel's capacity
// Always 0 for unbuffered channels
unsigned int channel_capacity(Channel *chan);

// Returns channel implementation
int channel_impl(Channel *chan);

//void channel_inspect(Channel *chan);

// Closes channel chan, meaning no further values can be sent
bool channel_close(Channel *chan);

// Opens channel chan, meaning more values can be sent
//bool channel_open(Channel *chan);

// Returns true if channel is closed, false otherwise
bool channel_closed(Channel *chan);

#define unique_name_paste(id, n) id ##_## n
#define unique_name(id, n) unique_name_paste(id, n)

// Iterate through all values received on channel c
#define channel_range(c, p) \
	bool unique_name(__got_val_from_chan, __LINE__); \
unique_name(L, __LINE__): \
	unique_name(__got_val_from_chan, __LINE__) = false; \
	/* As long as the channel is not closed and nonempty */ \
	while (!channel_closed(c) || channel_peek(c)) { \
		if (!channel_receive(c, p, sizeof(*(p)))) { \
			continue; \
		} else { \
			unique_name(__got_val_from_chan, __LINE__) = true; \
			break; \
		} \
	} \
	/* Use of goto in for requires a statement expression */ \
	for (; unique_name(__got_val_from_chan, __LINE__); ({ goto unique_name(L, __LINE__); }))

#endif // CHANNEL_H
