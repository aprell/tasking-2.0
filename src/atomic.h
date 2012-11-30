#ifndef ATOMIC_H
#define ATOMIC_H

// Adapted from http://golubenco.org/2007/06/14/atomic-operations

/**
 * @brief Atomic type
 */

typedef int atomic_t;

#define ATOMIC_INIT(i)	(i)

/**
 * @brief Atomic read
 *
 * @param v pointer of type atomic_t
 */

static inline int atomic_read(atomic_t *v)
{
	return *(volatile int *)v;
}

/**
 * @brief Atomic write
 *
 * @param v pointer of type atomic_t
 * @param i new integer value
 */

static inline void atomic_set(atomic_t *v, int i)
{
	*v = i;
}

/**
 * @brief Atomic add
 *
 * @param i integer value to add
 * @param v pointer of type atomic_t
 * @return new value of @v
 */

static inline int atomic_add(int i, atomic_t *v)
{
	return __sync_add_and_fetch(v, i);
}

/**
 * @brief Atomic subtract
 *
 * @param i integer value to subtract
 * @param v pointer of type atomic_t
 * @return new value of @v
 */

static inline int atomic_sub(int i, atomic_t *v)
{
	return __sync_sub_and_fetch(v, i);
}

/**
 * @brief Atomic subtract and test if zero
 *
 * @param i integer value to subtract
 * @param v pointer of type atomic_t
 *
 * Atomically subtracts @i from @v and returns true if the result is 0 
 * and false otherwise.
 */

static inline int atomic_sub_and_test(int i, atomic_t *v)
{
	return !(__sync_sub_and_fetch(v, i));
}

/**
 * @brief Atomic add and test if zero
 *
 * @param i integer value to add
 * @param v pointer of type atomic_t
 *
 * Atomically adds @i to @v and returns true if the result is 0
 * and false otherwise.
 */

static inline int atomic_add_and_test(int i, atomic_t *v)
{
	return !(__sync_add_and_fetch(v, i));
}

/**
 * @brief Atomic increment
 *
 * @param v pointer of type atomic_t
 * @return incremented value of @v
 */

#define atomic_inc(v)	atomic_add(1, v)

/**
 * @brief Atomic decrement
 *
 * @param v: pointer of type atomic_t
 * @return decremented value of @v
 */

#define atomic_dec(v)	atomic_sub(1, v)

/**
 * @brief Atomic decrement and test if zero
 *
 * @param v pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and returns true if the result is 0
 * and false otherwise.
 */

#define atomic_dec_and_test(v)	atomic_sub_and_test(1, v)

/**
 * @brief Atomic increment and test if zero
 *
 * @param v pointer of type atomic_t
 *
 * Atomically increments @v by 1 and returns true if the result is 0
 * and false otherwise.
 */

#define atomic_inc_and_test(v)	atomic_add_and_test(1, v)

/**
 * @brief Compare and swap (CAS)
 *
 * @param v pointer of type atomic_t
 * @param oldval old integer value
 * @param newval new integer value
 * @return true if comparison was successful and @newval was written, 
 * false otherwise
 *
 * Atomically tests if @v == @oldval, and if true sets @v to @newval.
 */

static inline int atomic_CAS(atomic_t *v, int oldval, int newval)
{
	return __sync_bool_compare_and_swap(v, oldval, newval);
}

// Prevents the compiler from reordering memory operations
#define __compiler_barrier() \
__asm__ __volatile__ ("" : : : "memory")

// Full memory barrier (includes compiler barriers for safety)
// See http://gcc.gnu.org/ml/gcc/2011-09/msg00088.html
#define __memory_barrier() \
{ \
	__compiler_barrier(); \
	__sync_synchronize(); \
	__compiler_barrier(); \
}

#endif // ATOMIC_H
