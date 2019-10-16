#ifndef FUTURE_INTERNAL_H
#define FUTURE_INTERNAL_H

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "channel.h"
#include "macro_utils.h"
#include "profile.h"
#include "runtime.h"
#include "tasking_internal.h"

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

#define FUTURE_CONVERT(task) \
do { \
	/* Lazy allocation */ \
	lazy_future *f; \
	memcpy(&f, (task)->data, sizeof(lazy_future *)); \
	if (!f->has_channel) { \
		assert(sizeof(f->buf) == 8); \
		f->chan = channel_alloc(sizeof(f->buf), 0, SPSC); \
		f->has_channel = true; \
	} /* else nothing to do; already allocated */ \
} while (0)

#else // Regular, eagerly allocated futures

typedef Channel *future;

#define FUTURE_ALLOC(fun) fun##_channel()

#define FUTURE_SET(fut, res) channel_send(fut, &(res), sizeof(res))

#define FUTURE_GET(fut, res, ty) \
do { \
	RT_force_future(fut, res, sizeof(ty)); \
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

// DEFINE_FUTURE /////////////////////////////////////////////////////////////

#define DEFINE_FUTURE_IMPL(rty, fun, args) DEFINE_FUTURE_IMPL_2(rty, fun, ARGTYPES args)
#define DEFINE_FUTURE_IMPL_2(rty, fun, ...) FUTURE_DECL(rty, fun, __VA_ARGS__)
#define FUTURE_DECL(rty, fun, decls, args...) \
struct fun##_task_data { \
	/* Store future as first field to be able to retrieve it easily */\
	future __f; \
	decls; \
}; \
/* Helper function to capture type of values sent over channel
 * Only used for regular futures */\
static inline Channel *fun##_channel(void) \
{ \
	return channel_alloc(sizeof(rty), 0, SPSC); \
} \
/* Helper function to force a future in a list of futures */\
static inline void await_##fun(struct future_node *n) \
{ \
	FUTURE_GET(n->f, n->r, rty); \
} \
void fun##_task_func(struct fun##_task_data *__d) \
{ \
	Task *this = get_current_task(); \
	assert(!is_root_task(this)); \
	UNPACK(__d, __f, args); \
	rty __tmp = fun(args); \
	FUTURE_SET(__f, __tmp); \
}

// DEFINE_FUTURE0 ////////////////////////////////////////////////////////////

#define DEFINE_FUTURE0_IMPL(rty, fun) FUTURE0_DECL(rty, fun)
#define FUTURE0_DECL(rty, fun) \
/* Helper function to capture type of values sent over channel
 * Only used for regular futures */\
static inline Channel *fun##_channel(void) \
{ \
	return channel_alloc(sizeof(rty), 0, SPSC); \
} \
/* Helper function to force a future in a list of futures */\
static inline void await_##fun(struct future_node *n) \
{ \
	FUTURE_GET(n->f, n->r, rty); \
} \
void fun##_task_func(void *__d __attribute__((unused))) \
{ \
	Task *this = get_current_task(); \
	assert(!is_root_task(this)); \
	future __f; \
	rty __tmp = fun(); \
	memcpy(&__f, this->data, sizeof(__f)); \
	FUTURE_SET(__f, __tmp); \
}

PROFILE_EXTERN_DECL(ENQ_DEQ_TASK);

// FUTURE ////////////////////////////////////////////////////////////////////

#define FUTURE_IMPL(...) FUTURE_IMPL_2(VA_NARGS(__VA_ARGS__), __VA_ARGS__)
#define FUTURE_IMPL_2(n, ...) FUTURE_IMPL_3(n, __VA_ARGS__)
#define FUTURE_IMPL_3(n, ...) FUTURE_##n##_IMPL_3(__VA_ARGS__)

// FUTURE (two arguments) ////////////////////////////////////////////////////

#define FUTURE_2_IMPL_3(fun, args) FUTURE_2_IMPL_4(fun, ARGS args)
#define FUTURE_2_IMPL_4(fun, ...) FUTURE_2_CALL(fun, __VA_ARGS__)
#define FUTURE_2_CALL(fun, args...) \
({  \
	Task *__task; \
	struct fun##_task_data __d; \
	future __f; \
	PROFILE(ENQ_DEQ_TASK) { \
	\
	__task = RT_task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	__task->has_future = true; \
	\
	__f = FUTURE_ALLOC(fun); \
	PACK(&__d, __f, args); \
	memcpy(__task->data, &__d, sizeof(__d)); \
	RT_push(__task); \
	} /* PROFILE */ \
	__f; \
})

// FUTURE (three arguments) //////////////////////////////////////////////////

