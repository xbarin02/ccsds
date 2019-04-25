#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <float.h>

#include "config.h"
#include "common.h"
#include "frame.h"
#include "dwt.h"

/* number of measurements */
#define MEASUREMENTS_NO 5
#define BPP 8
#define DWTTYPE 0

/* single measurement */
double measure_dwt_secs(struct frame *frame)
{
	struct parameters parameters;
	clock_t begin, end;
	int err;

	if (frame_create_random(frame)) {
		fprintf(stderr, "[ERROR] frame allocation failed\n");
		return 0.;
	}

	parameters.DWTtype = DWTTYPE;

	begin = clock();

#if (CONFIG_PERFTEST_DIR == 0)
	err = dwt_encode(frame, &parameters);
#endif
#if (CONFIG_PERFTEST_DIR == 1)
	err = dwt_decode(frame, &parameters);
#endif

	if (err) {
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
double measure_dwt_secs_point(size_t height, size_t width)
{
	struct frame frame;
	double min_t = HUGE_VAL;
	int i;

	frame.height = height;
	frame.width = width;
	frame.bpp = BPP;

	for (i = 0; i < MEASUREMENTS_NO; ++i) {
		double t = measure_dwt_secs(&frame);

		if (t < DBL_MIN) {
			return 0.;
		}

		if (t < min_t)
			min_t = t;
	}

	return min_t;
}

int measurement_dwt()
{
	size_t k;

	for (k = 1; k < CONFIG_PERFTEST_NUM; ++k) {
#if (CONFIG_PERFTEST_TYPE == 0)
		size_t width = 160 * k;
		size_t height = 120 * k;
#endif
#if (CONFIG_PERFTEST_TYPE == 1)
		size_t width = 1024;
		size_t height = 1024 * k;
#endif
#if (CONFIG_PERFTEST_TYPE == 2)
		size_t width = 256 * k;
		size_t height = 144 * k;
#endif

		size_t resolution = height * width;

		double secs = measure_dwt_secs_point(height, width);
		double nsecs_per_pel = secs / (double) resolution * 1e9;

		fprintf(stdout, "# %lu %lu\n", (unsigned long) width, (unsigned long) height);
		fprintf(stdout, "%lu\t%f\n", (unsigned long) resolution, nsecs_per_pel);
		fflush(stdout);
	}

	return RET_SUCCESS;
}

int main()
{
	measurement_dwt();

	return EXIT_SUCCESS;
}
