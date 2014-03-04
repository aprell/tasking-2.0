#ifndef CHANREF_H
#define CHANREF_H

#include <assert.h>
#include "channel.h"
#ifdef CHAN_SCC
#include "MPB.h"
#endif

#ifdef CHAN_SHM
typedef Channel *chanref_t;
#elif defined CHAN_SCC
typedef struct {
	int owner, offset;
} chanref_t;
#else
#error "Need to specify platform. Options: CHAN_SHM, CHAN_SCC"
#endif

typedef chanref_t chan;

// Set c to contain pointer to channel p
static inline void chanref_set(chanref_t *c, Channel *p)
{
	assert(c != NULL);
#ifdef CHAN_SHM
	*c = p;
#elif defined CHAN_SCC
	if (p == NULL) {
		c->owner = c->offset = -1;
	} else {
		c->owner = channel_owner(p);
		c->offset = MPB_data_offset(c->owner, p);
	}
#endif
}

#include "overload_chanref_init.h"

// Initialize c with default value
static inline void chanref_init(chanref_t *c)
{
	chanref_set(c, NULL);
}

// Initialize c with p
static inline void chanref_init(chanref_t *c, Channel *p)
{
	chanref_set(c, p);
}

// Extract pointer to channel from c
static inline Channel *chanref_get(chanref_t c)
{
#ifdef CHAN_SHM
	return c;
#elif defined CHAN_SCC
	return (Channel *)MPB_data_ptr(c.owner, c.offset);
#endif
}

static inline void chanref_inspect(chanref_t c)
{
#ifdef CHAN_SHM
	printf("[c = %p]\n", c);
#elif defined CHAN_SCC
	printf("[owner = %d, offset = %d]\n", c.owner, c.offset);
#endif
}

#endif // CHANREF_H
