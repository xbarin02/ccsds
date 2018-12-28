/**
 * C89 implementation of CCSDS 122.0-B-2 compressor
 *
 * supported features:
 * - lossy (Float DWT) and lossless compression (Integer DWT)
 * - frame-based input
 * - Pixel Type: Unsigned Integer, 8 bpp and 16 bpp
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

/**
 * error codes
 */
enum return_t {
	/* 0x0000 successful completion */
	RET_SUCCESS                   = 0x0000, /**< success */
	/* 0x1xxx input/output errors */
	RET_FAILURE_FILE_IO           = 0x1000, /**< I/O error */
	RET_FAILURE_FILE_UNSUPPORTED  = 0x1001, /**< unsupported feature or file type */
	RET_FAILURE_FILE_OPEN         = 0x1002, /**< file open failure */
	/* 0x2xxx memory errors */
	RET_FAILURE_MEMORY_ALLOCATION = 0x2000, /**< unable to allocate dynamic memory */
	/* 0x3xxx general exceptions */
	RET_FAILURE_LOGIC_ERROR       = 0x3000, /**< faulty logic within the program */
	RET_LAST
};

/**
 * image parameters and pointer to image data
 */
struct frame_t {
	size_t width;  /**< number of columns, range [17; 1<<20] */
	size_t height; /**< number of rows, range [17; infty) */
	size_t bpp;    /**< pixel bit depth */

	void *data;
};

/**
 * wavelet coefficients
 */
struct transform_t {
	size_t width;
	size_t height;
	int *data;
};

/**
 * compression parameters
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
	  * @f$ 16 \le S \le 2^20 @f$
	  */
	unsigned S;
};

/**
 * largest integral value not greater than argument
 */
static int floor_(double x)
{
	/*
	 * per C89 standard, 6.2.1.3 Floating and integral:
	 *
	 * When a value of floating type is convened to integral type,
	 * the fractional part is discarded. If the value of the integral part
	 * cannot be represented by the integral type, the behavior is
	 * undetined.
	 *
	 * "... discarded", i.e., the value is truncated toward zero
	 */

	/* truncate */
	int i = (int) x;

	/* convert trunc to floor */
	return i - (int) ( (double) i > x );
}

/**
 * round to nearest integer
 */
#define round_(x) floor_( (x) + 0.5 )

int frame_free(struct frame_t *frame)
{
	assert( frame );

	free(frame->data);

	frame->data = NULL;

	return 0;
}

/**
 * this should be enabled on little endian architectures
 */
#define SWAP_BYTE_ORDER

/**
 * convert big endian to native byte order
 */
static unsigned short be_to_native_s(unsigned short a)
{
#ifdef SWAP_BYTE_ORDER
	return (unsigned short) (
		((a & 0xff00U) >> 8U) |
		((a & 0x00ffU) << 8U)
	);
#else
	return a;
#endif
}

/**
 * convert native byte order to big endian
 */
static unsigned short native_to_be_s(unsigned short a)
{
#ifdef SWAP_BYTE_ORDER
	return (unsigned short) (
		((a & 0xff00U) >> 8U) |
		((a & 0x00ffU) << 8U)
	);
#else
	return a;
#endif
}

/**
 * the result is undefined for n == 0
 */
static unsigned long floor_log2_l(unsigned long n)
{
	unsigned long r = 0;

	while (n >>= 1) {
		r++;
	}

	return r;
}

static size_t convert_maxval_to_bpp(unsigned long maxval)
{
	if (maxval) {
		return floor_log2_l(maxval) + 1;
	}

	return 0;
}

static unsigned long convert_bpp_to_maxval(size_t bpp)
{
	if (bpp) {
		return (1UL << bpp) - 1;
	}

	return 0;
}

