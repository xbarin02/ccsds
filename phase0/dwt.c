#include "config.h"
#include "dwt.h"
#include "utils.h"

#include <stddef.h>
#include <assert.h>
#include <stdlib.h>

int dwtint_encode_line(int *line, size_t size, size_t stride)
{
	int *line_;
	int *D, *C;
	size_t n, N;

	assert( (size&1) == 0 );

	N = size/2;

	line_ = malloc( size * sizeof(int) );

	if (NULL == line_) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	C = line_;
	D = line_ + N;

	/* lifting */

	/* subbands: D (H) at odd indices, C (L) at even indices */
#define c(n) line[stride*(2*(n)+0)]
#define d(n) line[stride*(2*(n)+1)]

	assert( line );

	D[0] = d(0) - round_div_pow2(
		-1*c(1) +9*c(0) +9*c(1) -1*c(2),
		4
	);

	for (n = 1; n <= N-3; ++n) {
		D[n] = d(n) - round_div_pow2(
			-1*c(n-1) +9*c(n) +9*c(n+1) -1*c(n+2),
			4
		);
	}

	D[N-2] = d(N-2) - round_div_pow2(
		-1*c(N-3) +9*c(N-2) +9*c(N-1) -1*c(N-1),
		4
	);

	D[N-1] = d(N-1) - round_div_pow2(
		-1*c(N-2) +9*c(N-1),
		3
	);

	C[0] = c(0) - round_div_pow2(-D[0], 1);

	for (n = 1; n <= N-1; ++n) {
		C[n] = c(n) - round_div_pow2(
			-1*D[n-1] -1*D[n],
			2
		);
	}

#ifndef DWT_LAYOUT_INTERLEAVED
	/* unpack */
	for (n = 0; n < size; ++n) {
		line[stride*n] = line_[n];
	}
#else
	/* keep interleaved */
	for (n = 0; n < N; ++n) {
		c(n) = C[n];
		d(n) = D[n];
	}
#endif

#undef c
#undef d

	free(line_);

	return RET_SUCCESS;
}

int dwtfloat_encode_line(int *line, size_t size, size_t stride)
{
	int *line_;
	int *D, *C;
	size_t n;

	assert( (size&1) == 0 );

	line_ = malloc( size * sizeof(int) );

	if (NULL == line_) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	C = line_;
	D = line_ + size/2;

	/* convolution */

	assert( line );

#	define x(m) ( (m) & (size_t)1<<(sizeof(size_t)*CHAR_BIT-1) ? line[stride*-(m)] : \
		( (m) > (size-1) ? line[stride*(2*(size-1)-(m))] : \
		line[stride*(m)] ) )

	for (n = 0; n < size/2; ++n) {
		C[n] = (int) round_ (
			+0.037828455507 * x(2*n-4)
			-0.023849465020 * x(2*n-3)
			-0.110624404418 * x(2*n-2)
			+0.377402855613 * x(2*n-1)
			+0.852698679009 * x(2*n+0)
			+0.377402855613 * x(2*n+1)
			-0.110624404418 * x(2*n+2)
			-0.023849465020 * x(2*n+3)
			+0.037828455507 * x(2*n+4)
		);

		D[n] = (int) round_ (
			-0.064538882629 * x(2*n+1-3)
			+0.040689417609 * x(2*n+1-2)
			+0.418092273222 * x(2*n+1-1)
			-0.788485616406 * x(2*n+1+0)
			+0.418092273222 * x(2*n+1+1)
			+0.040689417609 * x(2*n+1+2)
			-0.064538882629 * x(2*n+1+3)
		);
	}

#	undef x

#ifndef DWT_LAYOUT_INTERLEAVED
	/* unpack */
	for (n = 0; n < size; ++n) {
		line[stride*n] = line_[n];
	}
#else
	/* keep interleaved */
	for (n = 0; n < size/2; ++n) {
		line[stride*(2*n+0)] = C[n];
		line[stride*(2*n+1)] = D[n];
	}
#endif

	free(line_);

	return RET_SUCCESS;
}

