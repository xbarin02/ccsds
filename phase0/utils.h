/**
 * \file utils.h
 * \brief Various useful routines
 */
#ifndef UTILS_H_
#define UTILS_H_

#include <stddef.h>
#include <limits.h>
#include <stdlib.h>

/**
 * \brief Error codes
 *
 * The standard (C89, 6.1.3.3 Enumeration constants) states that
 * an identifier declared as an enumeration constant has type \c int.
 * Therefore, it is fine if the function returning these constants
 * has return type \c int.
 */
enum return_t {
	/* 0x0000 successful completion */
	RET_SUCCESS                   = 0x0000, /**< success */
	/* 0x1xxx input/output errors */
	RET_FAILURE_FILE_IO           = 0x1000, /**< I/O error */
	RET_FAILURE_FILE_UNSUPPORTED  = 0x1001, /**< unsupported feature or file type */
	RET_FAILURE_FILE_OPEN         = 0x1002, /**< file open failure */
	/* 0x2xxx memory errors */
	RET_FAILURE_MEMORY_ALLOCATION = 0x2000, /**< unable to allocate dynamic memory */
	/* 0x3xxx general exceptions */
	RET_FAILURE_LOGIC_ERROR       = 0x3000, /**< faulty logic within the program */
	RET_LAST
};

/**
 * \brief Largest integral value not greater than argument
 *
 * Unlike floor(), this function does not require linking with \c -lm.
 */
static int floor_(double x)
{
	/*
	 * per C89 standard, 6.2.1.3 Floating and integral:
	 *
	 * When a value of floating type is convened to integral type,
	 * the fractional part is discarded. If the value of the integral part
	 * cannot be represented by the integral type, the behavior is
	 * undetined.
	 *
	 * "... discarded", i.e., the value is truncated toward zero
	 */

	/* truncate */
	int i = (int) x;

	/* convert trunc to floor */
	return i - (int) ( (double) i > x );
}

/**
 * \brief Round to nearest integer
 *
 * The round() function is not present in C89.
 */
#define round_(x) floor_( (x) + 0.5 )

/**
 * \brief Indicates the layout of multi-byte integers
 *
 * The macro should be defined on little endian architectures.
 */
#define SWAP_BYTE_ORDER

/**
 * \brief Convert big-endian to native byte order
 *
 * This function is similar to htons(). However the htons() is not present in C89.
 */
static unsigned short be_to_native_s(unsigned short a)
{
#ifdef SWAP_BYTE_ORDER
	return (unsigned short) (
		((a & 0xff00U) >> 8U) |
		((a & 0x00ffU) << 8U)
	);
#else
	return a;
#endif
}

/**
 * \brief Convert native byte order to big endian
 *
 * This function is similar to ntohs(). However the ntohs() is not present in C89.
 */
static unsigned short native_to_be_s(unsigned short a)
{
#ifdef SWAP_BYTE_ORDER
	return (unsigned short) (
		((a & 0xff00U) >> 8U) |
		((a & 0x00ffU) << 8U)
	);
#else
	return a;
#endif
}

/**
 * \brief Base-2 logarithm
 *
 * The result is undefined for zero \p n.
 */
static unsigned long floor_log2_l(unsigned long n)
{
	unsigned long r = 0;

	while (n >>= 1) {
		r++;
	}

	return r;
}

static size_t convert_maxval_to_bpp(unsigned long maxval)
{
	if (maxval) {
		return floor_log2_l(maxval) + 1;
	}

	return 0;
}

static unsigned long convert_bpp_to_maxval(size_t bpp)
{
	if (bpp) {
		return (1UL << bpp) - 1;
	}

	return 0;
}

/**
 * \brief Round \p n up to the nearest multiple of 8
 */
static size_t ceil_multiple8(size_t n)
{
	return (n + 7) / 8 * 8;
}

/**
 * \brief Convert \p bpp into the number of bytes required by the smallest type that can hold the \p bpp bit samples
 *
 * If no suitable type can be found, returns zero.
 */
static size_t convert_bpp_to_depth(size_t bpp)
{
	return bpp <= CHAR_BIT ? 1
		: ( bpp <= CHAR_BIT * sizeof(short) ? sizeof(short)
			: 0);
}

/**
 * \brief Returns the value of \p v constrained to the range \p lo to \p hi
 */
int clamp(int v, int lo, int hi)
{
	return v < lo ? lo : ( hi < v ? hi : v );
}

/**
 * \brief Compute the absolute value of an integer
 *
 * Unlike abs(), the absolute value of the most negative integer is defined to be INT_MAX.
 */
int abs_(int j)
{
	int r = abs(j);

	if (r < 0) {
		return INT_MAX;
	}

	return r;
}

#endif /* UTILS_H_ */
