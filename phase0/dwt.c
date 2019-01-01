#include "dwt.h"
#include "utils.h"

#include <stddef.h>
#include <assert.h>
#include <stdlib.h>

int dwtint_encode_line(int *line, size_t size, size_t stride)
{
	int *line_;
	int *D, *C;
	size_t n;

	assert( (size&1) == 0 );

	line_ = malloc( size * sizeof(int) );

	if (NULL == line_) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	D = line_ + size/2;
	C = line_;

	/* lifting */

	/* subbands: D (H) at odd indices, C (L) at even indices */

	assert( line );

	D[0] = line[stride*1] - round_div_pow2(9*(line[stride*0] + line[stride*2]) - 1*(line[stride*2] + line[stride*4]), 4);

	for (n = 1; n <= size/2-3; ++n) {
		D[n] = line[stride*(2*n+1)] - round_div_pow2(9*(line[stride*(2*n)] + line[stride*(2*n+2)]) - 1*(line[stride*(2*n-2)] + line[stride*(2*n+4)]), 4);
	}

	D[size/2-2] = line[stride*(size-3)] - round_div_pow2(9*(line[stride*(size-4)] + line[stride*(size-2)]) - 1*(line[stride*(size-6)] + line[stride*(size-2)]), 4);

	D[size/2-1] = line[stride*(size-1)] - round_div_pow2(9*line[stride*(size-2)] - 1*line[stride*(size-4)], 3);

	C[0] = line[stride*0] - round_div_pow2(-D[0], 1);

	for (n = 1; n <= size/2-1; ++n) {
		C[n] = line[stride*(2*n)] - round_div_pow2(-(D[n-1]+D[n]), 2);
	}

	/* unpack */
	for (n = 0; n < size; ++n) {
		line[stride*n] = line_[n];
	}

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

	D = line_ + size/2;
	C = line_;

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

	/* unpack */
	for (n = 0; n < size; ++n) {
		line[stride*n] = line_[n];
	}

	free(line_);

	return RET_SUCCESS;
}

int dwtint_decode_line(int *line, size_t size, size_t stride)
{
	int *line_;
	int *D, *C;
	size_t n;

	assert( (size&1) == 0 );

	line_ = malloc( size * sizeof(int) );

	if (NULL == line_) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	D = line_ + size/2;
	C = line_;

	assert( line );

	/* pack */
	for (n = 0; n < size; ++n) {
		line_[n] = line[stride*n];
	}

	/* inverse lifting */

	line[stride*0] = C[0] + round_div_pow2(-D[0], 1);

	for (n = 1; n <= size/2-1; ++n) {
		line[stride*(2*n)] = C[n] + round_div_pow2(-(D[n-1]+D[n]), 2);
	}

	line[stride*1] = D[0] + round_div_pow2(9*(line[stride*0] + line[stride*2]) - 1*(line[stride*2] + line[stride*4]), 4);

	for (n = 1; n <= size/2-3; ++n) {
		line[stride*(2*n+1)] = D[n] + round_div_pow2(9*(line[stride*(2*n)] + line[stride*(2*n+2)]) - 1*(line[stride*(2*n-2)] + line[stride*(2*n+4)]), 4);
	}

	line[stride*(size-3)] = D[size/2-2] + round_div_pow2(9*(line[stride*(size-4)] + line[stride*(size-2)]) - 1*(line[stride*(size-6)] + line[stride*(size-2)]), 4);

	line[stride*(size-1)] = D[size/2-1] + round_div_pow2(9*line[stride*(size-2)] - 1*line[stride*(size-4)], 3);

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

	D = line_ + size/2;
	C = line_;

	assert( line );

	/* pack */
	for (n = 0; n < size; ++n) {
		line_[n] = line[stride*n];
	}

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

int dwtint_weight_line(int *line, size_t size, size_t stride, int weight)
{
	size_t n;

	assert( line );

	for (n = 0; n < size; ++n) {
		line[stride*n] <<= weight;
	}

	return RET_SUCCESS;
}

/**
 * inverse function to dwt_weight_line
 */
int dwtint_unweight_line(int *line, size_t size, size_t stride, int weight)
{
	size_t n;

	assert( line );

	for (n = 0; n < size; ++n) {
		line[stride*n] >>= weight;
	}

	return RET_SUCCESS;
}

int dwtint_encode(struct frame_t *frame)
{
	int j;
	size_t width, height;
	size_t width_s, height_s;
	size_t y, x;
	int *data;

	assert( frame );

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	width_s = width >> 3;
	height_s = height >> 3;

	/* size_t is unsigned integer type */
	assert( 0 == (width & 7) && 0 == (height & 7) );

	data = frame->data;

	assert( data );

	/* (2.2) forward two-dimensional transform */

	/* for each level */
	for (j = 0; j < 3; ++j) {
		size_t width_j = width>>j, height_j = height>>j;

		/* for each row */
		for (y = 0; y < height_j; ++y) {
			/* invoke one-dimensional transform */
			dwtint_encode_line(data + y*width, width_j, 1);
		}
		/* for each column */
		for (x = 0; x < width_j; ++x) {
			/* invoke one-dimensional transform */
			dwtint_encode_line(data + x, height_j, width);
		}
	}

	/* (2.3) apply Subband Weights */

	for (j = 1; j < 4; ++j) {
		size_t width_j = width>>j, height_j = height>>j;
		/* HL (width_j, 0), LH (0, height_j) */
		for (y = 0; y < height_j; ++y) {
			dwtint_weight_line(data + (0+y)*width + width_j, width_j, 1, j); /* HL */
			dwtint_weight_line(data + (height_j+y)*width + 0, width_j, 1, j); /* LH */
		}
		/* HH (width_j, height_j) */
		for (y = 0; y < height_j; ++y) {
			dwtint_weight_line(data + (height_j+y)*width + width_j, width_j, 1, j-1);
		}
	}

	/* LL (0,0) */
	for (y = 0; y < height_s; ++y) {
		dwtint_weight_line(data + (0+y)*width + 0, width_s, 1, 3);
	}

	return RET_SUCCESS;
}

/*
 * NOTE
 * When applying
 * the Float DWT, it would not be necessary for coefficients in these subbands to be rounded to
 * integer values, and so presumably the binary word size is irrelevant for these subbands.
 */
int dwtfloat_encode(struct frame_t *frame)
{
	int j;
	size_t width, height;
	size_t y, x;
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
		size_t width_j = width>>j, height_j = height>>j;

		/* for each row */
		for (y = 0; y < height_j; ++y) {
			/* invoke one-dimensional transform */
			dwtfloat_encode_line(data + y*width, width_j, 1);
		}
		/* for each column */
		for (x = 0; x < width_j; ++x) {
			/* invoke one-dimensional transform */
			dwtfloat_encode_line(data + x, height_j, width);
		}
	}

	return RET_SUCCESS;
}