int frame_save_pgm(const struct frame_t *frame, const char *path)
{
	FILE *stream;
	size_t y;
	size_t width;
	size_t height;
	size_t bpp;
	size_t stride;
	const void *data;

	if (0 == strcmp(path, "-"))
		stream = stdout;
	else
		stream = fopen(path, "w");

	if (NULL == stream) {
		fprintf(stderr, "[ERROR] fopen fails\n");
		return RET_FAILURE_FILE_OPEN;
	}

	/* write header */

	assert( frame );

	height = frame->height;
	width = frame->width;
	bpp = frame->bpp;

	if (fprintf(stream, "P5\n%lu %lu\n%lu\n", width, height, convert_bpp_to_maxval(bpp)) < 0) {
		return RET_FAILURE_FILE_IO;
	}

	data = frame->data;

	assert( data );

	/* save data */

	stride = width * ( bpp <= CHAR_BIT ? 1 : sizeof(unsigned short) );

	for (y = 0; y < height; ++y) {
		if (fwrite((const unsigned char *)data + y*stride, stride, 1, stream) < 1) {
			return RET_FAILURE_FILE_IO;
		}
	}

	if (stream != stdout) {
		if (EOF == fclose(stream))
			return RET_FAILURE_FILE_IO;
	}

	return RET_SUCCESS;
}

