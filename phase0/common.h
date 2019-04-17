/**
 * \file common.h
 * \brief Common stuff and various useful routines
 */
#ifndef COMMON_H_
#define COMMON_H_

#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* FIXME */
#pragma GCC diagnostic ignored "-Wunused-function"

/**
 * \brief Error codes
 *
 * The standard (C89, 6.1.3.3 Enumeration constants) states that
 * an identifier declared as an enumeration constant has type \c int.
 * Therefore, it is fine if the function returning these constants
 * has return type \c int.
 */
enum {
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
	RET_FAILURE_OVERFLOW_ERROR    = 0x3001, /**< result is too large for the destination type */
	RET_LAST
};

/**
 * \brief Round \p n up to the nearest multiple of 8
 */
static size_t ceil_multiple8(size_t n)
{
	return (n + 7) / 8 * 8;
}

/**
 * \brief Returns the value of \p v constrained to the range \p lo to \p hi
 */
int clamp(int v, int lo, int hi);

/**
 * \brief Compute the absolute value of an integer
 *
 * Unlike abs(), the absolute value of the most negative integer is defined to be \c INT_MAX.
 */
int safe_abs(int j);

/**
 * \brief Round integer the fraction \f$ a/2^b \f$ to nearest integer
 *
 * Returns \f$ \mathrm{round} ( \mathrm{numerator} / 2^\mathrm{log2\_denominator} ) \f$.
 * The result is undefined for \p log2_denominator smaller than 1.
 */
static int round_div_pow2(int numerator, int log2_denominator)
{
	/* NOTE per C89 standard, the right shift of negative signed type is implementation-defined */
	return (numerator + (1 << (log2_denominator - 1)) ) >> log2_denominator;
}

/**
 * \brief Checks whether the number \p n is even
 */
static int is_even(ptrdiff_t n)
{
	/* size_t is unsigned integer type */
	return !((size_t) n & 1);
}

/**
 * \brief Checks whether the number \p n is multiple of 8
 */
static int is_multiple8(ptrdiff_t n)
{
	return !((size_t) n & 7);
}

/**
 * \brief Debugging \c printf
 *
 * If the macro \c NDEBUG is defined, this macro generates no code, and hence
 * does nothing at all. Otherwise, the macro acts as the fprintf function and
 * writes its output to stderr.
 */
#ifdef NDEBUG
#	define dprint(arg)
#else
#	define dprint(arg) eprintf arg
#endif

/**
 * \brief Formatted output, write output to \c stderr
 */
int eprintf(const char *format, ...);

/**
 * \brief Compression parameters
 */
struct parameters {
	/**
	 * \brief Wavelet transform type
	 *
	 * Specifies DWT type:
	 * - 0: Float DWT
	 * - 1: Integer DWT
	 */
	int DWTtype;

	 /**
	  * \brief Segment size
	  *
	  * segment size in blocks
	  * A segment is defined as a group of S consecutive blocks.
	  * \f$ 16 \le S \le 2^{20} \f$
	  */
	unsigned S;
};

#endif /* COMMON_H_ */
