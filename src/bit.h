#ifndef BIT_H
#define BIT_H

#define BIT(n)                             (1u << (n))

#define BIT_MASK_32(n)                     ((n) >= 32 ? -1u : BIT(n) - 1)

#define BIT_MASK_64(n)                     ((n) >= 64 ? -1u : BIT(n) - 1)

// Check if the n-th bit is set or not:
// if (word & BIT(n)) set
// else not set

// Check if a number is even or odd:
// if (number & BIT(0) == 0) number is even
// else number is odd

#define IS_EVEN(n)                         (((n) & BIT(0)) == 0)

#define IS_ODD(n)                          (((n) & BIT(0)) == 1)

// Set the n-th bit:
// word |= BIT(n)

// Zero the n-th bit:
// word &= ~BIT(n)

// Toggle the n-th bit:
// word ^= BIT(n)

#define ZERO_RIGHTMOST_ONE_BIT(n)          ((n) &  ((n) - 1))

#define ISOLATE_RIGHTMOST_ONE_BIT(n)       ((n) & ~((n) - 1))
// -x = ~x + 1 (two's complement)
// => ~x = -x - 1
// => ~(x - 1) = -(x - 1) - 1 = -x

#define SET_RIGHTMOST_ZERO_BIT(n)           ((n) |  ((n) + 1))

#define ISOLATE_RIGHTMOST_ZERO_BIT(n)      (~(n) &  ((n) + 1))
// ~x & SET_RIGHTMOST_ZERO_BIT(x) =
// ~x & (x | (x + 1)) =
// ~x & x | ~x & (x + 1) =
// 0 | ~x & (x + 1) =
// ~x & (x + 1)

#define SWAP(x, y) \
do { \
	x = x ^ y; \
	y = x ^ y; \
	x = x ^ y; \
} while (0)
// x = x ^ y
// y = x ^ y = (x ^ y) ^ y = x ^ (y ^ y) = x ^ 0 = x
// x = x ^ y = (x ^ y) ^ x = (y ^ x) ^ x = y ^ (x ^ x) = y ^ 0 = y

#define MIN(x, y)      ((y) ^ (((x) ^ (y)) & -((x) < (y))))
// Case 1: x < y
// => y ^ ((x ^ y) & -1) = y ^ (x ^ y) = x
// Case 2: x >= y
// => y ^ ((x ^ y) & -0) = y ^ 0 = y

#define MAX(x, y)      ((y) ^ (((x) ^ (y)) & -((x) > (y))))
// Case 1: x > y
// => y ^ ((x ^ y) & -1) = y ^ (x ^ y) = x
// Case 2: x <= y
// => y ^ ((x ^ y) & -0) = y ^ 0 = y

static inline unsigned int count_one_bits(unsigned int n)
{
	unsigned int count = 0;
	unsigned int i;

	for (i = n; i != 0; i = ZERO_RIGHTMOST_ONE_BIT(i))
		count++;

	return count;
}

static inline int rightmost_one_bit_pos(unsigned int n)
{
	int pos = -1;
	unsigned int i;

	for (i = ISOLATE_RIGHTMOST_ONE_BIT(n); i != 0; i >>= 1)
		pos++;

	return pos;
}

#define IS_POWER_OF_TWO(n)     (ZERO_RIGHTMOST_ONE_BIT(n) == 0)

static inline unsigned int round_down_power_of_two(unsigned int n)
{
	if (IS_POWER_OF_TWO(n)) return n;

	while (ZERO_RIGHTMOST_ONE_BIT(n) > 0) {
		n = ZERO_RIGHTMOST_ONE_BIT(n);
	}

	return n;
}

static inline unsigned int round_up_power_of_two(unsigned int n)
{
	if (IS_POWER_OF_TWO(n)) return n;

	return round_down_power_of_two(n) << 1;
}

static inline unsigned int round_up_power_of_two_2(unsigned int n)
{
	//if (IS_POWER_OF_TWO(n)) return n;

	n--;
	n |= n >>  1;
	n |= n >>  2;
	n |= n >>  4;
	n |= n >>  8;
	n |= n >> 16;
	n++;

	return n;
}

#ifdef TEST
#include <assert.h>
static inline void BIT_H_test(void)
{
	unsigned int _13 = 0 | BIT(3) | BIT(2) | BIT(0);
	assert(_13 == 13);

	_13 |= BIT(1);
	assert(_13 == 15);
	assert(IS_ODD(_13));

	_13 &= ~BIT(3);
	assert(_13 == 7);
	assert(IS_ODD(_13));

	_13 = ZERO_RIGHTMOST_ONE_BIT(_13);
	assert(_13 == 6);
	assert(IS_EVEN(_13));

	assert(count_one_bits(  0) == 0);
	assert(count_one_bits(  1) == 1);
	assert(count_one_bits(  2) == 1);
	assert(count_one_bits(  3) == 2);
	assert(count_one_bits(255) == 8);

	assert(rightmost_one_bit_pos( 0) == -1);
	assert(rightmost_one_bit_pos( 1) ==  0);
	assert(rightmost_one_bit_pos( 2) ==  1);
	assert(rightmost_one_bit_pos( 3) ==  0);
	assert(rightmost_one_bit_pos(16) ==  4);

	int a = 1, b = 2;
	assert(MIN(a, b) == a);
	assert(MAX(a, b) == b);

	SWAP(a, b);
	assert(MIN(a, b) == b);
	assert(MAX(a, b) == a);

	assert(!IS_POWER_OF_TWO(42));
	assert(IS_POWER_OF_TWO(64));
	assert(round_down_power_of_two(42) == 32);
	assert(round_down_power_of_two(64) == 64);
	assert(round_up_power_of_two(42) == 64);
	assert(round_up_power_of_two(64) == 64);
	assert(round_up_power_of_two_2(42) == 64);
	assert(round_up_power_of_two_2(64) == 64);

	unsigned int all_ones = 0xFFFFFFFF;
	assert((all_ones & BIT_MASK_32(0)) == 0x00000000);
	assert((all_ones & BIT_MASK_32(1)) == 0x00000001);
	assert((all_ones & BIT_MASK_32(2)) == 0x00000003);
	assert((all_ones & BIT_MASK_32(3)) == 0x00000007);
	assert((all_ones & BIT_MASK_32(10)) == 0x000003FF);
	assert((all_ones & BIT_MASK_32(16)) == 0x0000FFFF);
	assert((all_ones & BIT_MASK_32(17)) == 0x0001FFFF);
	assert((all_ones & BIT_MASK_32(25)) == 0x01FFFFFF);
	assert((all_ones & BIT_MASK_32(32)) == 0xFFFFFFFF);
}
#endif

#endif // BIT_H
