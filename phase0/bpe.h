/**
 * \file bpe.h
 * \brief Bit-plane encoder and decoder
 */
#ifndef BPE_H_
#define BPE_H_

#include "common.h"
#include "frame.h"
#include "bio.h"

int bpe_encode(struct frame *frame, const struct parameters *parameters, struct bio *bio);

int bpe_decode(struct frame *frame, const struct parameters *parameters, struct bio *bio);

size_t get_maximum_stream_size(struct frame *frame);

#endif /* BPE_H_ */
