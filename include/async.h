#ifndef ASYNC_H
#define ASYNC_H

#include <assert.h>
#include "tasking_internal.h"
#include "chanref.h"
#include "profile.h"

PROFILE_EXTERN_DECL(ENQ_DEQ_TASK);

/* Count variadic macro arguments (1-10 arguments, extend as needed)
 */
#define VA_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) N
#define VA_NARGS(...) VA_NARGS_IMPL(__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)

/* Pack args into data structure x
 */
#define PACK(x, args...) \
do { \
	*(x) = (typeof(*(x))){ args }; \
} while (0)

/* Unpack data structure x (up to ten members, extend as needed)
 */
#define UNPACK1(x, _1) \
	typeof((x)->_1) _1 = (x)->_1

#define UNPACK2(x, _1, _2) \
	typeof((x)->_1) _1 = (x)->_1; \
	typeof((x)->_2) _2 = (x)->_2

#define UNPACK3(x, _1, _2, _3) \
	typeof((x)->_1) _1 = (x)->_1; \
	typeof((x)->_2) _2 = (x)->_2; \
	typeof((x)->_3) _3 = (x)->_3

#define UNPACK4(x, _1, _2, _3, _4) \
	typeof((x)->_1) _1 = (x)->_1; \
	typeof((x)->_2) _2 = (x)->_2; \
	typeof((x)->_3) _3 = (x)->_3; \
	typeof((x)->_4) _4 = (x)->_4

#define UNPACK5(x, _1, _2, _3, _4, _5) \
	typeof((x)->_1) _1 = (x)->_1; \
	typeof((x)->_2) _2 = (x)->_2; \
	typeof((x)->_3) _3 = (x)->_3; \
	typeof((x)->_4) _4 = (x)->_4; \
	typeof((x)->_5) _5 = (x)->_5

#define UNPACK6(x, _1, _2, _3, _4, _5, _6) \
	typeof((x)->_1) _1 = (x)->_1; \
	typeof((x)->_2) _2 = (x)->_2; \
	typeof((x)->_3) _3 = (x)->_3; \
	typeof((x)->_4) _4 = (x)->_4; \
	typeof((x)->_5) _5 = (x)->_5; \
	typeof((x)->_6) _6 = (x)->_6

#define UNPACK7(x, _1, _2, _3, _4, _5, _6, _7) \
	typeof((x)->_1) _1 = (x)->_1; \
	typeof((x)->_2) _2 = (x)->_2; \
	typeof((x)->_3) _3 = (x)->_3; \
	typeof((x)->_4) _4 = (x)->_4; \
	typeof((x)->_5) _5 = (x)->_5; \
	typeof((x)->_6) _6 = (x)->_6; \
	typeof((x)->_7) _7 = (x)->_7

#define UNPACK8(x, _1, _2, _3, _4, _5, _6, _7, _8) \
	typeof((x)->_1) _1 = (x)->_1; \
	typeof((x)->_2) _2 = (x)->_2; \
	typeof((x)->_3) _3 = (x)->_3; \
	typeof((x)->_4) _4 = (x)->_4; \
	typeof((x)->_5) _5 = (x)->_5; \
	typeof((x)->_6) _6 = (x)->_6; \
	typeof((x)->_7) _7 = (x)->_7; \
	typeof((x)->_8) _8 = (x)->_8

#define UNPACK9(x, _1, _2, _3, _4, _5, _6, _7, _8, _9) \
	typeof((x)->_1) _1 = (x)->_1; \
	typeof((x)->_2) _2 = (x)->_2; \
	typeof((x)->_3) _3 = (x)->_3; \
	typeof((x)->_4) _4 = (x)->_4; \
	typeof((x)->_5) _5 = (x)->_5; \
	typeof((x)->_6) _6 = (x)->_6; \
	typeof((x)->_7) _7 = (x)->_7; \
	typeof((x)->_8) _8 = (x)->_8; \
	typeof((x)->_9) _9 = (x)->_9

