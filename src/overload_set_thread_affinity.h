#ifndef OVERLOAD_set_thread_affinity_H
#define OVERLOAD_set_thread_affinity_H

#ifndef VA_NARGS
/* Count variadic macro arguments (1-10 arguments, extend as needed)
 */
#define VA_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) N
#define VA_NARGS(...) VA_NARGS_IMPL(__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#endif

/* Simple name mangling of function set_thread_affinity based on arity
 */
#define __set_thread_affinity_impl2(n, ...) __set_thread_affinity_impl__ ## n(__VA_ARGS__)
#define __set_thread_affinity_impl(n, ...) __set_thread_affinity_impl2(n, __VA_ARGS__)
#define set_thread_affinity(...) __set_thread_affinity_impl(VA_NARGS(__VA_ARGS__), __VA_ARGS__)

#endif // OVERLOAD_set_thread_affinity_H
