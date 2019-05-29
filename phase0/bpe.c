#include "bpe.h"
#include <assert.h>

int bpe_encode_block(int *data, ptrdiff_t stride, struct bio *bio)
{
	ptrdiff_t y, x;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			bio_write_int(bio, data[y*stride + x]);
		}
	}

	return 0;
}

int bpe_decode_block(int *data, ptrdiff_t stride, struct bio *bio)
{
	ptrdiff_t y, x;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			bio_read_int(bio, &data[y*stride + x]);
		}
	}

	return 0;
}

size_t get_total_no_blocks(struct frame *frame)
{
	size_t height, width;

	assert(frame);

	height = ceil_multiple8(frame->height);
	width  = ceil_multiple8(frame->width);

	return height / 8 * width / 8;
}

int bpe_encode_block_by_index(struct frame *frame, struct bio *bio, size_t block_index)
{
	size_t height, width;
	size_t y, x;
	int *data;

	assert(frame);

	height = ceil_multiple8(frame->height);
	width  = ceil_multiple8(frame->width);

	y = block_index / (height / 8) * 8;
	x = block_index % (height / 8) * 8;

	data = frame->data;

	return bpe_encode_block(data + y*width + x, width, bio);
}

int bpe_encode(struct frame *frame, const struct parameters *parameters, struct bio *bio)
{
#if 0
	ptrdiff_t height, width;
	ptrdiff_t y, x;
	int *data;

	assert(frame);

	height = (ptrdiff_t) ceil_multiple8(frame->height);
	width  = (ptrdiff_t) ceil_multiple8(frame->width);

	data = frame->data;

	for (y = 0; y < height; y += 8) {
		for (x = 0; x < width; x += 8) {
			bpe_encode_block(data + y*width + x, width, bio);
		}
	}
#else
	size_t total_no_blocks;
	size_t block_index;

	total_no_blocks = get_total_no_blocks(frame);

	for (block_index = 0; block_index < total_no_blocks; ++block_index) {
		bpe_encode_block_by_index(frame, bio, block_index);
	}
#endif
	return 0;
}

int bpe_decode(struct frame *frame, const struct parameters *parameters, struct bio *bio)
{
	ptrdiff_t height, width;
	ptrdiff_t y, x;
	int *data;

	assert(frame);

	height = (ptrdiff_t) ceil_multiple8(frame->height);
	width  = (ptrdiff_t) ceil_multiple8(frame->width);

	data = frame->data;

	for (y = 0; y < height; y += 8) {
		for (x = 0; x < width; x += 8) {
			bpe_decode_block(data + y*width + x, width, bio);
		}
	}

	return 0;
}

size_t get_maximum_stream_size(struct frame *frame)
{
	size_t width, height;

	assert(frame);

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	return height * width * sizeof(int);
}
