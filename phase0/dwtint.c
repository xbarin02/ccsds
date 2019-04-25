#include "config.h"
#include "common.h"
#include "dwt.h"
#include "dwtint.h"

#include <stddef.h>
#include <assert.h>

static int floor_div_pow2(int numerator, int log2_denominator)
{
	/* NOTE per C89 standard, the right shift of negative signed type is implementation-defined */
	if (numerator < 0)
		return ~(~numerator >> log2_denominator);
	else
		return numerator >> log2_denominator;
}

/**
 * \brief Round integer the fraction \f$ a/2^b \f$ to nearest integer
 *
 * Returns \f$ \mathrm{round} ( \mathrm{numerator} / 2^\mathrm{log2\_denominator} ) \f$.
 * The result is undefined for \p log2_denominator smaller than 1.
 */
static int round_div_pow2(int numerator, int log2_denominator)
{
	return floor_div_pow2(numerator + (1 << (log2_denominator - 1)), log2_denominator);
}

static void dwtint_encode_core(int data[2], int buff[5], const int lever[2])
{
	int c0 = buff[0];
	int c1 = buff[1];
	int c2 = buff[2];
	int d3 = buff[3];
	int d4 = buff[4];

	int x0 = data[0];
	int x1 = data[1];

	switch (lever[0]) {
		case -1:
			d3 = d3 - round_div_pow2(
				-1*c0 +9*c1 +9*c0 -1*x1,
				4
			);
			break;
		case +1:
			d3 = d3 - round_div_pow2(
				-1*c2 +9*c1 +9*c0 -1*c0,
				4
			);
			break;
		case +2:
			d3 = d3 - round_div_pow2(
				-1*c2 +9*c1 +9*c1 -1*c2,
				4
			);
			break;
		default:
			d3 = d3 - round_div_pow2(
				-1*c2 +9*c1 +9*c0 -1*x1,
				4
			);
	}

	buff[0] = x1;
	buff[1] = c0;
	buff[2] = c1;
	buff[3] = x0;
	buff[4] = d3;

	switch (lever[1]) {
		case -1:
			c1 = c1 - round_div_pow2(
				-1*d3 -1*d3,
				2
			);
			break;
		default:
			c1 = c1 - round_div_pow2(
				-1*d4 -1*d3,
				2
			);
	}

	data[0] = c1;
	data[1] = d3;
}

void dwtint_encode_line_segment(int *line, ptrdiff_t size, ptrdiff_t stride, int *buff, ptrdiff_t n0, ptrdiff_t n1)
{
	ptrdiff_t n, N;
	int data[2];

	assert( size > 0 && is_even(size) );

	N = size / 2;

#	define c(n) line[stride*(2*(n)+0)]
#	define d(n) line[stride*(2*(n)+1)]

	n = n0;

	/* prologue */
	for (; n < n1 && n < 1; n++) {
		int lever[2] = { 0, 0 };

		data[0] = 0;
		data[1] = (int) c(n);
		dwtint_encode_core(data, buff, lever);
	}
	for (; n < n1 && n < 2; n++) {
		int lever[2] = { 0, 0 };

		data[0] = (int) d(n-1);
		data[1] = (int) c(n);
		dwtint_encode_core(data, buff, lever);
	}
	for (; n < n1 && n < 3; n++) {
		int lever[2] = { -1, -1 };

		data[0] = (int) d(n-1);
		data[1] = (int) c(n);
		dwtint_encode_core(data, buff, lever); /* 0 */
		c(n-2) = ( data[0] );
		d(n-2) = ( data[1] );
	}
	/* regular */
	for (; n < n1 && n < N; n++) {
		int lever[2] = { 0, 0 };

		data[0] = (int) d(n-1);
		data[1] = (int) c(n);
		dwtint_encode_core(data, buff, lever); /* j */
		c(n-2) = ( data[0] );
		d(n-2) = ( data[1] );
	}
	/* epilogue */
	for (; n < n1 && n == N; n++) {
		int lever[2] = { +1, 0 };

		data[0] = (int) d(n-1);
		data[1] = 0;
		dwtint_encode_core(data, buff, lever); /* N-2 */
		c(n-2) = ( data[0] );
		d(n-2) = ( data[1] );
	}
	for (; n < n1 && n == N+1; n++) {
		int lever[2] = { +2, 0 };

		data[0] = 0;
		data[1] = 0;
		dwtint_encode_core(data, buff, lever); /* N-1 */
		c(n-2) = ( data[0] );
		d(n-2) = ( data[1] );
	}

#undef c
#undef d
}

int dwtint_encode_line(int *line, ptrdiff_t size, ptrdiff_t stride)
{
#if (CONFIG_DWT1_MODE == 2)
	ptrdiff_t N;
	int buff[5] = { 0, 0, 0, 0, 0 };

	assert( size > 0 && is_even(size) );

	N = size / 2;

	dwtint_encode_line_segment(line, size, stride, buff, 0, N+2);

	return RET_SUCCESS;
#endif
#if (CONFIG_DWT1_MODE == 1)
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
#endif
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
