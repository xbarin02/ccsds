#include <stdio.h>
#include <stdlib.h>

#include "frame.h"
#include "common.h"
#include "dwt.h"

int main()
{
	struct frame frame;
	struct parameters parameters;

	frame.width = 4096;
	frame.height = 2048;
	frame.bpp = 16;

	if (frame_create_random(&frame)) {
		fprintf(stderr, "[ERROR] frame allocation failed\n");
		return EXIT_FAILURE;
	}

	parameters.DWTtype = 0;

	if (dwt_encode(&frame, &parameters)) {
		fprintf(stderr, "[ERROR] transform failed\n");
		return EXIT_FAILURE;
	}

	frame_destroy(&frame);

	return EXIT_SUCCESS;
}
