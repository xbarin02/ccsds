#include "common.h"
#include <assert.h>

#define M27 134217727

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

	assert(parameters != NULL);

	parameters->DWTtype = 0;
	parameters->S = 16;

	for (i = 0; i < 12; ++i) {
		parameters->weight[i] = weight[i];
	}

	parameters->SegByteLimit = M27;

	return RET_SUCCESS;
}

static INT32 int32_abs(INT32 j)
{
#if (USHRT_MAX == UINT32_MAX_)
	return (INT32) abs((int) j);
#elif (UINT_MAX == UINT32_MAX_)
	return (INT32) abs((int) j);
#elif (ULONG_MAX == UINT32_MAX_)
	return (INT32) labs((long int) j);
#else
#	error "Not implemented"
#endif
}

UINT32 uint32_abs(INT32 j)
{
	if (j == INT32_MIN_) {
		return (UINT32)INT32_MAX_ + 1;
	}

	return (UINT32) int32_abs(j);
}

/* Round up to the next highest power of 2 */
static UINT32 uint32_ceil_pow2(UINT32 v)
{
	assert(v != 0);

	v--;

	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;

	v++;

	return v;
}

static size_t uint32_floor_log2(UINT32 n)
{
	size_t r = 0;

	assert(n != 0);

	while (n >>= 1) {
		r++;
	}

	return r;
}

size_t uint32_ceil_log2(UINT32 n)
{
	return uint32_floor_log2(uint32_ceil_pow2(n));
}
