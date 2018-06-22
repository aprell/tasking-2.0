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

#define FUTURE_GET(fut, res, ty) RT_force_future(fut, res, sizeof(ty))

#define REDUCE_IMPL(op, var) \
({ \
	Task *this = get_current_task(); \
	while (this->futures != NULL) { \
		struct future_node *p = this->futures; \
		this->futures = ((struct future_node *)this->futures)->next; \
		var op##= AWAIT(p->f, typeof(var)); \
		free(p->f); /* Allocated on the heap */ \
		free(p); \
	} \
	var; \
})

#else // Regular, eagerly allocated futures

typedef Channel *future;

#define FUTURE_ALLOC(fun) fun##_channel()

#define FUTURE_SET(fut, res) channel_send(fut, &(res), sizeof(res))

#define FUTURE_GET(fut, res, ty) \
do { \
	RT_force_future(fut, res, sizeof(ty)); \
	channel_free(fut); \
} while (0)

#define REDUCE_IMPL(op, var) \
({ \
	Task *this = get_current_task(); \
	while (this->futures != NULL) { \
		struct future_node *p = this->futures; \
		this->futures = ((struct future_node *)this->futures)->next; \
		var op##= AWAIT(p->f, typeof(var)); \
		free(p); \
	} \
	var; \
})

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