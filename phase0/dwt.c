#include "config.h"
#include "dwt.h"
#include "utils.h"

#include <stddef.h>
#include <assert.h>
#include <stdlib.h>

int dwtint_encode_line(int *line, size_t size, size_t stride)
{
	size_t n, N;

	assert( is_even(size) );

	N = size/2;

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

/*
 * The CCSDS standard states that pixel bit depth shall not exceed the limit
 * of 28 bits for the signed pixel type (sign bit + 27 bit magnitude).
 *
 * To support 28 bit pixels, one would need 32 (28 + 6 due to DWT) bits for
 * coefficients, 8 fractional bits for lifting scheme, and 23 < n <= 52 bits
 * for lifting constants, giving a total of 40 + n > 64 bits. Thus one would
 * need 64x64 -> 128 multiplication. Another possibility is to multiply 64-bit
 * lifting coefficients by constants in double-precision floating-point format
 * (single-precision format is not sufficient).
 *
 * The Open122 library supports 16-bit unsigned pixels.
 *
 * To support 16-bit input, one need 22 (16 + 6) for DWT coefficients,
 * at least 6 fractional bits for lifting coefficients, and at least 22
 * fractional bits for lifting constants. Thus 32x32 -> 64 multiplication
 * should be sufficient. Another possibility is to multiply the coefficients
 * by constants in single-precision (or double-) format. Yet another
 * possibility is to carry out the lifting scheme completelly in floats.
 *
 * Note that minimum long int size per the standard is 32 bits. This is
 * de facto standard on 32-bit machines (int and long int are 32-bit numbers).
 * Therefore no 32x32 -> 64 multiplication is available here. For these
 * reasons, the 32-bit integer for coefficients and 32-bit float for lifting
 * constants seems to be quite portable solution. Another portable solution is
 * to compute the lifting scheme completelly in 32-bit floats.
 */

#define alpha -1.58613434201888022056773162788538f
#define beta  -0.05298011857604780601431779000503f
#define gamma +0.88291107549260031282806293551600f
#define delta +0.44350685204939829327158029348930f
#define zeta  +1.14960439885900000000000000000000f

static void vert_2x1_switch(
	float *t0,
	float *t1,
	float *buff,
	int *lever
)
{
	const float w0 = +delta;
	const float w1 = +gamma;
	const float w2 = +beta;
	const float w3 = +alpha;

	float l0, l1, l2, l3;
	float c0, c1, c2, c3;
	float r0, r1, r2, r3;
	float x0, x1;
	float y0, y1;

	l0 = buff[0];
	l1 = buff[1];
	l2 = buff[2];
	l3 = buff[3];

	x0 = *t0;
	x1 = *t1;

	c0 = l1;
	c1 = l2;
	c2 = l3;
	c3 = x0;

	r3 = x1;
	r2 = c3 + w3 * ( (lever[3] < 0 ? r3 : l3) + (lever[3] > 0 ? l3 : r3) );
	r1 = c2 + w2 * ( (lever[2] < 0 ? r2 : l2) + (lever[2] > 0 ? l2 : r2) );
	r0 = c1 + w1 * ( (lever[1] < 0 ? r1 : l1) + (lever[1] > 0 ? l1 : r1) );
	y0 = c0 + w0 * ( (lever[0] < 0 ? r0 : l0) + (lever[0] > 0 ? l0 : r0) );
	y1 = r0;

	l0 = r0;
	l1 = r1;
	l2 = r2;
	l3 = r3;

	*t0 = y0;
	*t1 = y1;

	buff[0] = l0;
	buff[1] = l1;
	buff[2] = l2;
	buff[3] = l3;
}

int dwtfloat_encode_line_range(int *line, size_t size, size_t stride, float *buff, size_t n0, size_t n1)
{
	size_t m, N;
	float t[2];

	assert( is_even(size) );

	N = size/2;

#	define c(n) ( line[stride*(2*(n)+0)] )
#	define d(n) ( line[stride*(2*(n)+1)] )

	m = n0 + 2;

	if (n0 == 0) {
		m = 0;
	}

	/* prologue */
	for (; m < 1; m++) {
		int lever[4] = { 0, 0, 0, 0 };

		t[0] = (float) 0;
		t[1] = (float) c(m);
		vert_2x1_switch(t+0, t+1, buff, lever);
	}
	for (; m < 2; m++) {
		int lever[4] = { 0, 0, -1, 0 };

		t[0] = (float) d(m-1);
		t[1] = (float) c(m);
		vert_2x1_switch(t+0, t+1, buff, lever);
	}
	for (; m < 3; m++) {
		int lever[4] = { -1, 0, 0, 0 };

		t[0] = (float) d(m-1);
		t[1] = (float) c(m);
		vert_2x1_switch(t+0, t+1, buff, lever);
		c(m-2) = roundf_( t[0] * (  +zeta) );
		d(m-2) = roundf_( t[1] * (1/-zeta) );
	}
	/* regular */
	for (; m < n1 + 2 && m < N; m++) {
		int lever[4] = { 0, 0, 0, 0 };

		t[0] = (float) d(m-1);
		t[1] = (float) c(m);
		vert_2x1_switch(t+0, t+1, buff, lever);
		c(m-2) = roundf_( t[0] * (  +zeta) );
		d(m-2) = roundf_( t[1] * (1/-zeta) );
	}
	/* epilogue */
	for (; m < n1 + 2 && m == N; m++) {
		int lever[4] = { 0, 0, 0, +1 };

		t[0] = (float) d(m-1);
		t[1] = (float) 0;
		vert_2x1_switch(t+0, t+1, buff, lever);
		c(m-2) = roundf_( t[0] * (  +zeta) );
		d(m-2) = roundf_( t[1] * (1/-zeta) );
	}
	for (; m < n1 + 2 && m == N+1; m++) {
		int lever[4] = { 0, +1, 0, 0 };

		t[0] = (float) 0;
		t[1] = (float) 0;
		vert_2x1_switch(t+0, t+1, buff, lever);
		c(m-2) = roundf_( t[0] * (  +zeta) );
		d(m-2) = roundf_( t[1] * (1/-zeta) );
	}

#undef c
#undef d

	return RET_SUCCESS;
}

int dwtfloat_encode_line(int *line, size_t size, size_t stride)
{
#if 1
	float buff[4] = { .0f, .0f, .0f, .0f };

	assert( is_even(size) );

	return dwtfloat_encode_line_range(line, size, stride, buff, 0, size/2);
#else
	void *line_;
	size_t n, N;

	assert( is_even(size) );

	N = size/2;

	line_ = malloc( size * sizeof(float) );

	if (NULL == line_) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	assert( line );

	/* lifting */

#	define c(n) ((float *)line_)[2*(n)+0]
#	define d(n) ((float *)line_)[2*(n)+1]

	for (n = 0; n < N; ++n) {
		c(n) = (float) line[stride*(2*n+0)];
		d(n) = (float) line[stride*(2*n+1)];
	}

	/* alpha: predict D from C */
	for (n = 0; n < N-1; ++n) {
		d(n)   += alpha * (c(n) + c(n+1));
	}
		d(N-1) += alpha * (c(N-1) + c(N-1));

	/* beta: update C from D */
	for (n = 1; n < N; ++n) {
		c(n) += beta  * (d(n) + d(n-1));
	}
		c(0) += beta  * (d(0) + d(0));

	/* gamma: predict D from C */
	for (n = 0; n < N-1; ++n) {
		d(n)   += gamma * (c(n) + c(n+1));
	}
		d(N-1) += gamma  * (c(N-1) + c(N-1));

	/* delta: update C from D */
	for (n = 1; n < N; ++n) {
		c(n) += delta * (d(n) + d(n-1));
	}
		c(0) += delta * (d(0) + d(0));

	/* zeta: scaling */
	for (n = 0; n < N; ++n) {
		c(n) = c(n) * (  +zeta);
		d(n) = d(n) * (1/-zeta);
	}

	for (n = 0; n < N; ++n) {
		line[stride*(2*n+0)] = roundf_( c(n) );
		line[stride*(2*n+1)] = roundf_( d(n) );
	}

#	undef c
#	undef d

	free(line_);

	return RET_SUCCESS;
#endif
}

int dwtint_decode_line(int *line, size_t size, size_t stride)
{
	size_t n, N;

	assert( is_even(size) );

	N = size/2;

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

int dwtfloat_decode_line(int *line, size_t size, size_t stride)
{
	int *line_;
	size_t n, N;

	assert( is_even(size) );

	N = size/2;

	line_ = malloc( size * sizeof(float) );

	if (NULL == line_) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	assert( line );

	/* inverse lifting */

#	define c(n) ((float *)line_)[2*(n)+0]
#	define d(n) ((float *)line_)[2*(n)+1]

	for (n = 0; n < N; ++n) {
		c(n) = (float) line[stride*(2*n+0)];
		d(n) = (float) line[stride*(2*n+1)];
	}

	/* zeta: scaling */
	for (n = 0; n < N; ++n) {
		c(n) = c(n) * (1/+zeta);
		d(n) = d(n) * (  -zeta);
	}

	/* delta: update C from D */
	for (n = 1; n < N; ++n) {
		c(n) -= delta * (d(n) + d(n-1));
	}
		c(0) -= delta * (d(0) + d(0));

	/* gamma: predict D from C */
	for (n = 0; n < N-1; ++n) {
		d(n)   -= gamma * (c(n) + c(n+1));
	}
		d(N-1) -= gamma  * (c(N-1) + c(N-1));

	/* beta: update C from D */
	for (n = 1; n < N; ++n) {
		c(n) -= beta  * (d(n) + d(n-1));
	}
		c(0) -= beta  * (d(0) + d(0));

	/* alpha: predict D from C */
	for (n = 0; n < N-1; ++n) {
		d(n)   -= alpha * (c(n) + c(n+1));
	}
		d(N-1) -= alpha * (c(N-1) + c(N-1));

	for (n = 0; n < N; ++n) {
		line[stride*(2*n+0)] = roundf_( c(n) );
		line[stride*(2*n+1)] = roundf_( d(n) );
	}

#	undef c
#	undef d

	free(line_);

	return RET_SUCCESS;
}

void dwtint_weight_line(int *line, size_t size, size_t stride, int weight)
{
	size_t n;

	assert( line );

	for (n = 0; n < size; ++n) {
		line[stride*n] <<= weight;
	}
}

/*
 * inverse function to dwt_weight_line
 */
void dwtint_unweight_line(int *line, size_t size, size_t stride, int weight)
{
	size_t n;

	assert( line );

	for (n = 0; n < size; ++n) {
		line[stride*n] >>= weight;
	}
}

int dwtint_encode_band(int *band, size_t stride_y, size_t stride_x, size_t height, size_t width)
{
	size_t y, x;

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

int dwtfloat_encode_band(int *band, size_t stride_y, size_t stride_x, size_t height, size_t width)
{
	size_t y, x;

	/* for each row */
	for (y = 0; y < height; ++y) {
		/* invoke one-dimensional transform */
		dwtfloat_encode_line(band + y*stride_y, width, stride_x);
	}
	/* for each column */
	for (x = 0; x < width; ++x) {
		/* invoke one-dimensional transform */
		dwtfloat_encode_line(band + x*stride_x, height, stride_y);
	}

	return RET_SUCCESS;
}

int dwtfloat_decode_band(int *band, size_t stride_y, size_t stride_x, size_t height, size_t width)
{
	size_t y, x;

	/* for each column */
	for (x = 0; x < width; ++x) {
		/* invoke one-dimensional transform */
		dwtfloat_decode_line(band + x*stride_x, height, stride_y);
	}
	/* for each row */
	for (y = 0; y < height; ++y) {
		/* invoke one-dimensional transform */
		dwtfloat_decode_line(band + y*stride_y, width, stride_x);
	}

	return RET_SUCCESS;
}

int dwtint_decode_band(int *band, size_t stride_y, size_t stride_x, size_t height, size_t width)
{
	size_t y, x;

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

void dwtint_weight_band(int *band, size_t stride_y, size_t stride_x, size_t height, size_t width, int weight)
{
	size_t y;

	for (y = 0; y < height; ++y) {
		dwtint_weight_line(band + y*stride_y, width, stride_x, weight);
	}
}

void dwtint_unweight_band(int *band, size_t stride_y, size_t stride_x, size_t height, size_t width, int weight)
{
	size_t y;

	for (y = 0; y < height; ++y) {
		dwtint_unweight_line(band + y*stride_y, width, stride_x, weight);
	}
}

int dwtint_encode(struct frame *frame)
{
	int j;
	size_t height, width;
	int *data;

	assert( frame );

	height = ceil_multiple8(frame->height);
	width = ceil_multiple8(frame->width);

	assert( is_multiple8(width) && is_multiple8(height) );

	data = frame->data;

	assert( data );

	/* (2.2) forward two-dimensional transform */

	/* for each level */
	for (j = 0; j < 3; ++j) {
		/* number of elements for input */
		size_t height_j = height>>j, width_j = width>>j;

		/* stride of input data (for level j) */
		size_t stride_y = (1U << j) * width, stride_x = (1U << j) * 1;

		dwtint_encode_band(data, stride_y, stride_x, height_j, width_j);
	}

	/* (2.3) apply Subband Weights */

	for (j = 1; j < 4; ++j) {
		size_t height_j = height>>j, width_j = width>>j;

		size_t stride_y = (1U << j) * width, stride_x = (1U << j) * 1;

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

int dwtfloat_encode(struct frame *frame)
{
	int j;
	size_t height, width;
	int *data;

	assert( frame );

	height = ceil_multiple8(frame->height);
	width = ceil_multiple8(frame->width);

	assert( is_multiple8(width) && is_multiple8(height) );

	data = frame->data;

	assert( data );

	/* (2.2) forward two-dimensional transform */

	/* for each level */
	for (j = 0; j < 3; ++j) {
		/* number of elements for input */
		size_t height_j = height>>j, width_j = width>>j;

		/* stride of input data (for level j) */
		size_t stride_y = (1U << j) * width, stride_x = (1U << j) * 1;

		dwtfloat_encode_band(data, stride_y, stride_x, height_j, width_j);
	}

	return RET_SUCCESS;
}

int dwtint_decode(struct frame *frame)
{
	int j;
	size_t height, width;
	int *data;

	assert( frame );

	height = ceil_multiple8(frame->height);
	width = ceil_multiple8(frame->width);

	assert( is_multiple8(width) && is_multiple8(height) );

	data = frame->data;

	assert( data );

	/* undo Subband Weights */

	for (j = 1; j < 4; ++j) {
		size_t height_j = height>>j, width_j = width>>j;

		size_t stride_y = (1U << j) * width, stride_x = (1U << j) * 1;

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
		size_t height_j = height>>j, width_j = width>>j;

		size_t stride_y = (1U << j) * width, stride_x = (1U << j) * 1;

		dwtint_decode_band(data, stride_y, stride_x, height_j, width_j);
	}

	return RET_SUCCESS;
}

int dwtfloat_decode(struct frame *frame)
{
	int j;
	size_t height, width;
	int *data;

	assert( frame );

	height = ceil_multiple8(frame->height);
	width = ceil_multiple8(frame->width);

	assert( is_multiple8(width) && is_multiple8(height) );

	data = frame->data;

	assert( data );

	/* inverse two-dimensional transform */

	for (j = 2; j >= 0; --j) {
		size_t height_j = height>>j, width_j = width>>j;

		size_t stride_y = (1U << j) * width, stride_x = (1U << j) * 1;

		dwtfloat_decode_band(data, stride_y, stride_x, height_j, width_j);
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
