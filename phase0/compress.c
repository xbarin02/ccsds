/**
 * C89 implementation of CCSDS 122.0-B-2 compressor
 *
 * supported features:
 * - lossy (Float DWT) and lossless compression (Integer DWT)
 * - frame-based input
 * - Pixel Type: Unsigned Integer
 *
 * @author David Barina <ibarina@fit.vutbr.cz>
 */

#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>

#include "utils.h"
#include "frame.h"

/**
 * \brief Compression parameters
 */
struct parameters_t {
	/**
	 * Specifies DWT type:
	 * 0: Float DWT
	 * 1: Integer DWT
	 */
	int DWTtype;

	 /**
	  * segment size in blocks
	  * A segment is defined as a group of S consecutive blocks.
	  * \f$ 16 \le S \le 2^20 \f$
	  */
	unsigned S;
};

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

	/* NOTE per C89 standard, the right shift of negative signed type is implementation-defined */

	D[0] = line[stride*1] - ( ( 9*(line[stride*0] + line[stride*2]) - 1*(line[stride*2] + line[stride*4]) + 8 ) >> 4 );

	for (n = 1; n <= size/2-3; ++n) {
		D[n] = line[stride*(2*n+1)] - ( ( 9*(line[stride*(2*n)] + line[stride*(2*n+2)]) - 1*(line[stride*(2*n-2)] + line[stride*(2*n+4)]) + 8 ) >> 4 );
	}

	D[size/2-2] = line[stride*(size-3)] - ( ( 9*(line[stride*(size-4)] + line[stride*(size-2)]) - 1*(line[stride*(size-6)] + line[stride*(size-2)]) + 8 ) >> 4 );

	D[size/2-1] = line[stride*(size-1)] - ( ( 9*line[stride*(size-2)] - 1*line[stride*(size-4)] + 4 ) >> 3 );

	C[0] = line[stride*0] - ( ( -D[0] + 1 ) >> 1 );

	for (n = 1; n <= size/2-1; ++n) {
		C[n] = line[stride*(2*n)] - ( ( -(D[n-1]+D[n]) + 2 ) >> 2 );
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

	line[stride*0] = C[0] + ( ( -D[0] + 1 ) >> 1 );

	for (n = 1; n <= size/2-1; ++n) {
		line[stride*(2*n)] = C[n] + ( ( -(D[n-1]+D[n]) + 2 ) >> 2 );
	}

	line[stride*1] = D[0] + ( ( 9*(line[stride*0] + line[stride*2]) - 1*(line[stride*2] + line[stride*4]) + 8 ) >> 4 );

	for (n = 1; n <= size/2-3; ++n) {
		line[stride*(2*n+1)] = D[n] + ( ( 9*(line[stride*(2*n)] + line[stride*(2*n+2)]) - 1*(line[stride*(2*n-2)] + line[stride*(2*n+4)]) + 8 ) >> 4 );
	}

	line[stride*(size-3)] = D[size/2-2] + ( ( 9*(line[stride*(size-4)] + line[stride*(size-2)]) - 1*(line[stride*(size-6)] + line[stride*(size-2)]) + 8 ) >> 4 );

	line[stride*(size-1)] = D[size/2-1] + ( ( 9*line[stride*(size-2)] - 1*line[stride*(size-4)] + 4 ) >> 3 );

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

#if 0
int bpe_encode(const struct frame_t *frame, const struct parameters_t *parameters)
{
	/* LL band size */
	size_t width_s = frame->width >> 3;
	size_t height_s = frame->height >> 3;

	/* start of the current segment (in LL band) */
	size_t segment_y = 0, segment_x = 0;

	/* for each segment */
	{
		/* encode segment header, BLUE BOOK section 4.2 */

		/* quantize DC coefficients, BLUE BOOK section 4.3 */

		/* encode quantized DC coefficients, BLUE BOOK section 4.3 */

		/* output additional DC bit planes, BLUE BOOK section 4.3 */

		/* encode AC bit depths, BLUE BOOK section 4.4 */

		/* for each bit plane, , BLUE BOOK section 4.5 */
		{
			/* stage 0: DC refinement bits for all blocks */
			/* stage 1: code parents update for all blocks */
			/* stage 2: code children updates for all blocks */
			/* stage 3: code grandchildren updates for all blocks */
			/* stage 4: produce refinement bits for all blocks */
		}
	}

	return RET_SUCCESS;
}
#endif

int main(int argc, char *argv[])
{
	struct frame_t frame;
	struct parameters_t parameters;

	/* NOTE Since we implement the floor function for negative numbers using
	 * an arithmetic right shift, we must check whether the underlying
	 * signed integer representation is two's complement. */
	assert( ~-1 == 0 );

	/*
	 * NOTE The C standard states that the result of the >> operator is
	 * implementation-defined if the left operand has a signed type and
	 * a negative value. I have never seen the compiler that would
	 * implement this differently than using an arithmetic right shift.
	 * However, the following assert checks the sanity of this assumption.
	 */
	assert( -1 >> 1 == -1 );

	if (argc < 2) {
		fprintf(stderr, "[ERROR] argument expected\n");
		return EXIT_FAILURE;
	}

	/** (1) load input image */

	if ( frame_load_pgm(&frame, argv[1]) ) {
		fprintf(stderr, "[ERROR] unable to load image\n");
		return EXIT_FAILURE;
	}

	fprintf(stderr, "[DEBUG] frame %lu %lu %lu\n", (unsigned long) frame.width, (unsigned long) frame.height, (unsigned long) frame.bpp);

	if ( frame.width > (1<<20) || frame.width < 17 ) {
		fprintf(stderr, "[ERROR] unsupported image width\n");
		return EXIT_FAILURE;
	}

	if ( frame.height < 17 ) {
		fprintf(stderr, "[ERROR] unsupported image height\n");
		return EXIT_FAILURE;
	}

	frame_dump(&frame, "input.pgm", 1);

	parameters.DWTtype = 0;
	parameters.S = 16;

	fprintf(stderr, "[DEBUG] transform...\n");

	/** (2) forward DWT */

	if (parameters.DWTtype == 1)
		dwtint_encode(&frame);
	else
		dwtfloat_encode(&frame);

	fprintf(stderr, "[DEBUG] transform done\n");

	frame_dump(&frame, "dwt3.pgm", 8);

	/** (3) BPE */
#if 0
	bpe_encode(&frame, &parameters);
#endif
	/** (2) inverse DWT */

	if (parameters.DWTtype == 1)
		dwtint_decode(&frame);
	else
		dwtfloat_decode(&frame);

	frame_dump(&frame, "decoded.pgm", 1);

	/** (1) save output image */

	if ( frame_save_pgm(&frame, "output.pgm") ) {
		fprintf(stderr, "[ERROR] unable to save an output raster\n");
		return EXIT_FAILURE;
	}

	/** (1) release resources */

	frame_destroy(&frame);

	return EXIT_SUCCESS;
}
