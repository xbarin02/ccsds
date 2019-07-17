/**
 * \file bpe.h
 * \brief Bit-plane encoder and decoder
 */
#ifndef BPE_H_
#define BPE_H_

#include "common.h"
#include "frame.h"
#include "bio.h"

struct bpe {
	/* the number of block in the segment,
	 * the S is given in struct parameters */
	size_t S;

	/* local copy of S blocks, the size is 64*S = 8*8*S 32-bit integers */
	INT32 *segment;

	size_t block_index;

	struct bio *bio;
};

int bpe_init(struct bpe *bpe, const struct parameters *parameters, struct bio *bio);

int bpe_push_block(struct bpe *bpe, INT32 *data, size_t stride);
int bpe_pop_block(struct bpe *bpe, INT32 *data, size_t stride, size_t total_no_blocks);

int bpe_destroy(struct bpe *bpe);

/* helper function (to be removed in future) */
size_t get_total_no_blocks(struct frame *frame);

int bpe_encode(struct frame *frame, const struct parameters *parameters, struct bio *bio);

int bpe_decode(struct frame *frame, const struct parameters *parameters, struct bio *bio);

size_t get_maximum_stream_size(struct frame *frame);

#endif /* BPE_H_ */
