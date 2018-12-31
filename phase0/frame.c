#include "frame.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <assert.h>

int frame_write_pgm_header(const struct frame_t *frame, FILE *stream)
{
	size_t width, height;
	size_t bpp;

	/* write header */

	assert( frame );

	height = frame->height;
	width = frame->width;
	bpp = frame->bpp;

	if ( width > ULONG_MAX || height > ULONG_MAX ) {
		return RET_FAILURE_OVERFLOW_ERROR;
	}

	if (fprintf(stream, "P5\n%lu %lu\n%lu\n", (unsigned long) width, (unsigned long) height, convert_bpp_to_maxval(bpp)) < 0) {
		return RET_FAILURE_FILE_IO;
	}

	return RET_SUCCESS;
}

int frame_save_pgm(const struct frame_t *frame, const char *path)
{
	FILE *stream;
	int err;
	size_t width_, height_, depth_;
	size_t width;
	size_t y, x;
	int maxval;
	void *line;
	const int *data;

	/* open file */
	if (0 == strcmp(path, "-"))
		stream = stdout;
	else
		stream = fopen(path, "w");

	if (NULL == stream) {
		fprintf(stderr, "[ERROR] cannot open output file\n");
		return RET_FAILURE_FILE_OPEN;
	}

	/* write header */
	err = frame_write_pgm_header(frame, stream);

	if (err) {
		return err;
	}

	assert( frame );

	width_ = frame->width;
	height_ = frame->height;
	depth_ = convert_bpp_to_depth(frame->bpp);

	maxval = (int) convert_bpp_to_maxval(frame->bpp);

	width = ceil_multiple8(frame->width);

	/* allocate a line */
	line = malloc( width_ * depth_ );

	if (NULL == line) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	data = frame->data;

	assert( data );

	/* write data */
	for (y = 0; y < height_; ++y) {
		/* copy data into line */
		switch (depth_) {
			case sizeof(char): {
				unsigned char *line_ = line;
				/* input data */
				for (x = 0; x < width_; ++x) {
					int sample = data [y*width + x];
					line_ [x] = (unsigned char) clamp(sample, 0, maxval);
				}
				break;
			}
			case sizeof(short): {
				unsigned short *line_ = line;
				/* input data */
				for (x = 0; x < width_; ++x) {
					int sample = data [y*width + x];
					line_ [x] = native_to_be_s( (unsigned short) clamp(sample, 0, maxval) );
				}
				break;
			}
			default:
				return RET_FAILURE_LOGIC_ERROR;
		}
		/* write line */
		if ( fwrite(line, depth_, width_, stream) < width_ ) {
			return RET_FAILURE_FILE_IO;
		}
	}

	free(line);

	/* close file */
	if (stream != stdout) {
		if (EOF == fclose(stream))
			return RET_FAILURE_FILE_IO;
	}

	return RET_SUCCESS;
}

/*
 * consumes multiple line comments
 */
int stream_skip_comment(FILE *stream)
{
	int c;

	/* look ahead for a comment, ungetc */
	while ( (c = getc(stream)) == '#' ) {
		char com[4096];
		if (NULL == fgets(com, 4096, stream))
			return RET_FAILURE_FILE_IO;
	}

	if (EOF == ungetc(c, stream))
		return RET_FAILURE_FILE_IO;

	return RET_SUCCESS;
}

