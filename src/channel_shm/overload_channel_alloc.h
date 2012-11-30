#ifndef OVERLOAD_channel_alloc_H
#define OVERLOAD_channel_alloc_H

#ifndef VA_NARGS
/* Count variadic macro arguments (1-10 arguments, extend as needed)
 */
#define VA_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) N
#define VA_NARGS(...) VA_NARGS_IMPL(__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#endif

/* Simple name mangling of function channel_alloc based on arity
 */
#define __channel_alloc_impl2(n, ...) __channel_alloc_impl__ ## n(__VA_ARGS__)
#define __channel_alloc_impl(n, ...) __channel_alloc_impl2(n, __VA_ARGS__)
#define channel_alloc(...) __channel_alloc_impl(VA_NARGS(__VA_ARGS__), __VA_ARGS__)

#endif // OVERLOAD_channel_alloc_H
