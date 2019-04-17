#include "common.h"

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