int dwtint_decode_line(int *line, size_t size, size_t stride)
{
	int *line_;
	int *D, *C;
	size_t n, N;

	assert( (size&1) == 0 );

	N = size/2;

	line_ = malloc( size * sizeof(int) );

	if (NULL == line_) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	C = line_;
	D = line_ + N;

#define c(n) line[stride*(2*(n)+0)]
#define d(n) line[stride*(2*(n)+1)]

	assert( line );

#ifndef DWT_LAYOUT_INTERLEAVED
	/* pack */
	for (n = 0; n < size; ++n) {
		line_[n] = line[stride*n];
	}
#else
	/* line[] is interleaved */
	for (n = 0; n < N; ++n) {
		C[n] = c(n);
		D[n] = d(n);
	}
#endif
	/* inverse lifting */

	c(0) = C[0] + round_div_pow2(-D[0], 1);

	for (n = 1; n <= N-1; ++n) {
		c(n) = C[n] + round_div_pow2(
			-1*D[n-1] -1*D[n],
			2
		);
	}

	d(0) = D[0] + round_div_pow2(
		-1*c(1) +9*c(0) +9*c(1) -1*c(2),
		4
	);

	for (n = 1; n <= N-3; ++n) {
		d(n) = D[n] + round_div_pow2(
			-1*c(n-1) +9*c(n) +9*c(n+1) -1*c(n+2),
			4
		);
	}

	d(N-2) = D[N-2] + round_div_pow2(
		-1*c(N-3) +9*c(N-2) +9*c(N-1) -1*c(N-1),
		4
	);

	d(N-1) = D[N-1] + round_div_pow2(
		-1*c(N-2) +9*c(N-1),
		3
	);

#undef c
#undef d

	free(line_);

	return RET_SUCCESS;
}

int dwtfloat_decode_line(int *line, size_t size, size_t stride)
{
	int *line_;
	int *D, *C;
	size_t n;

	assert( (size&1) == 0 );

	line_ = malloc( size * sizeof(int) );

	if (NULL == line_) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	C = line_;
	D = line_ + size/2;

	assert( line );

#ifndef DWT_LAYOUT_INTERLEAVED
	/* pack */
	for (n = 0; n < size; ++n) {
		line_[n] = line[stride*n];
	}
#else
	/* line[] is interleaved */
	for (n = 0; n < size/2; ++n) {
		C[n] = line[stride*(2*n+0)];
		D[n] = line[stride*(2*n+1)];
	}
#endif

	/* inverse convolution */

	line[stride*0] = C[0] + ( ( -D[0] + 1 ) >> 1 );

#	define c(m) ( (m) & (size_t)1<<(sizeof(size_t)*CHAR_BIT-1) ? C[-(m)] : \
		( (m) > (size/2-1) ? C[((size/2-1)-(m)+(size/2))] : \
		C[(m)] ) )
