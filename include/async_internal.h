#ifndef ASYNC_INTERNAL_H
#define ASYNC_INTERNAL_H

#include <assert.h>
#include <string.h>
#include "async_internal_utils.h"
#include "tasking_internal.h"
#include "profile.h"

// DEFINE_ASYNC //////////////////////////////////////////////////////////////

#define DEFINE_ASYNC_IMPL(fun, args) DEFINE_ASYNC_IMPL_2(fun, ARGTYPES args)
#define DEFINE_ASYNC_IMPL_2(fn, ...) ASYNC_DECL(fn, __VA_ARGS__)
#define ASYNC_DECL(fun, decls, args...) \
struct fun##_task_data { \
	decls; \
}; \
void fun##_task_func(struct fun##_task_data *__d) \
{ \
	Task *this = get_current_task(); \
	assert(!is_root_task(this)); \
	UNPACK(__d, args); \
	fun(args); \
}

// DEFINE_ASYNC0 /////////////////////////////////////////////////////////////

#define DEFINE_ASYNC0_IMPL(fun) ASYNC0_DECL(fun)
#define ASYNC0_DECL(fun) \
void fun##_task_func(void *__d __attribute__((unused))) \
{ \
	Task *this = get_current_task(); \
	assert(!is_root_task(this)); \
	fun(); \
}

PROFILE_EXTERN_DECL(ENQ_DEQ_TASK);

// ASYNC /////////////////////////////////////////////////////////////////////

#define ASYNC_IMPL(...) ASYNC_IMPL_2(VA_NARGS(__VA_ARGS__), __VA_ARGS__)
#define ASYNC_IMPL_2(n, ...) ASYNC_IMPL_3(n, __VA_ARGS__)
#define ASYNC_IMPL_3(n, ...) ASYNC_##n##_IMPL_3(__VA_ARGS__)

// ASYNC (two arguments) /////////////////////////////////////////////////////

#define ASYNC_2_IMPL_3(fun, args) ASYNC_2_IMPL_4(fun, ARGS args)
#define ASYNC_2_IMPL_4(fun, ...) ASYNC_2_CALL(fun, __VA_ARGS__)
#define ASYNC_2_CALL(fun, args...) \
do { \
	Task *__task; \
	struct fun##_task_data __d; \
	PROFILE(ENQ_DEQ_TASK) { \
	\
	__task = task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	\
	PACK(&__d, args); \
	memcpy(__task->data, &__d, sizeof(__d)); \
	push(__task); \
	} /* PROFILE */ \
} while (0)

// ASYNC (three arguments) ///////////////////////////////////////////////////

#define ASYNC_3_IMPL_3(fun, bounds, args) ASYNC_3_IMPL_4(fun, ARGS bounds, ARGS args)
#define ASYNC_3_IMPL_4(fun, lo, hi, ...) ASYNC_3_CALL(fun, lo, hi, __VA_ARGS__)
#define ASYNC_3_CALL(fun, lo, hi, args...) \
do { \
	Task *__task; \
	struct fun##_task_data __d; \
	PROFILE(ENQ_DEQ_TASK) { \
	\
	__task = task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	__task->is_loop = true; \
	__task->start = (lo); \
	__task->cur = (lo); \
	__task->end = (hi); \
	/* Chunk size, at least 1 */ \
	__task->chunks = abs((hi) - (lo)) / num_workers; \
	if (__task->chunks == 0) { \
		__task->chunks = 1; \
	} \
	__task->sst = 1; \
	\
	PACK(&__d, args); \
	memcpy(__task->data, &__d, sizeof(__d)); \
	push(__task); \
	} /* PROFILE */ \
} while (0)

// ASYNC0 ////////////////////////////////////////////////////////////////////

#define ASYNC0_IMPL(...) ASYNC0_IMPL_2(VA_NARGS(__VA_ARGS__), __VA_ARGS__)
#define ASYNC0_IMPL_2(n, ...) ASYNC0_IMPL_3(n, __VA_ARGS__)
#define ASYNC0_IMPL_3(n, ...) ASYNC0_##n##_IMPL_3(__VA_ARGS__)

// ASYNC0 (two arguments) ////////////////////////////////////////////////////

#define ASYNC0_2_IMPL_3(fun, args) ASYNC0_2_CALL(fun)
#define ASYNC0_2_CALL(fun) \
do { \
	Task *__task; \
	PROFILE(ENQ_DEQ_TASK) { \
	\
	__task = task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	\
	push(__task); \
	} /* PROFILE */ \
} while (0)

// ASYNC0 (three arguments) //////////////////////////////////////////////////

#define ASYNC0_3_IMPL_3(fun, bounds, args) ASYNC0_3_IMPL_4(fun, ARGS bounds)
#define ASYNC0_3_IMPL_4(fun, ...) ASYNC0_3_CALL(fun, __VA_ARGS__)
#define ASYNC0_3_CALL(fun, lo, hi) \
do { \
	Task *__task; \
	PROFILE(ENQ_DEQ_TASK) { \
	\
	__task = task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	__task->is_loop = true; \
	__task->start = (lo); \
	__task->cur = (lo); \
	__task->end = (hi); \
	/* Chunk size, at least 1 */ \
	__task->chunks = abs((hi) - (lo)) / num_workers; \
	if (__task->chunks == 0) { \
		__task->chunks = 1; \
	} \
	__task->sst = 1; \
	push(__task); \
	} /* PROFILE */ \
} while (0)

#include "future_internal.h"

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
	__task = task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	\
	__f = FUTURE_ALLOC(fun, __task); \
	PACK(&__d, __f, args); \
	memcpy(__task->data, &__d, sizeof(__d)); \
	push(__task); \
	} /* PROFILE */ \
	__f; \
})

// FUTURE (three arguments) //////////////////////////////////////////////////

#define FUTURE_3_IMPL_3(fun, args, addr) FUTURE_3_CALL(fun, args, addr)
#define FUTURE_3_CALL(fun, args, addr) \
	struct future_node *CONCAT(hd_, __LINE__) = alloca(sizeof(struct future_node)); \
	*CONCAT(hd_, __LINE__) = (struct future_node){ FUTURE_2_CALL(fun, ARGS args), addr, await_##fun, hd }; \
	hd = CONCAT(hd_, __LINE__)

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
	__task = task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	\
	__f = FUTURE_ALLOC(fun, __task); \
	memcpy(__task->data, &__f, sizeof(__f)); \
	push(__task); \
	} /* PROFILE */ \
	__f; \
})

// FUTURE0 (three arguments) /////////////////////////////////////////////////

#define FUTURE0_3_IMPL_3(fun, args, addr) FUTURE0_3_CALL(fun, addr)
#define FUTURE0_3_CALL(fun, addr) \
	struct future_node *CONCAT(hd_, __LINE__) = alloca(sizeof(struct future_node)); \
	*CONCAT(hd_, __LINE__) = (struct future_node){ FUTURE0_2_CALL(fun), addr, await_##fun, hd }; \
	hd = CONCAT(hd_, __LINE__)

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

// ASYNC_FOR /////////////////////////////////////////////////////////////////

#define ASYNC_FOR_IMPL(i) ASYNC_FOR_EACH(i)
#define ASYNC_FOR_EACH(i) \
	Task *this = get_current_task(); \
	assert(this->is_loop); \
	assert(this->start == this->cur); \
	for (i = this->start, this->cur++; i < this->end; i++, this->cur++, RT_loop_split())

#endif // ASYNC_INTERNAL_H
