#ifndef ASYNC_H
#define ASYNC_H

#include "async_internal.h"

// ASYNC procedures (have asynchronous side effects) /////////////////////////

#define DEFINE_ASYNC(fun, args) DEFINE_ASYNC_IMPL(fun, args)

#define ASYNC(/* fun, [(i, j),] args */ ...) ASYNC_IMPL(__VA_ARGS__)

// Special macros for zero-argument ASYNCs to avoid creating a data structure
// to collect task arguments /////////////////////////////////////////////////

#define DEFINE_ASYNC0(fun, ...) DEFINE_ASYNC0_IMPL(fun)

#define ASYNC0(/* fun, [(i, j),] empty_args */ ...) ASYNC0_IMPL(__VA_ARGS__)

// FUTURE functions (return values in the future) ////////////////////////////

#define DEFINE_FUTURE(rty, fun, args) DEFINE_FUTURE_IMPL(rty, fun, args)

#define FUTURE(/* fun, args [, addr] */ ...) FUTURE_IMPL(__VA_ARGS__)

// Special macros for zero-argument FUTUREs to avoid creating a data structure
// to collect task arguments /////////////////////////////////////////////////

#define DEFINE_FUTURE0(rty, fun, ...) DEFINE_FUTURE0_IMPL(rty, fun)

#define FUTURE0(/* fun, empty_args [,addr] */ ...) FUTURE0_IMPL(__VA_ARGS__)

// Await a future's result ///////////////////////////////////////////////////

#define AWAIT(fut, ty) AWAIT_IMPL(fut, ty)

// Await all scoped futures' results upon leaving a block ////////////////////

#define AWAIT_ALL AWAIT_ALL_IMPL

// Helper macro for executing splittable tasks ///////////////////////////////

#define ASYNC_FOR(i) ASYNC_FOR_IMPL(i)

// Reductions for splittable tasks ///////////////////////////////////////////

#define REDUCE(op, var) REDUCE_IMPL(op, var)

#endif // ASYNC_H