#	define d(m) ( (m) & (size_t)1<<(sizeof(size_t)*CHAR_BIT-1) ? D[-(m)-1] : \
		( (m) > (size/2-1) ? D[(2*(size/2-1)-(m))] : \
		D[(m)] ) )

	for (n = 0; n < size/2; ++n) {
		line[stride*(2*n)] = (int) round_ (
			-0.040689417609 * c(n-1)
			+0.788485616406 * c(n+0)
			-0.040689417609 * c(n+1)
			-0.023849465020 * d(n-2)
			+0.377402855613 * d(n-1)
			+0.377402855613 * d(n+0)
			-0.023849465020 * d(n+1)
		);

		line[stride*(2*n+1)] = (int) round_ (
			-0.064538882629 * c(n-1)
			+0.418092273222 * c(n+0)
			+0.418092273222 * c(n+1)
			-0.064538882629 * c(n+2)
			-0.037828455507 * d(n-2)
			+0.110624404418 * d(n-1)
			-0.852698679009 * d(n+0)
			+0.110624404418 * d(n+1)
			-0.037828455507 * d(n+2)
		);
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

int dwtint_encode(struct frame_t *frame)
{
	int j;
	size_t width, height;
	int *data;

	assert( frame );

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	/* size_t is unsigned integer type */
	assert( 0 == (width & 7) && 0 == (height & 7) );

	data = frame->data;

	assert( data );

	/* (2.2) forward two-dimensional transform */

	/* for each level */
	for (j = 0; j < 3; ++j) {
		/* number of elements for input */
		size_t height_j = height>>j, width_j = width>>j;

		/* stride of input data (for level j) */
#ifndef DWT_LAYOUT_INTERLEAVED
		size_t stride_y = width, stride_x = 1;
#else
		size_t stride_y = (1U << j) * width, stride_x = (1U << j) * 1;
#endif

		dwtint_encode_band(data, stride_y, stride_x, height_j, width_j);
	}

	/* (2.3) apply Subband Weights */

	for (j = 1; j < 4; ++j) {
		size_t height_j = height>>j, width_j = width>>j;

#ifndef DWT_LAYOUT_INTERLEAVED
		size_t stride_y = width, stride_x = 1;

		int *band_ll = data +        0*stride_y +       0*stride_x; /* LL (0,0) */
		int *band_hl = data +        0*stride_y + width_j*stride_x; /* HL (width_j, 0) */
		int *band_lh = data + height_j*stride_y +       0*stride_x; /* LH (0, height_j) */
		int *band_hh = data + height_j*stride_y + width_j*stride_x; /* HH (width_j, height_j) */
#else
		size_t stride_y = (1U << j) * width, stride_x = (1U << j) * 1;

		int *band_ll = data +          0 +          0;
		int *band_hl = data +          0 + stride_x/2;
		int *band_lh = data + stride_y/2 +          0;
		int *band_hh = data + stride_y/2 + stride_x/2;
#endif

		dwtint_weight_band(band_hl, stride_y, stride_x, height_j, width_j, j); /* HL */
		dwtint_weight_band(band_lh, stride_y, stride_x, height_j, width_j, j); /* LH */
		dwtint_weight_band(band_hh, stride_y, stride_x, height_j, width_j, j-1); /* HH */

		if (j < 3)
			continue;

		dwtint_weight_band(band_ll, stride_y, stride_x, height_j, width_j, j); /* LL */
	}

	return RET_SUCCESS;
}

int dwtfloat_encode(struct frame_t *frame)
{
	int j;
	size_t width, height;
	int *data;

	assert( frame );

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	/* size_t is unsigned integer type */
	assert( 0 == (width & 7) && 0 == (height & 7) );

	data = frame->data;

	assert( data );

	/* (2.2) forward two-dimensional transform */

	/* for each level */
	for (j = 0; j < 3; ++j) {
		/* number of elements for input */
		size_t height_j = height>>j, width_j = width>>j;

		/* stride of input data (for level j) */
#ifndef DWT_LAYOUT_INTERLEAVED
		size_t stride_y = width, stride_x = 1;
#else
		size_t stride_y = (1U << j) * width, stride_x = (1U << j) * 1;
#endif

		dwtfloat_encode_band(data, stride_y, stride_x, height_j, width_j);
	}

	return RET_SUCCESS;
}

int dwtint_decode(struct frame_t *frame)
{
	int j;
	size_t width, height;
	int *data;

	assert( frame );

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	/* size_t is unsigned integer type */
	assert( 0 == (width & 7) && 0 == (height & 7) );

	data = frame->data;

	assert( data );

	/* undo Subband Weights */

	for (j = 1; j < 4; ++j) {
		size_t height_j = height>>j, width_j = width>>j;

#ifndef DWT_LAYOUT_INTERLEAVED
		size_t stride_y = width, stride_x = 1;

		int *band_ll = data +        0*stride_y +       0*stride_x; /* LL (0,0) */
		int *band_hl = data +        0*stride_y + width_j*stride_x; /* HL (width_j, 0) */
		int *band_lh = data + height_j*stride_y +       0*stride_x; /* LH (0, height_j) */
		int *band_hh = data + height_j*stride_y + width_j*stride_x; /* HH (width_j, height_j) */
#else
		size_t stride_y = (1U << j) * width, stride_x = (1U << j) * 1;

		int *band_ll = data +          0 +          0;
		int *band_hl = data +          0 + stride_x/2;
		int *band_lh = data + stride_y/2 +          0;
		int *band_hh = data + stride_y/2 + stride_x/2;
#endif

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

#ifndef DWT_LAYOUT_INTERLEAVED
		size_t stride_y = width, stride_x = 1;
#else
		size_t stride_y = (1U << j) * width, stride_x = (1U << j) * 1;
#endif

		dwtint_decode_band(data, stride_y, stride_x, height_j, width_j);
	}

	return RET_SUCCESS;
}

int dwtfloat_decode(struct frame_t *frame)
{
	int j;
	size_t width, height;
	int *data;

	assert( frame );

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	/* size_t is unsigned integer type */
	assert( 0 == (width & 7) && 0 == (height & 7) );

	data = frame->data;

	assert( data );

	/* inverse two-dimensional transform */

	for (j = 2; j >= 0; --j) {
		size_t height_j = height>>j, width_j = width>>j;

#ifndef DWT_LAYOUT_INTERLEAVED
		size_t stride_y = width, stride_x = 1;
#else
		size_t stride_y = (1U << j) * width, stride_x = (1U << j) * 1;
#endif

		dwtfloat_decode_band(data, stride_y, stride_x, height_j, width_j);
	}

	return RET_SUCCESS;
}

int dwt_encode(struct frame_t *frame, const struct parameters_t *parameters)
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

int dwt_decode(struct frame_t *frame, const struct parameters_t *parameters)
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
