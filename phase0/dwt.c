#include "config.h"
#include "dwt.h"
#include "utils.h"
#include "dwtfloat.h"

#include <stddef.h>
#include <assert.h>
#include <stdlib.h>

int dwtint_encode_line(int *line, ptrdiff_t size, ptrdiff_t stride)
{
	ptrdiff_t n, N;

	assert( size > 0 && is_even(size) );

	N = size / 2;

	/* lifting */

	/* subbands: D (H) at odd indices, C (L) at even indices */
#define c(n) line[stride*(2*(n)+0)]
#define d(n) line[stride*(2*(n)+1)]

	assert( line );

	d(0) = d(0) - round_div_pow2(
		-1*c(1) +9*c(0) +9*c(1) -1*c(2),
		4
	);

	for (n = 1; n <= N-3; ++n) {
		d(n) = d(n) - round_div_pow2(
			-1*c(n-1) +9*c(n) +9*c(n+1) -1*c(n+2),
			4
		);
	}

	d(N-2) = d(N-2) - round_div_pow2(
		-1*c(N-3) +9*c(N-2) +9*c(N-1) -1*c(N-1),
		4
	);

	d(N-1) = d(N-1) - round_div_pow2(
		-1*c(N-2) +9*c(N-1),
		3
	);

	c(0) = c(0) - round_div_pow2(-d(0), 1);

	for (n = 1; n <= N-1; ++n) {
		c(n) = c(n) - round_div_pow2(
			-1*d(n-1) -1*d(n),
			2
		);
	}

#undef c
#undef d

	return RET_SUCCESS;
}

int dwtint_decode_line(int *line, ptrdiff_t size, ptrdiff_t stride)
{
	ptrdiff_t n, N;

	assert( size > 0 && is_even(size) );

	N = size / 2;

#define c(n) line[stride*(2*(n)+0)]
#define d(n) line[stride*(2*(n)+1)]

	assert( line );

	/* inverse lifting */

	c(0) = c(0) + round_div_pow2(-d(0), 1);

	for (n = 1; n <= N-1; ++n) {
		c(n) = c(n) + round_div_pow2(
			-1*d(n-1) -1*d(n),
			2
		);
	}

	d(0) = d(0) + round_div_pow2(
		-1*c(1) +9*c(0) +9*c(1) -1*c(2),
		4
	);

	for (n = 1; n <= N-3; ++n) {
		d(n) = d(n) + round_div_pow2(
			-1*c(n-1) +9*c(n) +9*c(n+1) -1*c(n+2),
			4
		);
	}

	d(N-2) = d(N-2) + round_div_pow2(
		-1*c(N-3) +9*c(N-2) +9*c(N-1) -1*c(N-1),
		4
	);

	d(N-1) = d(N-1) + round_div_pow2(
		-1*c(N-2) +9*c(N-1),
		3
	);

#undef c
#undef d

	return RET_SUCCESS;
}

void dwtint_weight_line(int *line, ptrdiff_t size, ptrdiff_t stride, int weight)
{
	ptrdiff_t n;

	assert( line );

	for (n = 0; n < size; ++n) {
		line[stride*n] <<= weight;
	}
}

/*
 * inverse function to dwt_weight_line
 */
void dwtint_unweight_line(int *line, ptrdiff_t size, ptrdiff_t stride, int weight)
{
	ptrdiff_t n;

	assert( line );

	for (n = 0; n < size; ++n) {
		line[stride*n] >>= weight;
	}
}

int dwtint_encode_band(int *band, ptrdiff_t stride_y, ptrdiff_t stride_x, ptrdiff_t height, ptrdiff_t width)
{
	ptrdiff_t y, x;

	/* for each row */
	for (y = 0; y < height; ++y) {
		/* invoke one-dimensional transform */
		dwtint_encode_line(band + y*stride_y, width, stride_x);
	}
	/* for each column */
	for (x = 0; x < width; ++x) {
		/* invoke one-dimensional transform */
		dwtint_encode_line(band + x*stride_x, height, stride_y);
	}

	return RET_SUCCESS;
}

int dwtint_decode_band(int *band, ptrdiff_t stride_y, ptrdiff_t stride_x, ptrdiff_t height, ptrdiff_t width)
{
	ptrdiff_t y, x;

	/* for each column */
	for (x = 0; x < width; ++x) {
		/* invoke one-dimensional transform */
		dwtint_decode_line(band + x*stride_x, height, stride_y);
	}
	/* for each row */
	for (y = 0; y < height; ++y) {
		/* invoke one-dimensional transform */
		dwtint_decode_line(band + y*stride_y, width, stride_x);
	}

	return RET_SUCCESS;
}

