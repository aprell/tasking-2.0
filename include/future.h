#ifndef FUTURE_H
#define FUTURE_H

#include "future_internal.h"

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

#ifdef EXPERIMENTAL

// Reductions for splittable tasks ///////////////////////////////////////////

#warning "Reductions are experimental"

#define REDUCE(op, var) REDUCE_IMPL(op, var)

#endif // EXPERIMENTAL

#endif // FUTURE_H