int frame_load_pgm(struct frame_t *frame, const char *path)
{
	FILE *stream;
	char magic[2];
	unsigned long maxval;
	size_t y;
	size_t width;
	size_t height;
	size_t bpp;
	size_t stride;
	void *data;
	int c;

	/* (1.1) open file */

	if (0 == strcmp(path, "-"))
		stream = stdin;
	else
		stream = fopen(path, "r");

	if (NULL == stream) {
		fprintf(stderr, "[ERROR] fopen fails\n");
		return RET_FAILURE_FILE_OPEN;
	}

	/* (1.2) read header */

	if (fscanf(stream, "%c%c ", magic, magic+1) != 2) {
		fprintf(stderr, "[ERROR] cannot read a magic number\n");
		return RET_FAILURE_FILE_IO;
	}

	if (magic[0] != 'P') {
		fprintf(stderr, "[ERROR] invalid magic number\n");
		return RET_FAILURE_FILE_UNSUPPORTED;
	}

	switch (magic[1]) {
		case '5':
			/* P5 is supported */
			break;
		default:
			fprintf(stderr, "[ERROR] invalid magic number\n");
			return RET_FAILURE_FILE_UNSUPPORTED;
	}

	/* look ahead for a comment, ungetc */
	if ( (c = getc(stream)) == '#' ) {
		char com[4096];
		if (NULL == fgets(com, 4096, stream))
			return RET_FAILURE_FILE_IO;
	} else {
		if (EOF == ungetc(c, stream))
			return RET_FAILURE_FILE_IO;
	}

	/* NOTE: C89 does not support 'z' length modifier */
	if (fscanf(stream, " %lu ", &width) != 1) {
		fprintf(stderr, "[ERROR] cannot read a width\n");
		return RET_FAILURE_FILE_IO;
	}

	/* look ahead for a comment, ungetc */
	if ( (c = getc(stream)) == '#' ) {
		char com[4096];
		if (NULL == fgets(com, 4096, stream))
			return RET_FAILURE_FILE_IO;
	} else {
		if (EOF == ungetc(c, stream))
			return RET_FAILURE_FILE_IO;
	}

	if (fscanf(stream, " %lu ", &height) != 1) {
		fprintf(stderr, "[ERROR] cannot read a height\n");
		return RET_FAILURE_FILE_IO;
	}

	/* look ahead for a comment, ungetc */
	if ( (c = getc(stream)) == '#' ) {
		char com[4096];
		if (NULL == fgets(com, 4096, stream))
			return RET_FAILURE_FILE_IO;
	} else {
		if (EOF == ungetc(c, stream))
			return RET_FAILURE_FILE_IO;
	}

	if (fscanf(stream, " %lu", &maxval) != 1) {
		fprintf(stderr, "[ERROR] cannot read a maximum gray value\n");
		return RET_FAILURE_FILE_IO;
	}

	bpp = convert_maxval_to_bpp(maxval);

	if (bpp > 16) {
		fprintf(stderr, "[ERROR] unsupported pixel depth\n");
		return RET_FAILURE_FILE_UNSUPPORTED;
	}

	/* look ahead for a comment, ungetc */
	if ( (c = getc(stream)) == '#' ) {
		char com[4096];
		if (NULL == fgets(com, 4096, stream))
			return RET_FAILURE_FILE_IO;
	} else {
		if (EOF == ungetc(c, stream))
			return RET_FAILURE_FILE_IO;
	}

	/* consume a single whitespace character */
	if ( !isspace(fgetc(stream)) ) {
		fprintf(stderr, "[ERROR] unexpected input\n");
		return RET_FAILURE_FILE_UNSUPPORTED;
	}

	/* stride in bytes (aka chars) */
	stride = width * ( bpp <= CHAR_BIT ? 1 : sizeof(short) );

	/* allocate a raster */
	data = malloc(height * stride);

	if (NULL == data) {
		fprintf(stderr, "[ERROR] cannot allocate a memory\n");
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	/* (1.3) load data */

	for (y = 0; y < height; ++y) {
		/* load a single row */
		if ( 1 != fread((unsigned char *)data + y*stride, stride, 1, stream) ) {
			fprintf(stderr, "[ERROR] end-of-file or error while reading a row\n");
			return RET_FAILURE_FILE_IO;
		}
	}

	/* fill the frame struct */
	assert( frame );

	frame->width = width;
	frame->height = height;
	frame->bpp = bpp;
	frame->data = data;

	/* (1.4) close file */

	if (stream != stdin) {
		if (EOF == fclose(stream))
			return RET_FAILURE_FILE_IO;
	}

	return RET_SUCCESS;
}

static size_t ceil_multiple8(size_t n)
{
	return (n + 7) / 8 * 8;
}

int dwt_create(const struct frame_t *frame, struct transform_t *transform)
{
	size_t width_, height_;
	size_t width, height;
	int *data;
	size_t y, x;

	assert( frame );

	width_ = frame->width;
	height_ = frame->height;

	/* the image dimensions be integer multiples of eight */
	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	data = malloc( width * height * sizeof *data );

	if (NULL == data) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	assert( frame->data );

	/* (2.1) copy the input raster into an array of 32-bit DWT coefficients, incl. padding */
	if (frame->bpp <= CHAR_BIT) {
		for (y = 0; y < height_; ++y) {
			const unsigned char *data_ = frame->data;
			/* input data */
			for (x = 0; x < width_; ++x) {
				data [y*width + x] = data_ [y*width_ + x];
			}
			/* padding */
			for (; x < width; ++x) {
				data [y*width + x] = data_ [y*width_ + width_-1];
			}
		}
	} else if (frame->bpp <= CHAR_BIT * sizeof(unsigned short)) {
		for (y = 0; y < height_; ++y) {
			const unsigned short *data_ = frame->data;
			/* input data */
			for (x = 0; x < width_; ++x) {
				data [y*width + x] = be_to_native_s( data_ [y*width_ + x] );
			}
			/* padding */
			for (; x < width; ++x) {
				data [y*width + x] = be_to_native_s( data_ [y*width_ + width_-1] );
			}
		}
	} else {
		fprintf(stderr, "[ERROR] unsupported pixel depth\n");
		return RET_FAILURE_LOGIC_ERROR;
	}
	/* padding */
	for (; y < height; ++y) {
		/* copy (y-1)-th row to y-th one */
		memcpy(data + y*width, data + (y-1)*width, width * sizeof *data);
	}

	assert( transform );

	transform->width = width;
	transform->height = height;
	transform->data = data;

	return RET_SUCCESS;
}

#if 0
int dwt_import(const struct frame_t *frame, struct transform_t *transform)
{
}
#endif

int clamp(int v, int lo, int hi)
{
	return v < lo ? lo : ( hi < v ? hi : v );
}

/**
 * export the transform into frame
 *
 * - frame dimensions must be set
 * - frame data pointer must be allocated
 */
int dwt_export(const struct transform_t *transform, struct frame_t *frame)
{
	size_t width_, height_;
	size_t width;
	size_t bpp;
	size_t y, x;
	const int *data;
	int i_maxval;

	assert( frame );

	bpp = frame->bpp;
	width_ = frame->width;
	height_ = frame->height;

	assert( transform );

	width = transform->width;

	data = transform->data;

	assert( data );
	assert( frame->data );

	i_maxval = (int) convert_bpp_to_maxval(bpp);

	if (bpp <= CHAR_BIT) {
		unsigned char *data_ = frame->data;
		for (y = 0; y < height_; ++y) {
			for (x = 0; x < width_; ++x) {
				int sample = data [y*width + x];

				data_ [y*width_ + x] = (unsigned char) clamp(sample, 0, i_maxval);
			}
		}
	} else if (bpp <= CHAR_BIT * sizeof(unsigned short)) {
		unsigned short *data_ = frame->data;
		for (y = 0; y < height_; ++y) {
			for (x = 0; x < width_; ++x) {
				int sample = data [y*width + x];

				data_ [y*width_ + x] = native_to_be_s( (unsigned short) clamp(sample, 0, i_maxval) );
			}
		}
	} else {
		return RET_FAILURE_LOGIC_ERROR;
	}

	return RET_SUCCESS;
}

/**
 * compute the absolute value of an integer
 *
 * Unlike abs(), the absolute value of the most negative integer is defined to be INT_MAX.
 */
int abs_(int j)
{
	int r = abs(j);

	if (r < 0) {
		return INT_MAX;
	}

	return r;
}

int dwt_dump(const struct transform_t *transform, const char *path, int factor)
{
	FILE *stream;
	size_t width, height;
	size_t bpp;
	size_t y, x;
	const int *data;
	int maxval;

	stream = fopen(path, "w");

	if (NULL == stream) {
		return RET_FAILURE_FILE_OPEN;
	}

	assert( transform );

	/* FIXME this should be stored in transform_t */
	bpp = 16;

	maxval = (int) convert_bpp_to_maxval(bpp);

	width = transform->width;
	height = transform->height;

	if ( fprintf(stream, "P5\n%lu %lu\n%lu\n", width, height, (1UL<<bpp)-1UL) < 0 ) {
		return RET_FAILURE_FILE_IO;
	}

	data = transform->data;

	assert( data );
	assert( factor );

	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			int sample = data [y*width + x];
			int magnitude = abs_(sample) / factor;

			switch (bpp) {
				case CHAR_BIT: {
						unsigned char c = (unsigned char) clamp(magnitude, 0, maxval);

						if ( 1 != fwrite(&c, 1, 1, stream) ) {
							return RET_FAILURE_FILE_IO;
						}
						break;
					}
				case CHAR_BIT * sizeof(short): {
						unsigned short c = native_to_be_s( (unsigned short) clamp(magnitude, 0, maxval) );

						if ( 1 != fwrite(&c, sizeof(short), 1, stream) ) {
							return RET_FAILURE_FILE_IO;
						}
						break;
					}
				default:
					return RET_FAILURE_LOGIC_ERROR;
			}
		}
	}

	if (EOF == fclose(stream))
		return RET_FAILURE_FILE_IO;

	return RET_SUCCESS;
}

