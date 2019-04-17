#include "common.h"

int floor_(double x)
{
	/*
	 * per C89 standard, 6.2.1.3 Floating and integral:
	 *
	 * When a value of floating type is convened to integral type,
	 * the fractional part is discarded. If the value of the integral part
	 * cannot be represented by the integral type, the behavior is
	 * undetined.
	 *
	 * "... discarded", i.e., the value is truncated toward zero
	 */

	/* truncate */
	int i = (int) x;

	/* convert trunc to floor */
	return i - (int) ( (double) i > x );
}

int roundf_(float x)
{
	int i;

	/* convert round to floor */
	x = x + .5f;

	/*
	 * per C89 standard, 6.2.1.3 Floating and integral:
	 *
	 * When a value of floating type is convened to integral type,
	 * the fractional part is discarded. If the value of the integral part
	 * cannot be represented by the integral type, the behavior is
	 * undetined.
	 *
	 * "... discarded", i.e., the value is truncated toward zero
	 */

	/* truncate */
	i = (int) x;

	/* convert trunc to floor */
	return i - (int) ( (float) i > x );
}

int round_(double x)
{
	int i;

	/* convert round to floor */
	x = x + .5;

	/*
	 * per C89 standard, 6.2.1.3 Floating and integral:
	 *
	 * When a value of floating type is convened to integral type,
	 * the fractional part is discarded. If the value of the integral part
	 * cannot be represented by the integral type, the behavior is
	 * undetined.
	 *
	 * "... discarded", i.e., the value is truncated toward zero
	 */

	/* truncate */
	i = (int) x;

	/* convert trunc to floor */
	return i - (int) ( (double) i > x );
}
