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
#define FUTURE_ALLOC(_) \
({ \
	future __f = alloca(sizeof(lazy_future)); \
	__f->chan = NULL; \
	__f->has_channel = false; \
	__f->set = false; \
	__f; \
})

#define FUTURE_SET(fut, res) \
do { \
	if (!(fut)->has_channel) { \
		memcpy((fut)->buf, &res, sizeof(res)); \
		(fut)->set = true; \
	} else { \
		assert((fut)->chan != NULL); \
		channel_send((fut)->chan, &(res), sizeof(res)); \
	} \
} while (0)

extern void RT_force_lazy_future(lazy_future *, void *, unsigned int);

#define FUTURE_GET(fut, res, ty) RT_force_lazy_future(fut, res, sizeof(ty))

#define REDUCE_IMPL(op, var) \
({ \
	Task *this = get_current_task(); \
	while (required_futures != NULL && required_futures->t == this) { \
		struct future_node *p = required_futures; \
		required_futures = required_futures->next; \
		var op##= AWAIT(p->f, typeof(var)); \
		free(p->f); \
		free(p); \
	} \
	var; \
})

#else // Regular, eagerly allocated futures

typedef Channel *future;

#define FUTURE_ALLOC(fun) fun##_channel()

#define FUTURE_SET(fut, res) channel_send(fut, &(res), sizeof(res))

extern void RT_force_future_channel(Channel *, void *, unsigned int);

#define FUTURE_GET(fut, res, ty) \
do { \
	RT_force_future(fut, res, sizeof(ty)); \
	channel_free(fut); \
} while (0)

#define REDUCE_IMPL(op, var) \
({ \
	Task *this = get_current_task(); \
	while (required_futures != NULL && required_futures->t == this) { \
		struct future_node *p = required_futures; \
		required_futures = required_futures->next; \
		var op##= AWAIT(p->f, typeof(var)); \
		free(p); \
	} \
	var; \
})

#endif // LAZY_FUTURES

// Scoped futures are wrapped in a structure and collected in a list
// Task t requires the result of future f

struct future_node {
	Task *t;
	future f;
	void *r;
	void (*await)(struct future_node *);
	struct future_node *next;
};

static inline void await_future_nodes(struct future_node *hd)
{
	struct future_node *n;
	for (n = hd; n != NULL; n = n->next) {
		assert(n->t == get_current_task());
		n->await(n);
	}
}

extern PRIVATE struct future_node *required_futures;

#endif // FUTURE_INTERNAL_H
