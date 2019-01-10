#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <float.h>

#include "frame.h"
#include "common.h"
#include "dwt.h"
#include "utils.h"

/* number of measurements */
#define MEASUREMENTS_NO 5
#define BPP 8

/* single measurement */
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

	if (begin == (clock_t) -1 || end == (clock_t) -1) {
		return 0.;
	}

	return (double)(end - begin) / CLOCKS_PER_SEC;
}

/* multiple measurements */
double measure_dwt_encode_secs_point(size_t height, size_t width)
{
	struct frame frame;
	double min_t = HUGE_VAL;
	int i;

	frame.height = height;
	frame.width = width;
	frame.bpp = BPP;

	for (i = 0; i < MEASUREMENTS_NO; ++i) {
		double t = measure_dwt_encode_secs(&frame);

		if (t < DBL_MIN) {
			return 0.;
		}

		/* printf("%f secs\n", t); */

		if (t < min_t)
			min_t = t;
	}

	return min_t;
}

int measurement_dwt_encode()
{
	size_t k;

	for(k = 1; k < 10; ++k) {
		size_t width = k * 160;
		size_t height = k * 120;

		size_t resolution = height * width;

		double t = measure_dwt_encode_secs_point(height, width);

		fprintf(stdout, "# %lu %lu\n", (unsigned long) width, (unsigned long) height);
		fprintf(stdout, "%lu\t%f\n", (unsigned long) resolution, t);
	}

	return RET_SUCCESS;
}

int main()
{
	measurement_dwt_encode();

	return EXIT_SUCCESS;
}
