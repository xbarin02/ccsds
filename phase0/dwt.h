/**
 * \file dwt.h
 * \brief Discrete wavelet transform routines
 */
#ifndef DWT_H_
#define DWT_H_

#include "frame.h"
#include "common.h"

/**
 * \brief Forward wavelet transform
 *
 * The transform is computed <em>in situ</em> using the \p frame buffer.
 * Either Float or Integer DWT is used, according to the \p parameters.
 */
int dwt_encode(struct frame *frame, const struct parameters *parameters);

/**
 * \brief Inverse wavelet transform
 *
 * The transform is computed <em>in situ</em> using the \p frame buffer.
 * Either Float or Integer DWT is used, according to the \p parameters.
 */
int dwt_decode(struct frame *frame, const struct parameters *parameters);

#endif /* DWT_H_ */