#define UNPACK10(x, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10) \
	typeof((x)->_1)   _1 = (x)->_1; \
	typeof((x)->_2)   _2 = (x)->_2; \
	typeof((x)->_3)   _3 = (x)->_3; \
	typeof((x)->_4)   _4 = (x)->_4; \
	typeof((x)->_5)   _5 = (x)->_5; \
	typeof((x)->_6)   _6 = (x)->_6; \
	typeof((x)->_7)   _7 = (x)->_7; \
	typeof((x)->_8)   _8 = (x)->_8; \
	typeof((x)->_9)   _9 = (x)->_9; \
	typeof((x)->_10) _10 = (x)->_10

#define UNPACK_IMPL2(x, n, ...) UNPACK ## n(x, __VA_ARGS__)
#define UNPACK_IMPL(x, n, ...) UNPACK_IMPL2(x, n, __VA_ARGS__)
#define UNPACK(x, ...) UNPACK_IMPL(x, VA_NARGS(__VA_ARGS__), __VA_ARGS__)

/* Allows for function fname to be called asynchronously
 *
 * Examples:
 * ASYNC_DECL(do_sth, int a; int b, a, b);
 * FUTURE_DECL(int, do_sth_else, int a; int b, a, b);
 */
#define ASYNC_DECL(fname, decls, args...) \
struct fname##_task_data { \
	decls; \
}; \
void fname##_task_func(struct fname##_task_data *__d) \
{ \
	Task *this = get_current_task(); \
	assert(!is_root_task(this)); \
	assert((struct fname##_task_data *)this->data == __d); \
	\
	UNPACK(__d, args); \
	fname(args); \
}

// User code need not be aware of channels
typedef chan future;

#define new_future(f) new_##f##_future()

#ifdef CACHE_FUTURES
#ifndef FUTURE_FREELIST_SIZE
#define FUTURE_FREELIST_SIZE 100
#endif
#define FUTURE_DECL_FREELIST(type) \
static PRIVATE unsigned int __future_##type##_freelist_items = 0; \
static PRIVATE future __future_##type##_freelist[FUTURE_FREELIST_SIZE]
#else
#define FUTURE_DECL_FREELIST(type) // empty
#endif // CACHE_FUTURES

/* Async functions with return values are basically futures
 */
#ifdef CACHE_FUTURES
#define FUTURE_DECL(ret, fname, decls, args...) \
struct fname##_task_data { \
	decls; \
	future __f; \
}; \
static inline future new_##fname##_future(void) \
{ \
	future f; \
	if (__future_##ret##_freelist_items > 0) { \
		return __future_##ret##_freelist[--__future_##ret##_freelist_items]; \
	} \
	chanref_set(&f, channel_alloc(sizeof(ret), 0, SPSC)); \
	return f; \
} \
void fname##_task_func(struct fname##_task_data *__d) \
{ \
	Task *this = get_current_task(); \
	assert(!is_root_task(this)); \
	assert((struct fname##_task_data *)this->data == __d); \
	\
	UNPACK(__d, args, __f); \
	ret __tmp = fname(args); \
	channel_send(chanref_get(__f), &__tmp, sizeof(__tmp)); \
}
#else
#define FUTURE_DECL(ret, fname, decls, args...) \
struct fname##_task_data { \
	decls; \
	future __f; \
}; \
static inline future new_##fname##_future(void) \
{ \
	future f; \
	chanref_set(&f, channel_alloc(sizeof(ret), 0, SPSC)); \
	return f; \
} \
void fname##_task_func(struct fname##_task_data *__d) \
{ \
	Task *this = get_current_task(); \
	assert(!is_root_task(this)); \
	assert((struct fname##_task_data *)this->data == __d); \
	\
	UNPACK(__d, args, __f); \
	ret __tmp = fname(args); \
	channel_send(chanref_get(__f), &__tmp, sizeof(__tmp)); \
}
#endif // CACHE_FUTURES

// Cilk-style fork/join
#define TASK_DECL(ret, fname, decls, args...) \
struct fname##_task_data { \
	decls; \
	ret *__r; \
	atomic_t *num_children; \
}; \
void fname##_task_func(struct fname##_task_data *__d) \
{ \
	Task *this = get_current_task(); \
	assert(!is_root_task(this)); \
	assert((struct fname##_task_data *)this->data == __d); \
	\
	UNPACK(__d, args, __r, num_children); \
	*__r = fname(args); \
	atomic_dec(num_children); \
}

/* Call f(args) asynchronously
 *
 * Requires underlying tasking implementation
 *
 * Examples:
 * ASYNC(do_sth, a, b);
 * ASYNC(do_sth_else, a, b, &c); // function with return value
 */
#define ASYNC(f, args...) \
do { \
	Task *__task; \
	struct f##_task_data *__d; \
	PROFILE(ENQ_DEQ_TASK) { \
	\
	__task = task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))f##_task_func; \
	\
	__d = (struct f##_task_data *)__task->data; \
	PACK(__d, args); \
	push(__task); \
	} /* PROFILE */ \
} while (0)

// Version that returns a future to wait for
#define __ASYNC(f, args...) \
({  \
	Task *__task; \
	struct f##_task_data *__d; \
	future __f; \
	PROFILE(ENQ_DEQ_TASK) { \
	\
	__task = task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))f##_task_func; \
	\
	__d = (struct f##_task_data *)__task->data; \
	__f = new_future(f); \
	PACK(__d, args, __f); \
	push(__task); \
	} /* PROFILE */ \
	__f; \
})

// Cilk-style spawn
#define SPAWN(f, args...) \
do { \
	Task *__task; \
	struct f##_task_data *__d; \
	PROFILE(ENQ_DEQ_TASK) { \
	\
	__task = task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))f##_task_func; \
	\
	__d = (struct f##_task_data *)__task->data; \
	atomic_inc(&num_children); \
	PACK(__d, args, &num_children); \
	push(__task); \
	} /* PROFILE */ \
} while (0)

extern void RT_force_future_channel(Channel *, void *, unsigned int);

// Returns the result of evaluating future f
// p is a pointer that will point to the future's result
#define AWAIT(f, p) \
({ \
	RT_force_future_channel(chanref_get(f), p, sizeof(typeof(*(p)))); \
	channel_free(chanref_get(f)); \
	*(p); \
})

// Returns the result of evaluating future f
// type is used to declare a temporary variable
#ifdef CACHE_FUTURES
#define __AWAIT(f, type) \
({ \
	type __tmp; \
	RT_force_future_channel(chanref_get(f), &__tmp, sizeof(__tmp)); \
	if (__future_##type##_freelist_items < FUTURE_FREELIST_SIZE) { \
		__future_##type##_freelist[__future_##type##_freelist_items++] = (f); \
	} else { \
		channel_free(chanref_get(f)); \
	} \
	__tmp; \
})
#else
#define __AWAIT(f, type) \
({ \
	type __tmp; \
	RT_force_future_channel(chanref_get(f), &__tmp, sizeof(__tmp)); \
	channel_free(chanref_get(f)); \
	__tmp; \
})
#endif // CACHE_FUTURES

extern void RT_taskwait(atomic_t *num_children);

// Cilk-style sync
#define SYNC RT_taskwait(&num_children)

/* Create splittable loop task [s, e)
 */
#define ASYNC_FOR(f, s, e, env...) \
do { \
	Task *__task; \
	struct f##_task_data *__d; \
	PROFILE(ENQ_DEQ_TASK) { \
	\
	__task = task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))f##_task_func; \
	__task->is_loop = true; \
	__task->start = (s); \
	__task->cur = (s); \
	__task->end = (e); \
	\
	__d = (struct f##_task_data *)__task->data; \
	PACK(__d, env); \
	push(__task); \
	} /* PROFILE */ \
} while (0)

/* Iterate over loop task
 *
 * Example:
 * long i;
 * for_each_task (i) {
 *     // Body of task i
 *     RT_split_loop();
 * }
 */
#define for_each_task(i) \
	Task *this = get_current_task(); \
	assert(this->is_loop); \
	assert(this->start == this->cur); \
	for (i = this->start, this->cur++; i < this->end; i++, this->cur++)

#endif // ASYNC_H
