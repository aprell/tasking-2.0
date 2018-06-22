#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef PLATFORM_SHM
#define PRIVATE __thread // TLS
#elif defined PLATFORM_SCC
#define PRIVATE
#else
#error "Need to specify platform. Options: PLATFORM_SHM, PLATFORM_SCC"
#endif

// Supported work-stealing strategies (-DSTEAL=[one|half|adaptive])
// Default is stealing one task at a time (-DSTEAL=one)
#define one 3

#ifndef STEAL
#define STEAL one
#endif

// Supported loop-splitting strategies (-DSPLIT=[half|guided|adaptive])
// Default is split-half (-DSPLIT=half)
#define half 0
#define guided 1
#define adaptive 2

#ifndef SPLIT
#define SPLIT half
#endif

#endif // PLATFORM_H
