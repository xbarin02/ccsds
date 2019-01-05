#include "frame.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <assert.h>

int frame_write_pgm_header(const struct frame *frame, FILE *stream)
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

int frame_write_pgm_data(const struct frame *frame, FILE *stream)
{
	size_t width_, height_, depth_;
	size_t width;
	size_t y, x;
	int maxval;
	void *line;
	const int *data;

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

	return RET_SUCCESS;
}

int frame_save_pgm(const struct frame *frame, const char *path)
{
	FILE *stream;
	int err;

	/* open file */
	if (0 == strcmp(path, "-"))
		stream = stdout;
	else
		stream = fopen(path, "w");

	if (NULL == stream) {
		dprint (("[ERROR] cannot open output file\n"));
		return RET_FAILURE_FILE_OPEN;
	}

	/* write header */
	err = frame_write_pgm_header(frame, stream);

	if (err) {
		return err;
	}

	/* write data */
	err = frame_write_pgm_data(frame, stream);

	if (err) {
		return err;
	}

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

int frame_read_pgm_header(struct frame *frame, FILE *stream)
{
	char magic[2];
	unsigned long maxval;
	size_t width, height;
	unsigned long width_l, height_l;
	size_t bpp;

	/* (1.2) read header */

	if (fscanf(stream, "%c%c ", magic, magic+1) != 2) {
		dprint (("[ERROR] cannot read a magic number\n"));
		return RET_FAILURE_FILE_IO;
	}

	if (magic[0] != 'P') {
		dprint (("[ERROR] invalid magic number\n"));
		return RET_FAILURE_FILE_UNSUPPORTED;
	}

	switch (magic[1]) {
		case '5':
			/* P5 is supported */
			break;
		default:
			dprint (("[ERROR] invalid magic number\n"));
			return RET_FAILURE_FILE_UNSUPPORTED;
	}

	if (stream_skip_comment(stream)) {
		return RET_FAILURE_FILE_IO;
	}

	/* C89 does not support 'z' length modifier */
	if (fscanf(stream, " %lu ", &width_l) != 1) {
		dprint (("[ERROR] cannot read a width\n"));
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
		dprint (("[ERROR] cannot read a height\n"));
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
		dprint (("[ERROR] cannot read a maximum gray value\n"));
		return RET_FAILURE_FILE_IO;
	}

	bpp = convert_maxval_to_bpp(maxval);

	if (bpp > 16) {
		dprint (("[ERROR] unsupported pixel depth\n"));
		return RET_FAILURE_FILE_UNSUPPORTED;
	}

	if (stream_skip_comment(stream)) {
		return RET_FAILURE_FILE_IO;
	}

	/* consume a single whitespace character */
	if ( !isspace(fgetc(stream)) ) {
		dprint (("[ERROR] unexpected input\n"));
		return RET_FAILURE_FILE_UNSUPPORTED;
	}

	/* fill the struct */
	assert( frame );

	frame->width = width;
	frame->height = height;
	frame->bpp = bpp;

	return RET_SUCCESS;
}

int frame_alloc_data(struct frame *frame)
{
	size_t width, height;
	int *data;

	assert( frame );

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	data = malloc( height * width * sizeof *data );

	if (NULL == data) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	frame->data = data;

	return RET_SUCCESS;
}

int frame_read_pgm_data(struct frame *frame, FILE *stream)
{
	size_t width_, height_, depth_;
	size_t width, height;
	size_t y, x;
	void *line;
	int *data;

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

	data = frame->data;

	assert( data );

	/* (2.1) copy the input raster into an array of 32-bit DWT coefficients, incl. padding */
	for (y = 0; y < height_; ++y) {
		/* read line */
		if ( fread(line, depth_, width_, stream) < width_ ) {
			dprint (("[ERROR] end-of-file or error while reading a row\n"));
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

	return RET_SUCCESS;
}

int frame_load_pgm(struct frame *frame, const char *path)
{
	FILE *stream;
	int err;

	/* open file */
	if (0 == strcmp(path, "-"))
		stream = stdin;
	else
		stream = fopen(path, "r");

	if (NULL == stream) {
		dprint (("[ERROR] unable to open input file\n"));
		return RET_FAILURE_FILE_OPEN;
	}

	/* read header */
	err = frame_read_pgm_header(frame, stream);

	if (err) {
		return err;
	}

	/* allocate framebuffer */
	err = frame_alloc_data(frame);

	if (err) {
		return err;
	}

	/* read data */
	err = frame_read_pgm_data(frame, stream);

	if (err) {
		return err;
	}

	/* close file */
	if (stream != stdin) {
		if (EOF == fclose(stream))
			return RET_FAILURE_FILE_IO;
	}

	assert( frame );

	dprint (("[INFO] frame %lu %lu %lu\n", (unsigned long) frame->width, (unsigned long) frame->height, (unsigned long) frame->bpp));

	/* return */
	return RET_SUCCESS;
}

int frame_dump(const struct frame *frame, const char *path, int factor)
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

void frame_destroy(struct frame *frame)
{
	assert( frame );

	free(frame->data);

	frame->data = NULL;
}

static void copy_band(int *dst, int *src, size_t height, size_t width, size_t stride_dst_x, size_t stride_dst_y, size_t stride_src_x, size_t stride_src_y)
{
	size_t y, x;

	assert( dst );
	assert( src );

	/* for each row */
	for (y = 0; y < height; ++y) {
		/* for each coefficient */
		for (x = 0; x < width; ++x) {
			dst[y*stride_dst_y + x*stride_dst_x] = src[y*stride_src_y + x*stride_src_x];
		}
	}
}

int frame_convert_chunked_to_semiplanar(struct frame *frame)
{
	size_t height, width;
	int *semiplanar_data, *chunked_data;
	int j;
	int err;

	assert( frame );

	height = ceil_multiple8(frame->height);
	width = ceil_multiple8(frame->width);

	chunked_data = frame->data;

	err = frame_alloc_data(frame);

	if (err) {
		return err;
	}

	semiplanar_data = frame->data;

	/* for each level */
	for (j = 1; j < 4; ++j) {
		/* shared for both the layouts */
		size_t width_j = width>>j, height_j = height>>j;

		/* semiplanar layout */
		size_t stride_semiplanar_x = 1;
		size_t stride_semiplanar_y = width;

		int *band_semiplanar_ll = semiplanar_data +        0*stride_semiplanar_y +       0*stride_semiplanar_x;
		int *band_semiplanar_hl = semiplanar_data +        0*stride_semiplanar_y + width_j*stride_semiplanar_x;
		int *band_semiplanar_lh = semiplanar_data + height_j*stride_semiplanar_y +       0*stride_semiplanar_x;
		int *band_semiplanar_hh = semiplanar_data + height_j*stride_semiplanar_y + width_j*stride_semiplanar_x;

		/* chunked layout */
		size_t stride_chunked_x = (1U << j);
		size_t stride_chunked_y = (1U << j) * width;

		int *band_chunked_ll = chunked_data + 0 + 0;
		int *band_chunked_hl = chunked_data + 0 + stride_chunked_x/2;
		int *band_chunked_lh = chunked_data + stride_chunked_y/2 + 0;
		int *band_chunked_hh = chunked_data + stride_chunked_y/2 + stride_chunked_x/2;

		/* for each subband (HL, LH, HH) */
		copy_band(band_semiplanar_hl, band_chunked_hl, height_j, width_j, stride_semiplanar_x, stride_semiplanar_y, stride_chunked_x, stride_chunked_y);
		copy_band(band_semiplanar_lh, band_chunked_lh, height_j, width_j, stride_semiplanar_x, stride_semiplanar_y, stride_chunked_x, stride_chunked_y);
		copy_band(band_semiplanar_hh, band_chunked_hh, height_j, width_j, stride_semiplanar_x, stride_semiplanar_y, stride_chunked_x, stride_chunked_y);

		/* LL */
		if (j == 3) {
			copy_band(band_semiplanar_ll, band_chunked_ll, height_j, width_j, stride_semiplanar_x, stride_semiplanar_y, stride_chunked_x, stride_chunked_y);
		}
	}

	free(chunked_data);

	return RET_SUCCESS;
}

int frame_clone(const struct frame *frame, struct frame *cloned_frame)
{
	int err;
	size_t width, height;

	assert( frame );
	assert( cloned_frame );

	*cloned_frame = *frame;

	err = frame_alloc_data(cloned_frame);

	if (err) {
		return err;
	}

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	memcpy(cloned_frame->data, frame->data, height * width * sizeof(int));

	return RET_SUCCESS;
}

int frame_dump_chunked_as_semiplanar(const struct frame *frame, const char *path, int factor)
{
	struct frame clone;
	int err;

	err = frame_clone(frame, &clone);

	if (err) {
		return err;
	}

	err = frame_convert_chunked_to_semiplanar(&clone);

	if (err) {
		return err;
	}

	err = frame_dump(&clone, path, factor);

	if (err) {
		return err;
	}

	frame_destroy(&clone);

	return RET_SUCCESS;
}

int frame_dump_mse(const struct frame *frameA, const struct frame *frameB)
{
	size_t height, width;
	size_t y, x;
	int *dataA, *dataB;
	double mse;

	assert( frameA );
	assert( frameB );

	if ( frameA->width != frameB->width
	  || frameA->height != frameB->height
	  || frameA->bpp != frameB->bpp ) {
		dprint (("[ERROR] frame dimensions must be identical\n"));
		return RET_FAILURE_FILE_UNSUPPORTED;
	}

	height = frameA->height;
	width = frameA->width;

	dataA = frameA->data;
	dataB = frameB->data;

	assert( dataA );
	assert( dataB );

	mse = 0.;

	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			int pixA = dataA[y*width + x];
			int pixB = dataB[y*width + x];
			int e;

			if ( (pixA < 0 && pixB > INT_MAX + pixA)
			  || (pixA > 0 && pixB < INT_MIN + pixA) ) {
				return RET_FAILURE_OVERFLOW_ERROR;
			}

			/* error */
			e = pixB - pixA;

			if ( (e > 0 && e > INT_MAX / e)
			  || (e < 0 && e < INT_MAX / e) ) {
				return RET_FAILURE_OVERFLOW_ERROR;
			}

			/* add squared error */
			mse += (unsigned)(e*e);
		}
	}

	/* compute mean */
	mse /= (double)height;
	mse /= (double)width;

	dprint (("[INFO] mse = %f\n", mse));

	/*
	 * The following code computes the PSNR which seems to be a bit
	 * unnecessary for a quick comparison. Moreover, it requires linking
	 * against -lm.
	 */
#if 0
	maxval = convert_bpp_to_maxval(frameA->bpp);

	psnr = 10. * log10( (double)maxval * (double)maxval / mse );

	dprint (("[INFO] psnr = %f dB\n", psnr));
#endif
	return RET_SUCCESS;
}
