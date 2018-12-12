/**
 * C89 implementation of CCSDS 122.0-B-2 compressor
 *
 * supported features:
 * - lossless compression (Integer DWT)
 * - frame-based input
 * - Pixel Type: Unsigned Integer, 8 bpp
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

int frame_free(struct frame_t *frame)
{
	assert( frame );

	free(frame->data);

	frame->data = NULL;

	return 0;
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

	switch (maxval) {
		case 255:
			bpp = 8;
			break;
		default:
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

	/* allocate a raster */
	data = malloc(width * height);

	if (NULL == data) {
		fprintf(stderr, "[ERROR] cannot allocate a memory\n");
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	/* (1.3) load data */

	for (y = 0; y < height; ++y) {
		/* load a single row */
		if( 1 != fread((unsigned char *)data + y*width, width, 1, stream) ) {
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

/**
 * wavelet coefficients
 */
struct transform_t {
	size_t width;
	size_t height;
	int *data;
};

int dwt_create(const struct frame_t *frame, struct transform_t *transform)
{
	size_t width_, height_;
	size_t width, height;
	void *data_;
	int *data;
	size_t y, x;

	assert( frame );

	width_ = frame->width;
	height_ = frame->height;
	data_ = frame->data;

	assert( data_ );

	/* the image dimensions be integer multiples of eight */
	width = (frame->width + 7) / 8 * 8;
	height = (frame->height + 7) / 8 * 8;

	data = malloc( width * height * sizeof *data );

	if (NULL == data) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	/* (2.1) copy the input raster into an array of 32-bit DWT coefficients, incl. padding */
	for (y = 0; y < height_; ++y) {
		/* input data */
		for (x = 0; x < width_; ++x) {
			*(data + y*width + x) = *( (unsigned char *)data_ + y*width_ + x );
		}
		/* padding */
		for (; x < width; ++x) {
			*(data + y*width + x) = *( (unsigned char *)data_ + y*width_ + width_-1 );
		}
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

int dwt_dump(struct transform_t *transform, const char *path, int factor)
{
	FILE *stream;
	size_t width, height;
	size_t y, x;
	int *data;

	stream = fopen(path, "w");

	if (NULL == stream) {
		return RET_FAILURE_FILE_OPEN;
	}

	assert( transform );

	width = transform->width;
	height = transform->height;

	if( fprintf(stream, "P5\n%lu %lu\n%lu\n", width, height, 255UL) < 0 ) {
		return RET_FAILURE_FILE_IO;
	}

	data = transform->data;

	assert( data );

	for(y = 0; y < height; ++y) {
		for(x = 0; x < width; ++x) {
			int rawval = *(data + y*width + x);
			int magnitude = abs(rawval);
			unsigned char c;

			if( magnitude < 0 )
				magnitude = INT_MAX;

			magnitude /= factor;

			assert( magnitude >= 0 && magnitude < 256 );

			c = (unsigned char)magnitude;

			if( 1 != fwrite(&c, 1, 1, stream) ) {
				return RET_FAILURE_FILE_IO;
			}
		}
	}

	if (EOF == fclose(stream))
		return RET_FAILURE_FILE_IO;

	return RET_SUCCESS;
}

int dwt_transform_line(int *line, size_t size, size_t stride)
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

	/* coefficients: D = H = odd indices, C = L = even indices */

	assert( line );

	/* FIXME: per C89 standard, the right shift of negative signed type is implementation-defined */

	D[0] = line[stride*1] - ( ( 9*(line[stride*0] + line[stride*2]) - 1*(line[stride*2] + line[stride*4]) + 8 ) >> 4 );

	for (n = 1; n <= size/2-3; ++n) {
		D[n] = line[stride*(2*n+1)] - ( ( 9*(line[stride*(2*n)] + line[stride*(2*n+2)]) - 1*(line[stride*(2*n-2)] + line[stride*(2*n+4)]) + 8 ) >> 4 );
	}

	D[size/2-2] = line[stride*(size-3)] - ( ( 9*(line[stride*(size-4)] + line[stride*(size-2)]) -1*(line[stride*(size-6)] + line[stride*(size-2)]) + 8 ) >> 4 );

	D[size/2-1] = line[stride*(size-1)] - ( ( 9*line[stride*(size-2)] -1*line[stride*(size-4)] + 4 ) >> 3 );

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

int dwt_weight_line(int *line, size_t size, size_t stride, int weight)
{
	size_t n;

	assert( line );

	for (n = 0; n < size; ++n) {
		line[stride*n] <<= weight;
	}

	return RET_SUCCESS;
}

int dwt_transform(struct transform_t *transform)
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
			dwt_transform_line(data + y*width, width_j, 1);
		}
		/* for each column */
		for (x = 0; x < width_j; ++x) {
			/* invoke one-dimensional transform */
			dwt_transform_line(data + x, height_j, width);
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
	for (y = 0; y < height>>3; ++y) {
		dwt_weight_line(data + (0+y)*width + 0, width>>3, 1, 3);
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

int main(int argc, char *argv[])
{
	struct frame_t frame;
	struct transform_t transform;

	if (argc < 2) {
		fprintf(stderr, "[ERROR] argument expected\n");
		return EXIT_FAILURE;
	}

	/** (1) load input image */
	if ( frame_load_pgm(&frame, argv[1]) ) {
		fprintf(stderr, "[ERROR] unable to load an input raster\n");
		return EXIT_FAILURE;
	}

	fprintf(stdout, "[DEBUG] frame %lu %lu %lu\n", frame.width, frame.height, frame.bpp);

	if ( frame.width > (1<<20) || frame.width < 17 ) {
		fprintf(stderr, "[ERROR] unsupported image width\n");
		return EXIT_FAILURE;
	}

	if ( frame.height < 17 ) {
		fprintf(stderr, "[ERROR] unsupported image height\n");
		return EXIT_FAILURE;
	}

	/** (2) DWT */

	if (dwt_create(&frame, &transform)) {
		fprintf(stderr, "[ERROR] unable to initialize a transform struct\n");
		return EXIT_FAILURE;
	}

	dwt_dump(&transform, "input.pgm", 1);

	dwt_transform(&transform);

	dwt_dump(&transform, "dwt3.pgm", 8);

	/** (3) BPE */

	/** (2) release resources */
	dwt_destroy(&transform);

	/** (1) free image */
	frame_free(&frame);

	return EXIT_SUCCESS;
}
