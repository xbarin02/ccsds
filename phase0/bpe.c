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

int bpe_encode(struct frame *frame, const struct parameters *parameters, struct bio *bio)
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
			bpe_encode_block(data + y*width + x, width, bio);
		}
	}

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