int dwt_encode_line(int *line, size_t size, size_t stride)
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

int dwt_encode_line_float(int *line, size_t size, size_t stride)
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

int dwt_decode_line(int *line, size_t size, size_t stride)
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

int dwt_decode_line_float(int *line, size_t size, size_t stride)
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

int dwt_weight_line(int *line, size_t size, size_t stride, int weight)
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
int dwt_slim_line(int *line, size_t size, size_t stride, int weight)
{
	size_t n;

	assert( line );

	for (n = 0; n < size; ++n) {
		line[stride*n] >>= weight;
	}

	return RET_SUCCESS;
}

int dwt_encode(struct transform_t *transform)
{
	int j;
	size_t width, height;
	size_t width_s, height_s;
	size_t y, x;
	int *data;

	assert( transform );

	width = transform->width;
	height = transform->height;

	width_s = width >> 3;
	height_s = height >> 3;

	/* size_t is unsigned integer type */
	assert( 0 == (width & 7) && 0 == (height & 7) );

	data = transform->data;

	assert( data );

	/* (2.2) forward two-dimensional transform */

	/* for each level */
	for (j = 0; j < 3; ++j) {
		size_t width_j = width>>j, height_j = height>>j;

		/* for each row */
		for (y = 0; y < height_j; ++y) {
			/* invoke one-dimensional transform */
			dwt_encode_line(data + y*width, width_j, 1);
		}
		/* for each column */
		for (x = 0; x < width_j; ++x) {
			/* invoke one-dimensional transform */
			dwt_encode_line(data + x, height_j, width);
		}
	}

	/* (2.3) apply Subband Weights */

	for (j = 1; j < 4; ++j) {
		size_t width_j = width>>j, height_j = height>>j;
		/* HL (width_j, 0), LH (0, height_j) */
		for (y = 0; y < height_j; ++y) {
			dwt_weight_line(data + (0+y)*width + width_j, width_j, 1, j); /* HL */
			dwt_weight_line(data + (height_j+y)*width + 0, width_j, 1, j); /* LH */
		}
		/* HH (width_j, height_j) */
		for (y = 0; y < height_j; ++y) {
			dwt_weight_line(data + (height_j+y)*width + width_j, width_j, 1, j-1);
		}
	}

	/* LL (0,0) */
	for (y = 0; y < height_s; ++y) {
		dwt_weight_line(data + (0+y)*width + 0, width_s, 1, 3);
	}

	return RET_SUCCESS;
}

