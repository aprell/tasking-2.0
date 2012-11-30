#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef PLATFORM_SHM
#define PRIVATE __thread // TLS
#elif defined PLATFORM_SCC
#define PRIVATE 
#else
#error "Need to specify platform. Options: PLATFORM_SHM, PLATFORM_SCC"
#endif

#endif // PLATFORM_H
