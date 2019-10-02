#ifndef ASYNC_INTERNAL_H
#define ASYNC_INTERNAL_H

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "macro_utils.h"
#include "profile.h"
#include "runtime.h"
#include "tasking_internal.h"

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
	__task = RT_task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	\
	PACK(&__d, args); \
	memcpy(__task->data, &__d, sizeof(__d)); \
	RT_push(__task); \
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
	__task = RT_task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	__task->splittable = true; \
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
	RT_push(__task); \
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
	__task = RT_task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	\
	RT_push(__task); \
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
	__task = RT_task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))fun##_task_func; \
	__task->splittable = true; \
	__task->start = (lo); \
	__task->cur = (lo); \
	__task->end = (hi); \
	/* Chunk size, at least 1 */ \
	__task->chunks = abs((hi) - (lo)) / num_workers; \
	if (__task->chunks == 0) { \
		__task->chunks = 1; \
	} \
	__task->sst = 1; \
	RT_push(__task); \
	} /* PROFILE */ \
} while (0)

// ASYNC_FOR /////////////////////////////////////////////////////////////////

#define ASYNC_FOR_IMPL(i) ASYNC_FOR_EACH(i)
#define ASYNC_FOR_EACH(i) \
	Task *this = get_current_task(); \
	assert(this->splittable); \
	assert(this->start == this->cur); \
	for (i = this->start, this->cur++; i < this->end; i++, this->cur++, POLL())

#endif // ASYNC_INTERNAL_H
