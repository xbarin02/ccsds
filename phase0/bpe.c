#include "bpe.h"
#include <assert.h>
#include <stdlib.h>

struct bpe {
	size_t S;

	/* S blocks (the S is given in struct parameters) */
	/* i.e. Sx8x8 32-bit integers */
	INT32 *segment;

	size_t block_index;

	struct bio *bio;
};

int bpe_init(struct bpe *bpe, const struct parameters *parameters, struct bio *bio)
{
	size_t S;

	assert(bpe);
	assert(parameters);

	S = parameters->S;

	bpe->segment = malloc( S * 8 * 8 * sizeof(INT32) );

	if (bpe->segment == NULL) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	bpe->S = S;

	bpe->bio = bio;

	bpe->block_index = 0;

	return RET_SUCCESS;
}

int bpe_destroy(struct bpe *bpe)
{
	assert(bpe);

	free(bpe->segment);

	return RET_SUCCESS;
}

int bpe_encode_block(INT32 *data, size_t stride, struct bio *bio)
{
	size_t y, x;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			bio_write_int(bio, (UINT32) data[y*stride + x]); /* FIXME INT32 -> UINT32 */
		}
	}

	return RET_SUCCESS;
}

int bpe_push_block(struct bpe *bpe, INT32 *data, size_t stride)
{
	size_t S;
	size_t s;
	INT32 *local;
	size_t y, x;

	assert(bpe);

	/* push block into bpe->segment[] at the index (block_index%S) */

	S = bpe->S;
	s = bpe->block_index % S;
	local = bpe->segment + s*8*8;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			local[y*8 + x] = (INT32) data[y*stride + x]; /* FIXME int -> INT32 */
		}
	}

	/* if this is the last block in the segment, serialize the segment into bpe->bio */
	if (s + 1 == S) {
		size_t blk;
		/* encode this segment */
		dprint (("BPE: encoding segment %lu\n", (bpe->block_index / S)));

		for (blk = 0; blk < S; ++blk) {
			/* encode the block */
			bpe_encode_block(bpe->segment + blk*8*8, 8, bpe->bio);
		}
	}

	/* next block will be... */
	bpe->block_index ++;

	return RET_SUCCESS;
}

int bpe_flush(struct bpe *bpe)
{
	size_t S;
	size_t s;

	S = bpe->S;
	s = bpe->block_index % S;

	if (s > 0) {
		size_t blk;

		dprint (("BPE: flushing %lu blocks\n", s));

		for (blk = 0; blk < s; ++blk) {
			bpe_encode_block(bpe->segment + blk*8*8, 8, bpe->bio);
		}
	}

	return RET_SUCCESS;
}

int bpe_decode_block(int *data, size_t stride, struct bio *bio)
{
	size_t y, x;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			bio_read_int(bio, (UINT32 *) &data[y*stride + x]); /* FIXME UINT32 -> int */
		}
	}

	return RET_SUCCESS;
}

size_t get_total_no_blocks(struct frame *frame)
{
	size_t height, width;

	assert(frame);

	height = ceil_multiple8(frame->height);
	width  = ceil_multiple8(frame->width);

	return height / 8 * width / 8;
}

struct block {
	int *data;
	size_t stride;
};

int block_by_index(struct block *block, struct frame *frame, size_t block_index)
{
	size_t width;
	size_t y, x;
	int *data;

	assert(frame);

	width  = ceil_multiple8(frame->width);

	y = block_index / (width / 8) * 8;
	x = block_index % (width / 8) * 8;

	data = frame->data;

	assert(block);

	block->data = data + y*width + x;
	block->stride = width;

	return 0;
}

int bpe_encode_block_by_index(struct frame *frame, struct bio *bio, size_t block_index)
{
	struct block block;

	block_by_index(&block, frame, block_index);

	return bpe_encode_block(block.data, block.stride, bio);
}

int bpe_decode_block_by_index(struct frame *frame, struct bio *bio, size_t block_index)
{
	struct block block;

	block_by_index(&block, frame, block_index);

	return bpe_decode_block(block.data, block.stride, bio);
}

int bpe_encode(struct frame *frame, const struct parameters *parameters, struct bio *bio)
{
#if 0
	size_t total_no_blocks;
	size_t block_index;
	size_t S;

	total_no_blocks = get_total_no_blocks(frame);

	assert(parameters);

	S = parameters->S;

	for (block_index = 0; block_index < total_no_blocks; ++block_index) {
		if (block_index % S == 0) {
			dprint (("segment %lu\n", (unsigned long)(block_index / S)));
		}
		bpe_encode_block_by_index(frame, bio, block_index);
	}

	return RET_SUCCESS;
#else
	size_t block_index;
	size_t total_no_blocks;
	struct bpe bpe;

	total_no_blocks = get_total_no_blocks(frame);

	bpe_init(&bpe, parameters, bio);

	/* push all blocks into the BPE engine */
	for (block_index = 0; block_index < total_no_blocks; ++block_index) {
		struct block block;

		block_by_index(&block, frame, block_index);

		bpe_push_block(&bpe, block.data, block.stride);
	}

	bpe_flush(&bpe);

	bpe_destroy(&bpe);

	return RET_SUCCESS;
#endif
}

int bpe_decode(struct frame *frame, const struct parameters *parameters, struct bio *bio)
{
	size_t total_no_blocks;
	size_t block_index;

	total_no_blocks = get_total_no_blocks(frame);

	for (block_index = 0; block_index < total_no_blocks; ++block_index) {
		bpe_decode_block_by_index(frame, bio, block_index);
	}

	return RET_SUCCESS;
}

size_t get_maximum_stream_size(struct frame *frame)
{
	size_t width, height;

	assert(frame);

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	return height * width * sizeof(int);
}