#define FUTURE_3_IMPL_3(fun, args, addr) FUTURE_3_CALL(fun, args, addr)
#define FUTURE_3_CALL(fun, args, addr) \
	struct future_node *CONCAT(hd_, __LINE__) = alloca(sizeof(struct future_node)); \
	*CONCAT(hd_, __LINE__) = (struct future_node){ FUTURE_2_CALL(fun, ARGS args), addr, await_##fun, hd }; \
	hd = CONCAT(hd_, __LINE__)

// FUTURE (four arguments) ///////////////////////////////////////////////////

#define FUTURE_4_IMPL_3(fun, bounds, args, _) FUTURE_4_IMPL_4(fun, ARGS bounds, ARGS args)
#define FUTURE_4_IMPL_4(fun, lo, hi, ...) FUTURE_4_CALL(fun, lo, hi, __VA_ARGS__)
#define FUTURE_4_CALL(fun, lo, hi, args...) \
({ \
	Task *__task; \
	struct fun##_task_data __d; \
	future __f; \
	PROFILE(ENQ_DEQ_TASK) { \
	\
	__task = RT_task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	__task->splittable = true; \
	__task->start = (lo); \
	__task->cur = (lo); \
	__task->end = (hi); \
	__task->has_future = true; \
	__f = FUTURE_ALLOC(fun); \
	PACK(&__d, __f, args); \
	memcpy(__task->data, &__d, sizeof(__d)); \
	RT_push(__task); \
	} /* PROFILE */ \
	__f; \
})

// FUTURE0 ///////////////////////////////////////////////////////////////////

#define FUTURE0_IMPL(...) FUTURE0_IMPL_2(VA_NARGS(__VA_ARGS__), __VA_ARGS__)
#define FUTURE0_IMPL_2(n, ...) FUTURE0_IMPL_3(n, __VA_ARGS__)
#define FUTURE0_IMPL_3(n, ...) FUTURE0_##n##_IMPL_3(__VA_ARGS__)

// FUTURE0 (two arguments) ///////////////////////////////////////////////////

#define FUTURE0_2_IMPL_3(fun, args) FUTURE0_2_CALL(fun)
#define FUTURE0_2_CALL(fun) \
({  \
	Task *__task; \
	future __f; \
	PROFILE(ENQ_DEQ_TASK) { \
	\
	__task = RT_task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	__task->has_future = true; \
	\
	__f = FUTURE_ALLOC(fun); \
	memcpy(__task->data, &__f, sizeof(__f)); \
	RT_push(__task); \
	} /* PROFILE */ \
	__f; \
})

// FUTURE0 (three arguments) /////////////////////////////////////////////////

#define FUTURE0_3_IMPL_3(fun, args, addr) FUTURE0_3_CALL(fun, addr)
#define FUTURE0_3_CALL(fun, addr) \
	struct future_node *CONCAT(hd_, __LINE__) = alloca(sizeof(struct future_node)); \
	*CONCAT(hd_, __LINE__) = (struct future_node){ FUTURE0_2_CALL(fun), addr, await_##fun, hd }; \
	hd = CONCAT(hd_, __LINE__)

// FUTURE0 (four arguments) //////////////////////////////////////////////////

#define FUTURE0_4_IMPL_3(fun, bounds, args, _) FUTURE0_4_IMPL_4(fun, ARGS bounds)
#define FUTURE0_4_IMPL_4(fun, ...) FUTURE0_4_CALL(fun, __VA_ARGS__)
#define FUTURE0_4_CALL(fun, lo, hi) \
({ \
	Task *__task; \
	future __f; \
	PROFILE(ENQ_DEQ_TASK) { \
	\
	__task = RT_task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	__task->splittable = true; \
	__task->start = (lo); \
	__task->cur = (lo); \
	__task->end = (hi); \
	__task->has_future = true; \
	__f = FUTURE_ALLOC(fun); \
	memcpy(__task->data, &__f, sizeof(__f)); \
	RT_push(__task); \
	} /* PROFILE */ \
	__f; \
})

// AWAIT /////////////////////////////////////////////////////////////////////

// Returns the result of evaluating future fut
// ty is used to declare a temporary variable
#define AWAIT_IMPL(fut, ty) AWAIT_CALL(fut, ty)
#define AWAIT_CALL(fut, ty) \
({ \
	/* Allows to nest FUTURE inside AWAIT */ \
	future __f = (fut); \
	ty __tmp; \
	FUTURE_GET(__f, &__tmp, ty); \
	__tmp; \
})

#if 0
// Returns the result of evaluating future fut
// ptr is a pointer that will point to the future's result
#define AWAIT_CALL(fut, ptr) \
({ \
	FUTURE_GET(fut, ptr, typeof(*ptr)); \
	*(ptr); \
})
#endif

// AWAIT_ALL /////////////////////////////////////////////////////////////////

#define AWAIT_ALL_IMPL AWAIT_ALL_CALL
#define AWAIT_ALL_CALL \
	for (struct future_node *hd = NULL; hd == NULL || (await_future_nodes(hd), 0);)

// REDUCE ////////////////////////////////////////////////////////////////////

#ifdef LAZY_FUTURES

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

#endif // FUTURE_INTERNAL_H
