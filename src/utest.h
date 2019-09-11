#ifndef UTEST_H
#define UTEST_H

#include <stdio.h>
#include <stdlib.h>

#define check_equal(a, b) \
do { \
	utest_count++; \
	if ((a) != (b)) { \
		fprintf(stderr, "%s:%u: check failed: %s != %s\n", \
				__FILE__, __LINE__, #a, #b); \
		utest_count_fail++; \
	} \
} while (0)

#define check_not_equal(a, b) \
do { \
	utest_count++; \
	if ((a) == (b)) { \
		fprintf(stderr, "%s:%u: check failed: %s == %s\n", \
				__FILE__, __LINE__, #a, #b); \
		utest_count_fail++; \
	} \
} while (0)

#define UTEST_INIT \
unsigned int utest_count = 0; \
unsigned int utest_count_fail = 0;

#define UTEST_DONE \
do { \
	unsigned int utest_count_ok = utest_count - utest_count_fail; \
	fprintf(stderr, "%u check%s were successful, %u check%s failed\n", \
			utest_count_ok, utest_count_ok == 1 ? "" : "s", \
			utest_count_fail, utest_count_fail == 1 ? "" : "s"); \
	if (utest_count_fail > 0) { \
		fprintf(stderr, "FAILURE\n"); \
		exit(EXIT_FAILURE); \
	} else { \
		fprintf(stderr, "SUCCESS\n"); \
	} \
} while (0)

#endif // UTEST_H
