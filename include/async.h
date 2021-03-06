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

// Helper macro for executing splittable tasks ///////////////////////////////

#define ASYNC_FOR(i) ASYNC_FOR_IMPL(i)

#endif // ASYNC_H
