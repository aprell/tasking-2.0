#ifndef UTEST_H
#define UTEST_H

#include <stdio.h>
#include <stdlib.h>

#define check_equal(a, b) \
{ \
	utest_count++; \
	if ((a) != (b)) { \
		fprintf(stderr, "Test %3u failed: %s != %s\n", utest_count, #a, #b); \
		utest_count_failed++; \
	} \
}

#define UTEST(name) \
void utest_##name(void)

#define UTEST_MAIN() \
unsigned int utest_count; \
unsigned int utest_count_failed; \
UTEST(main)

#define utest_init() \
{ \
	utest_count = utest_count_failed = 0; \
}

#define utest_exit() \
{ \
	fprintf(stderr, "%u of %u tests completed successfully\n",\
			utest_count - utest_count_failed, utest_count); \
	if (utest_count_failed) \
		abort(); \
}

#define utest() \
{ \
	utest_init(); \
	utest_main(); \
	utest_exit(); \
}

extern unsigned int utest_count;
extern unsigned int utest_count_failed;

#endif // UTEST_H