/**
 * \file dwtint.h
 * \brief Discrete wavelet transform routines
 */
#ifndef DWTINT_H_
#define DWTINT_H_

#include "frame.h"
#include "common.h"

int dwtint_encode(struct frame *frame);

int dwtint_decode(struct frame *frame);

#endif /* DWTINT_H_ */
