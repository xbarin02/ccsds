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

struct frame_t frame_load_pgm(const char *path)
{
	FILE *stream;
	struct frame_t frame;
	char magic[2];
	int retval;
	unsigned long maxval;
	size_t y;
	void *data;
	int c;

	assert( path );

	frame.data = NULL;

	/* (1.1) open file */

	if (0 == strcmp(path, "-"))
		stream = stdin;
	else
		stream = fopen(path, "r");

	if (NULL == stream) {
		perror("fopen");
		return frame;
	}
	
	/* (1.2) read header */

	retval = fscanf(stream, "%c%c", magic, magic+1);
	if (retval != 2) {
		fprintf(stderr, "[ERROR] cannot read a magic number\n");
		return frame;
	}

	if (magic[0] != 'P') {
		fprintf(stderr, "[ERROR] invalid magic number\n");
		return frame;
	}

	switch (magic[1]) {
		case '5':
			/* P5 is supported */
			break;
		default:
			fprintf(stderr, "[ERROR] invalid magic number\n");
			return frame;
	}

	/* look ahead for a comment, ungetc */
	if( (c = getc(stream)) == '#' ) {
		char com[4096];
		fgets(com, 4096, stream);
	} else {
		ungetc(c, stream);
	}

	/* NOTE: C89 does not support 'z' length modifier */
	retval = fscanf(stream, " %lu", &frame.width);
	if (retval != 1) {
		fprintf(stderr, "[ERROR] cannot read a width\n");
		return frame;
	}

	/* look ahead for a comment, ungetc */
	if( (c = getc(stream)) == '#' ) {
		char com[4096];
		fgets(com, 4096, stream);
	} else {
		ungetc(c, stream);
	}

	retval = fscanf(stream, " %lu", &frame.height);
	if (retval != 1) {
		fprintf(stderr, "[ERROR] cannot read a height\n");
		return frame;
	}

	/* look ahead for a comment, ungetc */
	if( (c = getc(stream)) == '#' ) {
		char com[4096];
		fgets(com, 4096, stream);
	} else {
		ungetc(c, stream);
	}

	retval = fscanf(stream, " %lu", &maxval);
	if (retval != 1) {
		fprintf(stderr, "[ERROR] cannot read a maximum gray value\n");
		return frame;
	}

	switch (maxval) {
		case 255:
			frame.bpp = 8;
			break;
		default:
			fprintf(stderr, "[ERROR] unsupported pixel depth\n");
			return frame;
	}

	/* look ahead for a comment, ungetc */
	if( (c = getc(stream)) == '#' ) {
		char com[4096];
		fgets(com, 4096, stream);
	} else {
		ungetc(c, stream);
	}

	/* consume a single whitespace character */
	if ( !isspace(fgetc(stream)) ) {
		fprintf(stderr, "[ERROR] unexpected input\n");
		return frame;
	}

	/* allocate a raster */
	data = malloc(frame.width * frame.height);

	if (NULL == data) {
		fprintf(stderr, "[ERROR] cannot allocate a memory\n");
		return frame;
	}

	/* (1.3) load data */
	for (y = 0; y < frame.height; ++y) {
		/* load a single row */
		if( 1 != fread((char *)data + y*frame.width, frame.width, 1, stream) ) {
			fprintf(stderr, "[ERROR] end-of-file or error while reading a row\n");
			return frame;
		}
	}

	/* fill the frame struct */
	frame.data = data;

	if (stream != stdin) {
		fclose(stream);
	}

	return frame;
}

int main(int argc, char *argv[])
{
	struct frame_t frame;

	if (argc < 2) {
		fprintf(stderr, "[ERROR] argument expected\n");
		return 1;
	}

	/** (1) load input image */
	frame = frame_load_pgm(argv[1]);

	if (NULL == frame.data) {
		return 1;
	}

	fprintf(stdout, "[DEBUG] frame %lu %lu %lu\n", frame.width, frame.height, frame.bpp);

	/** (2) DWT */

	/** (3) BPE */

	/** (1) free image */
	frame_free(&frame);

	return 0;
}