int frame_read_pgm_header(struct frame_t *frame, FILE *stream)
{
	char magic[2];
	unsigned long maxval;
	size_t width, height;
	unsigned long width_l, height_l;
	size_t bpp;

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

	if (stream_skip_comment(stream)) {
		return RET_FAILURE_FILE_IO;
	}

	/* C89 does not support 'z' length modifier */
	if (fscanf(stream, " %lu ", &width_l) != 1) {
		fprintf(stderr, "[ERROR] cannot read a width\n");
		return RET_FAILURE_FILE_IO;
	}

	/*
	 * (size_t)-1 is well defined in C89 under section 6.2.1.2 Signed and unsigned integers
	 */
	if (width_l > (size_t)-1) {
		return RET_FAILURE_OVERFLOW_ERROR;
	}

	width = (size_t) width_l;

	if (stream_skip_comment(stream)) {
		return RET_FAILURE_FILE_IO;
	}

	/* C89 does not support 'z' length modifier */
	if (fscanf(stream, " %lu ", &height_l) != 1) {
		fprintf(stderr, "[ERROR] cannot read a height\n");
		return RET_FAILURE_FILE_IO;
	}

	if (height_l > (size_t)-1) {
		return RET_FAILURE_OVERFLOW_ERROR;
	}

	height = (size_t) height_l;

	if (stream_skip_comment(stream)) {
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

	if (stream_skip_comment(stream)) {
		return RET_FAILURE_FILE_IO;
	}

	/* consume a single whitespace character */
	if ( !isspace(fgetc(stream)) ) {
		fprintf(stderr, "[ERROR] unexpected input\n");
		return RET_FAILURE_FILE_UNSUPPORTED;
	}

	/* fill the struct */
	assert( frame );

	frame->width = width;
	frame->height = height;
	frame->bpp = bpp;

	return RET_SUCCESS;
}

int frame_load_pgm(struct frame_t *frame, const char *path)
{
	FILE *stream;
	int err;
	size_t width_, height_, depth_;
	size_t width, height;
	void *line;
	int *data;
	size_t y, x;

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
	err = frame_read_pgm_header(frame, stream);

	if (err) {
		return err;
	}

	assert( frame );

	width_ = frame->width;
	height_ = frame->height;
	depth_ = convert_bpp_to_depth(frame->bpp);

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	/* allocate a line */
	line = malloc( width_ * depth_ );

	if (NULL == line) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	/* allocate a raster */
	data = malloc( height * width * sizeof *data );

	if (NULL == data) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	/* (1.3) load data */

	/* (2.1) copy the input raster into an array of 32-bit DWT coefficients, incl. padding */
	for (y = 0; y < height_; ++y) {
		/* read line */
		if ( fread(line, depth_, width_, stream) < width_ ) {
			fprintf(stderr, "[ERROR] end-of-file or error while reading a row\n");
			return RET_FAILURE_FILE_IO;
		}
		/* copy pixels from line into framebuffer */
		switch (depth_) {
			case sizeof(char): {
				const unsigned char *line_ = line;
				/* input data */
				for (x = 0; x < width_; ++x) {
					data [y*width + x] = line_ [x];
				}
				/* padding */
				for (; x < width; ++x) {
					data [y*width + x] = line_ [width_-1];
				}
				break;
			}
			case sizeof(short): {
				const unsigned short *line_ = line;
				/* input data */
				for (x = 0; x < width_; ++x) {
					data [y*width + x] = be_to_native_s( line_ [x] );
				}
				/* padding */
				for (; x < width; ++x) {
					data [y*width + x] = be_to_native_s( line_ [width_-1] );
				}
				break;
			}
			default:
				return RET_FAILURE_LOGIC_ERROR;
		}
	}
	/* padding */
	for (; y < height; ++y) {
		/* copy (y-1)-th row to y-th one */
		memcpy(data + y*width, data + (y-1)*width, width * sizeof *data);
	}

	free(line);

	/* (1.4) close file */

	if (stream != stdin) {
		if (EOF == fclose(stream))
			return RET_FAILURE_FILE_IO;
	}

	/* fill the struct */
	frame->data = data;

	/* return */
	return RET_SUCCESS;
}

int frame_dump(const struct frame_t *frame, const char *path, int factor)
{
	FILE *stream;
	size_t width, height, depth;
	size_t bpp;
	size_t stride;
	size_t y, x;
	const int *data;
	int maxval;
	void *line;

	stream = fopen(path, "w");

	if (NULL == stream) {
		return RET_FAILURE_FILE_OPEN;
	}

	assert( frame );

	bpp = frame->bpp;

	maxval = (int) convert_bpp_to_maxval(bpp);

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	if ( width > ULONG_MAX || height > ULONG_MAX ) {
		return RET_FAILURE_OVERFLOW_ERROR;
	}

	if ( fprintf(stream, "P5\n%lu %lu\n%lu\n", (unsigned long) width, (unsigned long) height, (unsigned long) maxval) < 0 ) {
		return RET_FAILURE_FILE_IO;
	}

	data = frame->data;

	assert( data );
	assert( factor );

	depth = convert_bpp_to_depth(bpp);
	stride = width * depth;
	line = malloc( stride );

	if (NULL == line) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			int sample = data [y*width + x];
			int magnitude = abs_(sample) / factor;

			switch (depth) {
				case sizeof(char): {
					unsigned char *line_ = line;

					line_ [x] = (unsigned char) clamp(magnitude, 0, maxval);

					break;
				}
				case sizeof(short): {
					unsigned short *line_ = line;

					line_ [x] = native_to_be_s( (unsigned short) clamp(magnitude, 0, maxval) );

					break;
				}
				default:
					return RET_FAILURE_LOGIC_ERROR;
			}
		}

		if ( 1 != fwrite(line, stride, 1, stream) ) {
			return RET_FAILURE_FILE_IO;
		}
	}

	free(line);

	if (EOF == fclose(stream))
		return RET_FAILURE_FILE_IO;

	return RET_SUCCESS;
}

int frame_destroy(struct frame_t *frame)
{
	assert( frame );

	free(frame->data);

	frame->data = NULL;

	return RET_SUCCESS;
}