/*
 * FIXME
 * When applying
 * the Float DWT, it would not be necessary for coefficients in these subbands to be rounded to
 * integer values, and so presumably the binary word size is irrelevant for these subbands.
 */
int dwt_encode_float(struct transform_t *transform)
{
	int j;
	size_t width, height;
	size_t y, x;
	int *data;

	assert( transform );

	width = transform->width;
	height = transform->height;

	/* size_t is unsigned integer type */
	assert( 0 == (width & 7) && 0 == (height & 7) );

	data = transform->data;

	assert( data );

	/* (2.2) forward two-dimensional transform */

	/* for each level */
	for (j = 0; j < 3; ++j) {
		size_t width_j = width>>j, height_j = height>>j;

		/* for each row */
		for (y = 0; y < height_j; ++y) {
			/* invoke one-dimensional transform */
			dwt_encode_line_float(data + y*width, width_j, 1);
		}
		/* for each column */
		for (x = 0; x < width_j; ++x) {
			/* invoke one-dimensional transform */
			dwt_encode_line_float(data + x, height_j, width);
		}
	}

	return RET_SUCCESS;
}

int dwt_decode(struct transform_t *transform)
{
	int j;
	size_t width, height;
	size_t width_s, height_s;
	size_t y, x;
	int *data;

	assert( transform );

	width = transform->width;
	height = transform->height;

	width_s = width >> 3;
	height_s = height >> 3;

	/* size_t is unsigned integer type */
	assert( 0 == (width & 7) && 0 == (height & 7) );

	data = transform->data;

	assert( data );

	/* undo Subband Weights */

	for (j = 1; j < 4; ++j) {
		size_t width_j = width>>j, height_j = height>>j;
		/* HL (width_j, 0), LH (0, height_j) */
		for (y = 0; y < height_j; ++y) {
			dwt_slim_line(data + (0+y)*width + width_j, width_j, 1, j); /* HL */
			dwt_slim_line(data + (height_j+y)*width + 0, width_j, 1, j); /* LH */
		}
		/* HH (width_j, height_j) */
		for (y = 0; y < height_j; ++y) {
			dwt_slim_line(data + (height_j+y)*width + width_j, width_j, 1, j-1);
		}
	}

	/* LL (0,0) */
	for (y = 0; y < height_s; ++y) {
		dwt_slim_line(data + (0+y)*width + 0, width_s, 1, 3);
	}

	/* inverse two-dimensional transform */

	for (j = 2; j >= 0; --j) {
		size_t width_j = width>>j, height_j = height>>j;

		/* for each column */
		for (x = 0; x < width_j; ++x) {
			/* invoke one-dimensional transform */
			dwt_decode_line(data + x, height_j, width);
		}
		/* for each row */
		for (y = 0; y < height_j; ++y) {
			/* invoke one-dimensional transform */
			dwt_decode_line(data + y*width, width_j, 1);
		}
	}

	return RET_SUCCESS;
}

