#include "common.h"

int safe_abs(int j)
{
	int r = abs(j);

	if (r < 0) {
		return INT_MAX;
	}

	return r;
}

int clamp(int v, int lo, int hi)
{
	return v < lo ? lo : ( hi < v ? hi : v );
}

int eprintf(const char *format, ...)
{
	va_list ap;
	int n;

	va_start(ap, format);
	n = vfprintf(stderr, format, ap);
	va_end(ap);

	return n;
}
