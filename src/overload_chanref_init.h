#ifndef OVERLOAD_chanref_init_H
#define OVERLOAD_chanref_init_H

#ifndef VA_NARGS
/* Count variadic macro arguments (1-10 arguments, extend as needed)
 */
#define VA_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) N
#define VA_NARGS(...) VA_NARGS_IMPL(__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#endif

/* Simple name mangling of function chanref_init based on arity
 */
#define __chanref_init_impl2(n, ...) __chanref_init_impl__ ## n(__VA_ARGS__)
#define __chanref_init_impl(n, ...) __chanref_init_impl2(n, __VA_ARGS__)
#define chanref_init(...) __chanref_init_impl(VA_NARGS(__VA_ARGS__), __VA_ARGS__)

#endif // OVERLOAD_chanref_init_H
