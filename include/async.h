#ifndef ASYNC_H
#define ASYNC_H

#include "tasking_internal.h"
#include "timer.h"

#ifndef NTIME
extern PRIVATE mytimer_t timer_run_tasks;
extern PRIVATE mytimer_t timer_enq_deq_tasks;
#endif

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

/* Unpack data structure x (up to five members, extend as needed)
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
	timer_end(&timer_enq_deq_tasks); \
	timer_start(&timer_run_tasks); \
	fname(args); \
	timer_end(&timer_run_tasks); \
	timer_start(&timer_enq_deq_tasks); \
}

/* Async functions with return values are basically futures
 */
#define FUTURE_DECL(ret, fname, decls, args...) \
struct fname##_task_data { \
	decls; \
	/*ret *__v;*/ \
	chan __f; \
}; \
void fname##_task_func(struct fname##_task_data *__d) \
{ \
	Task *this = get_current_task(); \
	assert(!is_root_task(this)); \
	assert((struct fname##_task_data *)this->data == __d); \
	\
	UNPACK(__d, args, __f); \
	timer_end(&timer_enq_deq_tasks); \
	timer_start(&timer_run_tasks); \
	ret tmp = fname(args); \
	timer_end(&timer_run_tasks); \
	timer_start(&timer_enq_deq_tasks); \
	/**(__v) = tmp;*/ \
	assert(channel_send(chanref_get(__f), &tmp, sizeof(tmp))); \
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
	timer_start(&timer_enq_deq_tasks); \
	\
	__task = task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))f##_task_func; \
	\
	__d = (struct f##_task_data *)__task->data; \
	PACK(__d, args); \
	push(__task); \
	timer_end(&timer_enq_deq_tasks); \
} while (0)

/* Create splittable loop task [s, e)
 */
#define ASYNC_FOR(f, s, e, env...) \
do { \
	Task *__task; \
	struct f##_task_data *__d; \
	timer_start(&timer_enq_deq_tasks); \
	\
	__task = task_alloc(); \
	__task->parent = get_current_task(); \
	__task->fn = (void (*)(void *))f##_task_func; \
	__task->is_loop = true; \
	__task->start = (s); \
	__task->end = (e); \
	\
	__d = (struct f##_task_data *)__task->data; \
	PACK(__d, env); \
	push(__task); \
	timer_end(&timer_enq_deq_tasks); \
} while (0)

#endif // ASYNC_H
