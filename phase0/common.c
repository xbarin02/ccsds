#include "common.h"
#include <assert.h>

int eprintf(const char *format, ...)
{
	va_list ap;
	int n;

	va_start(ap, format);
	n = vfprintf(stderr, format, ap);
	va_end(ap);

	return n;
}

size_t ceil_multiple8(size_t n)
{
	return (n + 7) / 8 * 8;
}

int is_even(ptrdiff_t n)
{
	/* size_t is unsigned integer type */
	return !((size_t) n & 1);
}

int is_multiple8(ptrdiff_t n)
{
	return !((size_t) n & 7);
}

int init_parameters(struct parameters *parameters)
{
	int weight[12] = {
		0, 1, 1, 0,
		0, 2, 2, 1,
		3, 3, 3, 2
	};
	int i;

	assert(parameters);

	parameters->DWTtype = 0;
	parameters->S = 16;

	for (i = 0; i < 12; ++i) {
		parameters->weight[i] = weight[i];
	}

	return RET_SUCCESS;
}
