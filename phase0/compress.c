/**
 * C89 implementation of CCSDS 122.0-B-2 compressor
 *
 * supported features:
 * - lossless compression (Integer DWT)
 * - frame-based input
 * - Pixel Type: Unsigned Integer, 8 bpp
 */

#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

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

	assert( path );

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

	if (fscanf(stream, "%c%c", magic, magic+1) != 2) {
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
		fgets(com, 4096, stream);
	} else {
		ungetc(c, stream);
	}

	/* NOTE: C89 does not support 'z' length modifier */
	if (fscanf(stream, " %lu", &width) != 1) {
		fprintf(stderr, "[ERROR] cannot read a width\n");
		return RET_FAILURE_FILE_IO;
	}

	/* look ahead for a comment, ungetc */
	if ( (c = getc(stream)) == '#' ) {
		char com[4096];
		fgets(com, 4096, stream);
	} else {
		ungetc(c, stream);
	}

	if (fscanf(stream, " %lu", &height) != 1) {
		fprintf(stderr, "[ERROR] cannot read a height\n");
		return RET_FAILURE_FILE_IO;
	}

	/* look ahead for a comment, ungetc */
	if ( (c = getc(stream)) == '#' ) {
		char com[4096];
		fgets(com, 4096, stream);
	} else {
		ungetc(c, stream);
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
		fgets(com, 4096, stream);
	} else {
		ungetc(c, stream);
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
		if( 1 != fread((char *)data + y*width, width, 1, stream) ) {
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
		fclose(stream);
	}

	return RET_SUCCESS;
}

struct transform_t {
	size_t width;
	size_t height;
	int *data;
};

int dwt(const struct frame_t *frame, struct transform_t *transform)
{
	size_t width, height;
	size_t width_, height_;
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

	/* (2.1) copy the input raster into an array of 32-bit DWT coefficients, incl. padding */
	for (y = 0; y < height_; ++y) {
		/* input data */
		for (x = 0; x < width_; ++x) {
			*(data + y*width + x) = *( (char *)data_ + y*width_ + x );
		}
		/* padding */
		for (; x < width; ++x) {
			*(data + y*width + x) = *( (char *)data_ + y*width_ + width_-1 );
		}
	}
	/* padding */
	for (; y < height; ++y) {
		/* copy (y-1)-th row to y-th one */
		memcpy(data + y*width, data + (y-1)*width, width * sizeof *data);
	}

	/* (2.2) forward two-dimensional transform */

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
	dwt(&frame, &transform);

	/** (3) BPE */

	/** (1) free image */
	frame_free(&frame);

	return EXIT_SUCCESS;
}
