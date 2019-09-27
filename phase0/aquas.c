#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "common.h"
#include "frame.h"
#include "dwt.h"
#include "bio.h"
#include "bpe.h"

#include "Lenna256.h"

/* 1<<19 == 512 kilobytes */
unsigned char compressed_bitstream[1<<19];

int main()
{
	struct parameters parameters;
	struct bio bio;
	struct frame output_frame;

	init_parameters(&parameters);

	dprint (("[DEBUG] saving the input image...\n"));

	if (frame_save_pgm(&input_frame, "input.pgm")) {
		fprintf(stderr, "[ERROR] unable to save an output raster\n");
		return EXIT_FAILURE;
	}

	dprint (("[DEBUG] wavelet transform...\n"));

	if (dwt_encode(&input_frame, &parameters)) {
		fprintf(stderr, "[ERROR] transform failed\n");
		return EXIT_FAILURE;
	}

	dprint (("[DEBUG] bit-plane encoder...\n"));

	bio_open(&bio, compressed_bitstream, BIO_MODE_WRITE);
	bpe_encode(&input_frame, &parameters, &bio);
	bio_close(&bio);

	dprint (("[DEBUG] bit-plane decoder...\n"));

	output_frame = input_frame;
	output_frame.data = NULL;

	bio_open(&bio, compressed_bitstream, BIO_MODE_READ);
	bpe_decode(&output_frame, &parameters, &bio);
	bio_close(&bio);

	dprint (("[DEBUG] inverse wavelet transform...\n"));

	if (dwt_decode(&output_frame, &parameters)) {
		fprintf(stderr, "[ERROR] inverse transform failed\n");
		return EXIT_FAILURE;
	}

	dprint (("[DEBUG] saving the output image...\n"));

	if (frame_save_pgm(&output_frame, "output.pgm")) {
		fprintf(stderr, "[ERROR] unable to save an output raster\n");
		return EXIT_FAILURE;
	}

	frame_destroy(&output_frame);

	return 0;
}
