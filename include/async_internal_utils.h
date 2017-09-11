#ifndef ASYNC_INTERNAL_UTILS_H
#define ASYNC_INTERNAL_UTILS_H

/* Count variadic macro arguments (1-10 arguments, extend as needed) */
#define VA_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) N
#define VA_NARGS(...) VA_NARGS_IMPL(__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)

#if 0
/* Count variadic macro arguments (0-10 arguments, extend as needed)
 * Requires the ", ##__VA_ARGS__" GCC extension */
#define VA_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) N
#define VA_NARGS(...) VA_NARGS_IMPL(_, ##__VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#endif

/* Pack args into data structure x */
#define PACK(x, args...) \
do { \
	*(x) = (typeof(*(x))){ args }; \
} while (0)

/* Unpack data structure x (up to ten members, extend as needed) */

#define UNPACK_1(x, _1) \
	typeof((x)->_1) _1 = (x)->_1

#define UNPACK_2(x, _1, _2) \
	UNPACK_1(x, _1); \
	typeof((x)->_2) _2 = (x)->_2

#define UNPACK_3(x, _1, _2, _3) \
	UNPACK_2(x, _1, _2); \
	typeof((x)->_3) _3 = (x)->_3

#define UNPACK_4(x, _1, _2, _3, _4) \
	UNPACK_3(x, _1, _2, _3); \
	typeof((x)->_4) _4 = (x)->_4

#define UNPACK_5(x, _1, _2, _3, _4, _5) \
	UNPACK_4(x, _1, _2, _3, _4); \
	typeof((x)->_5) _5 = (x)->_5

#define UNPACK_6(x, _1, _2, _3, _4, _5, _6) \
	UNPACK_5(x, _1, _2, _3, _4, _5); \
	typeof((x)->_6) _6 = (x)->_6

#define UNPACK_7(x, _1, _2, _3, _4, _5, _6, _7) \
	UNPACK_6(x, _1, _2, _3, _4, _5, _6); \
	typeof((x)->_7) _7 = (x)->_7

#define UNPACK_8(x, _1, _2, _3, _4, _5, _6, _7, _8) \
	UNPACK_7(x, _1, _2, _3, _4, _5, _6, _7); \
	typeof((x)->_8) _8 = (x)->_8

#define UNPACK_9(x, _1, _2, _3, _4, _5, _6, _7, _8, _9) \
	UNPACK_8(x, _1, _2, _3, _4, _5, _6, _7, _8); \
	typeof((x)->_9) _9 = (x)->_9

#define UNPACK_10(x, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10) \
	UNPACK_9(x, _1, _2, _3, _4, _5, _6, _7, _8, _9); \
	typeof((x)->_10) _10 = (x)->_10

#define UNPACK_IMPL_2(x, n, ...) UNPACK_##n(x, __VA_ARGS__)
#define UNPACK_IMPL(x, n, ...) UNPACK_IMPL_2(x, n, __VA_ARGS__)
#define UNPACK(x, ...) UNPACK_IMPL(x, VA_NARGS(__VA_ARGS__), __VA_ARGS__)

#define ARGTYPES_1(ty1) \
	ty1 t##0, \
	t##0

#define ARGTYPES_2(ty1, ty2) \
	ty1 t##0; ty2 t##1, \
	t##0, t##1

#define ARGTYPES_3(ty1, ty2, ty3) \
	ty1 t##0; ty2 t##1; ty3 t##2, \
	t##0, t##1, t##2

#define ARGTYPES_4(ty1, ty2, ty3, ty4) \
	ty1 t##0; ty2 t##1; ty3 t##2; ty4 t##3, \
	t##0, t##1, t##2, t##3

#define ARGTYPES_5(ty1, ty2, ty3, ty4, ty5) \
	ty1 t##0; ty2 t##1; ty3 t##2; ty4 t##3; ty5 t##4, \
	t##0, t##1, t##2, t##3, t##4

/* ... */

#define ARGTYPES_IMPL_2(n, ...) ARGTYPES_##n(__VA_ARGS__)
#define ARGTYPES_IMPL(n, ...) ARGTYPES_IMPL_2(n, __VA_ARGS__)
#define ARGTYPES(...) ARGTYPES_IMPL(VA_NARGS(__VA_ARGS__), __VA_ARGS__)

#define ARGS(...) __VA_ARGS__

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)

#endif // ASYNC_INTERNAL_UTILS_H
