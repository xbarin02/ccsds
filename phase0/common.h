/**
 * \file common.h
 * \brief Common stuff
 */
#ifndef COMMON_H_
#define COMMON_H_

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
