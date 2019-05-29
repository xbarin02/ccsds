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
size_t ceil_multiple8(size_t n);

/**
 * \brief Checks whether the number \p n is even
 */
int is_even(ptrdiff_t n);

/**
 * \brief Checks whether the number \p n is multiple of 8
 */
int is_multiple8(ptrdiff_t n);

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
	size_t S;

	/**
	 * \brief Subband weights for Integer DWT
	 *
	 * The order of subbands is LL0, HL0, LH0, HH0, LL1, HL1, LH1, HH1, LL2, HL2, LH2, HH2.
	 * The LL0 and LL1 weight must be set to zero.
	 */
	int weight[12];
};

int init_parameters(struct parameters *parameters);

#endif /* COMMON_H_ */