int dwt_decode_float(struct transform_t *transform)
{
	int j;
	size_t width, height;
	size_t y, x;
	int *data;

	assert( transform );

	width = transform->width;
	height = transform->height;

	/* size_t is unsigned integer type */
	assert( 0 == (width & 7) && 0 == (height & 7) );

	data = transform->data;

	assert( data );

	/* inverse two-dimensional transform */

	for (j = 2; j >= 0; --j) {
		size_t width_j = width>>j, height_j = height>>j;

		/* for each column */
		for (x = 0; x < width_j; ++x) {
			/* invoke one-dimensional transform */
			dwt_decode_line_float(data + x, height_j, width);
		}
		/* for each row */
		for (y = 0; y < height_j; ++y) {
			/* invoke one-dimensional transform */
			dwt_decode_line_float(data + y*width, width_j, 1);
		}
	}

	return RET_SUCCESS;
}

int dwt_destroy(struct transform_t *transform)
{
	assert( transform );

	free(transform->data);

	transform->data = NULL;

	return RET_SUCCESS;
}

#if 0
int bpe_encode(const struct transform_t *transform, const struct parameters_t *parameters)
{
	/* LL band size */
	size_t width_s = transform->width >> 3;
	size_t height_s = transform->height >> 3;

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
	struct transform_t transform;
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
		fprintf(stderr, "[ERROR] unable to load an input raster\n");
		return EXIT_FAILURE;
	}

	fprintf(stderr, "[DEBUG] frame %lu %lu %lu\n", frame.width, frame.height, frame.bpp);

	if ( frame.width > (1<<20) || frame.width < 17 ) {
		fprintf(stderr, "[ERROR] unsupported image width\n");
		return EXIT_FAILURE;
	}

	if ( frame.height < 17 ) {
		fprintf(stderr, "[ERROR] unsupported image height\n");
		return EXIT_FAILURE;
	}

	/** (2) DWT */

	/* FIXME split create and import */
	if ( dwt_create(&frame, &transform) ) {
		fprintf(stderr, "[ERROR] unable to initialize a transform struct\n");
		return EXIT_FAILURE;
	}

	dwt_dump(&transform, "input.pgm", 1);

	parameters.DWTtype = 0;
	parameters.S = 16;

	/* ***** encoding ***** */

	fprintf(stderr, "[DEBUG] transform...\n");

	if (parameters.DWTtype == 1)
		dwt_encode(&transform);
	else
		dwt_encode_float(&transform);

	fprintf(stderr, "[DEBUG] transform done\n");

	dwt_dump(&transform, "dwt3.pgm", 8);

	/** (3) BPE */
#if 0
	bpe_encode(&transform, &parameters);
#endif
	/* ***** decoding ***** */

	if (parameters.DWTtype == 1)
		dwt_decode(&transform);
	else
		dwt_decode_float(&transform);

	dwt_dump(&transform, "decoded.pgm", 1);

	/* convert data from transform into frame */
	dwt_export(&transform, &frame);

	if ( frame_save_pgm(&frame, "output.pgm") ) {
		fprintf(stderr, "[ERROR] unable to save an output raster\n");
		return EXIT_FAILURE;
	}

	/** (2) release resources */
	dwt_destroy(&transform);

	/** (1) free image */
	frame_free(&frame);

	return EXIT_SUCCESS;
}
