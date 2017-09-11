#ifndef FUTURE_INTERNAL_H
#define FUTURE_INTERNAL_H

#include <assert.h>
#include <stdbool.h>
#include "channel.h"

#ifdef LAZY_FUTURES

typedef struct {
	union {
		Channel *chan;               // <--+
		char buf[sizeof(Channel *)]; //    | <--+
	};                               //    |    |
	bool has_channel;                // ---+    |
	bool set;                        // --------+
} lazy_future;

typedef lazy_future *future;

// Allocated in the stack frame of the caller
#define FUTURE_ALLOC(_, task) \
({ \
	future __f = alloca(sizeof(lazy_future)); \
	(task)->has_future = true; \
	__f->chan = NULL; \
	__f->has_channel = false; \
	__f->set = false; \
	__f; \
})

#define FUTURE_SET(fut, res) \
do { \
	if (!(fut)->has_channel) { \
		*(typeof(&(res)))(fut)->buf = (res); \
		(fut)->set = true; \
	} else { \
		assert((fut)->chan != NULL); \
		channel_send((fut)->chan, &(res), sizeof(res)); \
	} \
} while (0)

extern void RT_force_lazy_future(lazy_future *, void *, unsigned int);

#define FUTURE_GET(fut, res) RT_force_lazy_future(fut, res, sizeof(*res))

#else // Regular, eagerly allocated futures

typedef Channel *future;

#define FUTURE_ALLOC(fun, _) fun##_channel()

#define FUTURE_SET(fut, res) channel_send(fut, &(res), sizeof(res))

extern void RT_force_future_channel(Channel *, void *, unsigned int);

#define FUTURE_GET(fut, res) \
do { \
	RT_force_future_channel(fut, res, sizeof(*(res))); \
	channel_free(fut); \
} while (0)

#endif // LAZY_FUTURES

// Scoped futures are wrapped in a structure and collected in a list

struct future_node {
	future f;
	void *r;
	void (*await)(struct future_node *);
	struct future_node *next;
};

static inline void await_future_nodes(struct future_node *hd)
{
	struct future_node *n;
	for (n = hd; n != NULL; n = n->next) {
		n->await(n);
	}
}

#endif // FUTURE_INTERNAL_H