void dwtint_weight_band(int *band, ptrdiff_t stride_y, ptrdiff_t stride_x, ptrdiff_t height, ptrdiff_t width, int weight)
{
	ptrdiff_t y;

	for (y = 0; y < height; ++y) {
		dwtint_weight_line(band + y*stride_y, width, stride_x, weight);
	}
}

void dwtint_unweight_band(int *band, ptrdiff_t stride_y, ptrdiff_t stride_x, ptrdiff_t height, ptrdiff_t width, int weight)
{
	ptrdiff_t y;

	for (y = 0; y < height; ++y) {
		dwtint_unweight_line(band + y*stride_y, width, stride_x, weight);
	}
}

int dwtint_encode(struct frame *frame)
{
	int j;
	ptrdiff_t height, width;
	int *data;

	assert( frame );

	height = (ptrdiff_t) ceil_multiple8(frame->height);
	width  = (ptrdiff_t) ceil_multiple8(frame->width);

	assert( is_multiple8(width) && is_multiple8(height) );

	data = frame->data;

	assert( data );

	/* (2.2) forward two-dimensional transform */

	/* for each level */
	for (j = 0; j < 3; ++j) {
		/* number of elements for input */
		ptrdiff_t height_j = height >> j, width_j = width >> j;

		/* stride of input data (for level j) */
		ptrdiff_t stride_y = width << j, stride_x = 1 << j;

		dwtint_encode_band(data, stride_y, stride_x, height_j, width_j);
	}

	/* (2.3) apply Subband Weights */

	for (j = 1; j < 4; ++j) {
		ptrdiff_t height_j = height >> j, width_j = width >> j;

		ptrdiff_t stride_y = width << j, stride_x = 1 << j;

		int *band_ll = data +          0 +          0;
		int *band_hl = data +          0 + stride_x/2;
		int *band_lh = data + stride_y/2 +          0;
		int *band_hh = data + stride_y/2 + stride_x/2;

		dwtint_weight_band(band_hl, stride_y, stride_x, height_j, width_j, j); /* HL */
		dwtint_weight_band(band_lh, stride_y, stride_x, height_j, width_j, j); /* LH */
		dwtint_weight_band(band_hh, stride_y, stride_x, height_j, width_j, j-1); /* HH */

		if (j < 3)
			continue;

		dwtint_weight_band(band_ll, stride_y, stride_x, height_j, width_j, j); /* LL */
	}

	return RET_SUCCESS;
}

int dwtint_decode(struct frame *frame)
{
	int j;
	ptrdiff_t height, width;
	int *data;

	assert( frame );

	height = (ptrdiff_t) ceil_multiple8(frame->height);
	width  = (ptrdiff_t) ceil_multiple8(frame->width);

	assert( is_multiple8(width) && is_multiple8(height) );

	data = frame->data;

	assert( data );

	/* undo Subband Weights */

	for (j = 1; j < 4; ++j) {
		ptrdiff_t height_j = height >> j, width_j = width >> j;

		ptrdiff_t stride_y = width << j, stride_x = 1 << j;

		int *band_ll = data +          0 +          0;
		int *band_hl = data +          0 + stride_x/2;
		int *band_lh = data + stride_y/2 +          0;
		int *band_hh = data + stride_y/2 + stride_x/2;

		dwtint_unweight_band(band_hl, stride_y, stride_x, height_j, width_j, j); /* HL */
		dwtint_unweight_band(band_lh, stride_y, stride_x, height_j, width_j, j); /* LH */
		dwtint_unweight_band(band_hh, stride_y, stride_x, height_j, width_j, j-1); /* HH */

		if (j < 3)
			continue;

		dwtint_unweight_band(band_ll, stride_y, stride_x, height_j, width_j, j); /* LL */
	}

	/* inverse two-dimensional transform */

	for (j = 2; j >= 0; --j) {
		ptrdiff_t height_j = height >> j, width_j = width >> j;

		ptrdiff_t stride_y = width << j, stride_x = 1 << j;

		dwtint_decode_band(data, stride_y, stride_x, height_j, width_j);
	}

	return RET_SUCCESS;
}

int dwt_encode(struct frame *frame, const struct parameters *parameters)
{
	assert( parameters );

	switch (parameters->DWTtype) {
		case 0:
			return dwtfloat_encode(frame);
		case 1:
			return dwtint_encode(frame);
		default:
			return RET_FAILURE_LOGIC_ERROR;
	}
}

int dwt_decode(struct frame *frame, const struct parameters *parameters)
{
	assert( parameters );

	switch (parameters->DWTtype) {
		case 0:
			return dwtfloat_decode(frame);
		case 1:
			return dwtint_decode(frame);
		default:
			return RET_FAILURE_LOGIC_ERROR;
	}
}