int dwtint_decode(struct frame_t *frame)
{
	int j;
	size_t width, height;
	size_t width_s, height_s;
	size_t y, x;
	int *data;

	assert( frame );

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	width_s = width >> 3;
	height_s = height >> 3;

	/* size_t is unsigned integer type */
	assert( 0 == (width & 7) && 0 == (height & 7) );

	data = frame->data;

	assert( data );

	/* undo Subband Weights */

	for (j = 1; j < 4; ++j) {
		size_t width_j = width>>j, height_j = height>>j;
		/* HL (width_j, 0), LH (0, height_j) */
		for (y = 0; y < height_j; ++y) {
			dwtint_unweight_line(data + (0+y)*width + width_j, width_j, 1, j); /* HL */
			dwtint_unweight_line(data + (height_j+y)*width + 0, width_j, 1, j); /* LH */
		}
		/* HH (width_j, height_j) */
		for (y = 0; y < height_j; ++y) {
			dwtint_unweight_line(data + (height_j+y)*width + width_j, width_j, 1, j-1);
		}
	}

	/* LL (0,0) */
	for (y = 0; y < height_s; ++y) {
		dwtint_unweight_line(data + (0+y)*width + 0, width_s, 1, 3);
	}

	/* inverse two-dimensional transform */

	for (j = 2; j >= 0; --j) {
		size_t width_j = width>>j, height_j = height>>j;

		/* for each column */
		for (x = 0; x < width_j; ++x) {
			/* invoke one-dimensional transform */
			dwtint_decode_line(data + x, height_j, width);
		}
		/* for each row */
		for (y = 0; y < height_j; ++y) {
			/* invoke one-dimensional transform */
			dwtint_decode_line(data + y*width, width_j, 1);
		}
	}

	return RET_SUCCESS;
}

int dwtfloat_decode(struct frame_t *frame)
{
	int j;
	size_t width, height;
	size_t y, x;
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
		size_t width_j = width>>j, height_j = height>>j;

		/* for each column */
		for (x = 0; x < width_j; ++x) {
			/* invoke one-dimensional transform */
			dwtfloat_decode_line(data + x, height_j, width);
		}
		/* for each row */
		for (y = 0; y < height_j; ++y) {
			/* invoke one-dimensional transform */
			dwtfloat_decode_line(data + y*width, width_j, 1);
		}
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