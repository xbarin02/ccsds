/**
 * \file dwtfloat.h
 * \brief Discrete wavelet transform routines
 */
#ifndef DWTFLOAT_H_
#define DWTFLOAT_H_

#include "frame.h"
#include "common.h"

int dwtfloat_encode(struct frame *frame);

int dwtfloat_decode(struct frame *frame);

#endif /* DWTFLOAT_H_ */
