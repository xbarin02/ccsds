/**
 * C89 implementation of CCSDS 122.0-B-2 compressor
 *
 * supported features:
 * - lossy (Float DWT) and lossless compression (Integer DWT)
 * - frame-based input
 *
 * @author David Barina <ibarina@fit.vutbr.cz>
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "common.h"
#include "frame.h"
#include "dwt.h"
#include "bio.h"
#include "bpe.h"

#if 0
int bpe_encode(const struct frame *frame, const struct parameters *parameters)
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
	struct frame frame, input_frame;
	struct parameters parameters;
	struct bio bio;
	void *ptr;

	/* NOTE Since we implement the floor function for negative numbers using
	 * an arithmetic right shift, we must check whether the underlying
	 * signed integer representation is two's complement. */
	assert( ~-1 == 0 );

	if (argc < 2) {
		fprintf(stderr, "[ERROR] argument expected\n");
		return EXIT_FAILURE;
	}

	/** (1) load input image */

	dprint (("[DEBUG] loading...\n"));

	if ( frame_load_pgm(&frame, argv[1]) ) {
		fprintf(stderr, "[ERROR] unable to load image\n");
		return EXIT_FAILURE;
	}

	if ( frame.width > (1<<20) || frame.width < 17 ) {
		fprintf(stderr, "[ERROR] unsupported image width\n");
		return EXIT_FAILURE;
	}

	if ( frame.height < 17 ) {
		fprintf(stderr, "[ERROR] unsupported image height\n");
		return EXIT_FAILURE;
	}

	frame_dump(&frame, "input.pgm", 1);

	if (frame_clone(&frame, &input_frame)) {
		fprintf(stderr, "[ERROR] unable to clone the frame\n");
		return EXIT_FAILURE;
	}

	init_parameters(&parameters);

	parameters.DWTtype = 0;

	dprint (("[DEBUG] transform...\n"));

	/** (2) forward DWT */
	if (dwt_encode(&frame, &parameters)) {
		fprintf(stderr, "[ERROR] transform failed\n");
		return EXIT_FAILURE;
	}

	dprint (("[DEBUG] dump...\n"));

	frame_dump_chunked_as_semiplanar(&frame, "dwt3.pgm", 8);

	/** (3) BPE */
#if 0
	bpe_encode(&frame, &parameters);
#endif
	ptr = malloc(get_maximum_stream_size(&frame));

	if (ptr == NULL) {
		fprintf(stderr, "[ERROR] malloc failed\n");
		return EXIT_FAILURE;
	}

	bio_open(&bio, ptr, BIO_MODE_WRITE);
	bpe_encode(&frame, &parameters, &bio);
	bio_close(&bio);

	/* rewrite the frame with random data */
	frame_create_random(&frame);

	bio_open(&bio, ptr, BIO_MODE_READ);
	bpe_decode(&frame, &parameters, &bio);
	bio_close(&bio);

	frame_dump_chunked_as_semiplanar(&frame, "dwt3-decoded.pgm", 8);

	dprint (("[DEBUG] inverse transform...\n"));

	/** (2) inverse DWT */
	if (dwt_decode(&frame, &parameters)) {
		fprintf(stderr, "[ERROR] inverse transform failed\n");
		return EXIT_FAILURE;
	}

	frame_dump(&frame, "decoded.pgm", 1);

	if (frame_dump_mse(&frame, &input_frame)) {
		fprintf(stderr, "[DEBUG] unable to compute MSE\n");
		return EXIT_FAILURE;
	}

	/** (1) save output image */

	dprint (("[DEBUG] saving...\n"));

	if ( frame_save_pgm(&frame, "output.pgm") ) {
		fprintf(stderr, "[ERROR] unable to save an output raster\n");
		return EXIT_FAILURE;
	}

	/** (1) release resources */

	frame_destroy(&frame);

	frame_destroy(&input_frame);

	return EXIT_SUCCESS;
}
