#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "frame.h"
#include "common.h"
#include "dwt.h"

double measure_dwt_encode_secs(struct frame *frame)
{
	struct parameters parameters;
	clock_t begin, end;

	if (frame_create_random(frame)) {
		fprintf(stderr, "[ERROR] frame allocation failed\n");
		return 0.;
	}

	parameters.DWTtype = 0;

	begin = clock();

	if (dwt_encode(frame, &parameters)) {
		fprintf(stderr, "[ERROR] transform failed\n");
		return 0.;
	}

	end = clock();

	frame_destroy(frame);

	return (double)(end - begin) / CLOCKS_PER_SEC;
}

int main()
{
	struct frame frame;

	double t;

	frame.width = 4096;
	frame.height = 2048;
	frame.bpp = 16;

	t = measure_dwt_encode_secs(&frame);

	printf("%f secs\n", t);

	frame_destroy(&frame);

	return EXIT_SUCCESS;
}
