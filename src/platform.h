#ifndef PLATFORM_H
#define PLATFORM_H

#define MAXWORKERS 256

#define PRIVATE __thread

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

// Supported worker backoff strategies (-DBACKOFF=[sleep_exp|wait_cond])
#define sleep_exp 4
#define wait_cond 5

#define UNUSED(x) x __attribute__((unused))

#define UNREACHABLE() assert(false && "Unreachable")

#endif // PLATFORM_H
