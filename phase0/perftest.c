#include <stdio.h>
#include <stdlib.h>

#include "frame.h"
#include "common.h"
#include "dwt.h"

int main(int argc, char *argv[])
{
	struct frame frame;
	struct parameters parameters;

	if (argc < 2) {
		fprintf(stderr, "[ERROR] argument expected\n");
		return EXIT_FAILURE;
	}

	if ( frame_load_pgm(&frame, argv[1]) ) {
		fprintf(stderr, "[ERROR] unable to load image\n");
		return EXIT_FAILURE;
	}

	parameters.DWTtype = 0;

	if (dwt_encode(&frame, &parameters)) {
		fprintf(stderr, "[ERROR] transform failed\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
