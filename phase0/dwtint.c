#include "config.h"
#include "common.h"
#include "dwt.h"
#include "dwtint.h"

#include <stddef.h>
#include <assert.h>

static int floor_div_pow2(int numerator, int log2_denominator)
{
	/* NOTE per C89 standard, the right shift of negative signed type is implementation-defined */
	if (numerator < 0)
		return ~(~numerator >> log2_denominator);
	else
		return numerator >> log2_denominator;
}

/**
 * \brief Round the integer fraction \f$ a/2^b \f$ to nearest integer
 *
 * Returns \f$ \mathrm{round} ( \mathrm{numerator} / 2^\mathrm{log2\_denominator} ) \f$.
 * The result is undefined for \p log2_denominator smaller than 1.
 */
static int round_div_pow2(int numerator, int log2_denominator)
{
	return floor_div_pow2(numerator + (1 << (log2_denominator - 1)), log2_denominator);
}

static void dwtint_encode_core(int data[2], int buff[5], int lever)
{
	int c0 = buff[0];
	int c1 = buff[1];
	int c2 = buff[2];
	int d3 = buff[3];
	int d4 = buff[4];

	int x0 = data[0];
	int x1 = data[1];

	switch (lever) {
		case -1:
			d3 = d3 - round_div_pow2(
				+9*c1 +8*c0 -1*x1,
				4
			);
			break;
		case +1:
			d3 = d3 - round_div_pow2(
				-1*c2 +9*c1 +8*c0,
				4
			);
			break;
		case +2:
			d3 = d3 - round_div_pow2(
				-2*c2 +9*c1 +9*c1,
				4
			);
			break;
		default:
			d3 = d3 - round_div_pow2(
				-1*c2 +9*c1 +9*c0 -1*x1,
				4
			);
	}

	buff[0] = x1;
	buff[1] = c0;
	buff[2] = c1;
	buff[3] = x0;
	buff[4] = d3;

	switch (lever) {
		case -1:
			c1 = c1 - round_div_pow2(
				-d3,
				1
			);
			break;
		default:
			c1 = c1 - round_div_pow2(
				-1*d4 -1*d3,
				2
			);
	}

	data[0] = c1;
	data[1] = d3;
}

static void dwtint_decode_core(int data[2], int buff[5], int lever)
{
	int c0 = buff[0];
	int c1 = buff[1];
	int c2 = buff[2];
	int d3 = buff[3];
	int d4 = buff[4];

	int x0 = data[0];
	int x1 = data[1];

	switch (lever) {
		case -3:
			x0 = x0 + round_div_pow2(
				-x1,
				1
			);
			break;
		default:
			x0 = x0 + round_div_pow2(
				-1*d3 -1*x1,
				2
			);
	}

	switch (lever) {
		case -1:
			d4 = d4 + round_div_pow2(
				+9*c1 +8*c0 -1*x0,
				4
			);
			break;
		case +1:
			d4 = d4 + round_div_pow2(
				-1*c2 +9*c1 +8*c0,
				4
			);
			break;
		case +2:
			d4 = d4 + round_div_pow2(
				-1*c2 +9*c1,
				3
			);
			break;
		default:
			d4 = d4 + round_div_pow2(
				-1*c2 +9*c1 +9*c0 -1*x0,
				4
			);
	}

	buff[0] = x0;
	buff[1] = c0;
	buff[2] = c1;
	buff[3] = x1;
	buff[4] = d3;

	data[0] = d4;
	data[1] = c0;
}

static void encode_adjust_levers(int lever[1], ptrdiff_t n, ptrdiff_t N)
{
	lever[0] = 0;

	if (n == 2)
		lever[0] = -1;
	if (n == N)
		lever[0] = +1;
	if (n == N+1)
		lever[0] = +2;
}

static void decode_adjust_levers(int lever[1], ptrdiff_t n, ptrdiff_t N)
{
	lever[0] = 0;

	if (n == 0)
		lever[0] = -3;
	if (n == 1)
		lever[0] = -2;
	if (n == 2)
		lever[0] = -1;
	if (n == N)
		lever[0] = +1;
	if (n == N+1)
		lever[0] = +2;
}

static void transpose(int core[4])
{
	int t = core[1];
	core[1] = core[2];
	core[2] = t;
}

/*static*/ void dwtint_encode_core2(int core[4], int *buff_y, int *buff_x, int lever[2])
{
	/* horizontal filtering */
	dwtint_encode_core(&core[0], buff_y + 5*(0), lever[1]);
	dwtint_encode_core(&core[2], buff_y + 5*(1), lever[1]);
	transpose(core);
	/* vertical filtering */
	dwtint_encode_core(&core[0], buff_x + 5*(0), lever[0]);
	dwtint_encode_core(&core[2], buff_x + 5*(1), lever[0]);
	transpose(core);
}

/*static*/ void dwtint_decode_core2(int core[4], int *buff_y, int *buff_x, int lever[2])
{
	transpose(core);
	/* vertical filtering */
	dwtint_decode_core(&core[0], buff_x + 5*(0), lever[0]);
	dwtint_decode_core(&core[2], buff_x + 5*(1), lever[0]);
	transpose(core);
	/* horizontal filtering */
	dwtint_decode_core(&core[0], buff_y + 5*(0), lever[1]);
	dwtint_decode_core(&core[2], buff_y + 5*(1), lever[1]);
}

#define signal_defined(n, N) ( (n) >= 0 && (n) < (N) )

void dwtint_encode_quad(int *data, ptrdiff_t N_y, ptrdiff_t N_x, ptrdiff_t stride_y, ptrdiff_t stride_x, int *buff_y, int *buff_x, ptrdiff_t n_y, ptrdiff_t n_x)
{
	/* vertical lever at [0], horizontal at [1] */
	int lever[2];
	/* order on input: 0=HH, 1=LH, 2=HH, 3=LL */
	int core[4];

	/* we cannot access buff_x[] and buff_y[] at negative indices */
	if ( n_y < 0 || n_x < 0 )
		return;

	encode_adjust_levers(lever+0, n_y, N_y);
	encode_adjust_levers(lever+1, n_x, N_x);

#	define cc(n_y, n_x) data[ stride_y*(2*(n_y)+0) + stride_x*(2*(n_x)+0) ] /* LL */
#	define dc(n_y, n_x) data[ stride_y*(2*(n_y)+0) + stride_x*(2*(n_x)+1) ] /* HL */
#	define cd(n_y, n_x) data[ stride_y*(2*(n_y)+1) + stride_x*(2*(n_x)+0) ] /* LH */
#	define dd(n_y, n_x) data[ stride_y*(2*(n_y)+1) + stride_x*(2*(n_x)+1) ] /* HH */

	core[0] = signal_defined(n_y-1, N_y) && signal_defined(n_x-1, N_x) ? (int) dd(n_y-1, n_x-1) : 0; /* HH */
	core[1] = signal_defined(n_y-1, N_y) && signal_defined(n_x-0, N_x) ? (int) cd(n_y-1, n_x-0) : 0; /* LH */
	core[2] = signal_defined(n_y-0, N_y) && signal_defined(n_x-1, N_x) ? (int) dc(n_y-0, n_x-1) : 0; /* HL */
	core[3] = signal_defined(n_y-0, N_y) && signal_defined(n_x-0, N_x) ? (int) cc(n_y-0, n_x-0) : 0; /* LL */

	dwtint_encode_core2(core, buff_y + 5*(2*n_y+0), buff_x + 5*(2*n_x+0), lever);

	if (signal_defined(n_y-2, N_y) && signal_defined(n_x-2, N_x)) {
		cc(n_y-2, n_x-2) = ( core[0] ); /* LL */
		dc(n_y-2, n_x-2) = ( core[1] ); /* HL */
		cd(n_y-2, n_x-2) = ( core[2] ); /* LH */
		dd(n_y-2, n_x-2) = ( core[3] ); /* HH */
	}

#	undef cc
#	undef dc
#	undef cd
#	undef dd
}

void dwtint_decode_quad(int *data, ptrdiff_t N_y, ptrdiff_t N_x, ptrdiff_t stride_y, ptrdiff_t stride_x, int *buff_y, int *buff_x, ptrdiff_t n_y, ptrdiff_t n_x)
{
	/* vertical lever at [0], horizontal at [1] */
	int lever[2];
	/* order on input: 0=LL, 1=HL, 2=LH, 3=HH */
	int core[4];

	/* we cannot access buff_x[] and buff_y[] at negative indices */
	if ( n_y < 0 || n_x < 0 )
		return;

	decode_adjust_levers(lever+0, n_y, N_y);
	decode_adjust_levers(lever+1, n_x, N_x);

#	define cc(n_y, n_x) data[ stride_y*(2*(n_y)+0) + stride_x*(2*(n_x)+0) ] /* LL */
#	define dc(n_y, n_x) data[ stride_y*(2*(n_y)+0) + stride_x*(2*(n_x)+1) ] /* HL */
#	define cd(n_y, n_x) data[ stride_y*(2*(n_y)+1) + stride_x*(2*(n_x)+0) ] /* LH */
#	define dd(n_y, n_x) data[ stride_y*(2*(n_y)+1) + stride_x*(2*(n_x)+1) ] /* HH */

	if ( signal_defined(n_y-0, N_y) && signal_defined(n_x-0, N_x) ) {
		core[0] = (int) cc(n_y, n_x); /* LL */
		core[1] = (int) dc(n_y, n_x); /* HL */
		core[2] = (int) cd(n_y, n_x); /* LH */
		core[3] = (int) dd(n_y, n_x); /* HH */
	} else {
		core[0] = 0;
		core[1] = 0;
		core[2] = 0;
		core[3] = 0;
	}

	dwtint_decode_core2(core, buff_y + 5*(2*n_y+0), buff_x + 5*(2*n_x+0), lever);

	if ( signal_defined(n_y-1, N_y) && signal_defined(n_x-1, N_x) )
		cc(n_y-1, n_x-1) = ( core[3] ); /* LL */
	if ( signal_defined(n_y-1, N_y) && signal_defined(n_x-2, N_x) )
		dc(n_y-1, n_x-2) = ( core[2] ); /* HL */
	if ( signal_defined(n_y-2, N_y) && signal_defined(n_x-1, N_x) )
		cd(n_y-2, n_x-1) = ( core[1] ); /* LH */
	if ( signal_defined(n_y-2, N_y) && signal_defined(n_x-2, N_x) )
		dd(n_y-2, n_x-2) = ( core[0] ); /* HH */

#	undef cc
#	undef dc
#	undef cd
#	undef dd
}

void dwtint_encode_line_segment(int *line, ptrdiff_t size, ptrdiff_t stride, int *buff, ptrdiff_t n0, ptrdiff_t n1)
{
	ptrdiff_t n, N;
	int data[2];

	assert( size > 0 && is_even(size) );

	N = size / 2;

#	define c(n) line[stride*(2*(n)+0)]
#	define d(n) line[stride*(2*(n)+1)]

	n = n0;

	/* prologue */
	for (; n < n1 && n < 1; n++) {
		int lever = -3;

		data[0] = 0;
		data[1] = (int) c(n);
		dwtint_encode_core(data, buff, lever);
	}
	for (; n < n1 && n < 2; n++) {
		int lever = -2;

		data[0] = (int) d(n-1);
		data[1] = (int) c(n);
		dwtint_encode_core(data, buff, lever);
	}
	for (; n < n1 && n < 3; n++) {
		int lever = -1;

		data[0] = (int) d(n-1);
		data[1] = (int) c(n);
		dwtint_encode_core(data, buff, lever); /* 0 */
		c(n-2) = ( data[0] );
		d(n-2) = ( data[1] );
	}
	/* regular */
	for (; n < n1 && n < N; n++) {
		int lever = 0;

		data[0] = (int) d(n-1);
		data[1] = (int) c(n);
		dwtint_encode_core(data, buff, lever); /* j */
		c(n-2) = ( data[0] );
		d(n-2) = ( data[1] );
	}
	/* epilogue */
	for (; n < n1 && n == N; n++) {
		int lever = +1;

		data[0] = (int) d(n-1);
		data[1] = 0;
		dwtint_encode_core(data, buff, lever); /* N-2 */
		c(n-2) = ( data[0] );
		d(n-2) = ( data[1] );
	}
	for (; n < n1 && n == N+1; n++) {
		int lever = +2;

		data[0] = 0;
		data[1] = 0;
		dwtint_encode_core(data, buff, lever); /* N-1 */
		c(n-2) = ( data[0] );
		d(n-2) = ( data[1] );
	}

#undef c
#undef d
}

int dwtint_encode_line(int *line, ptrdiff_t size, ptrdiff_t stride)
{
#if (CONFIG_DWT1_MODE == 2)
	ptrdiff_t N;
	int buff[5] = { 0, 0, 0, 0, 0 };

	assert( size > 0 && is_even(size) );

	N = size / 2;

	dwtint_encode_line_segment(line, size, stride, buff, 0, N+2);

	return RET_SUCCESS;
#endif
#if (CONFIG_DWT1_MODE == 1)
	ptrdiff_t n, N;

	assert( size > 0 && is_even(size) );

	N = size / 2;

	/* lifting */

	/* subbands: D (H) at odd indices, C (L) at even indices */
#define c(n) line[stride*(2*(n)+0)]
#define d(n) line[stride*(2*(n)+1)]

	assert( line );

	d(0) = d(0) - round_div_pow2(
		-1*c(1) +9*c(0) +9*c(1) -1*c(2),
		4
	);

	for (n = 1; n <= N-3; ++n) {
		d(n) = d(n) - round_div_pow2(
			-1*c(n-1) +9*c(n) +9*c(n+1) -1*c(n+2),
			4
		);
	}

	d(N-2) = d(N-2) - round_div_pow2(
		-1*c(N-3) +9*c(N-2) +9*c(N-1) -1*c(N-1),
		4
	);

	d(N-1) = d(N-1) - round_div_pow2(
		-1*c(N-2) +9*c(N-1),
		3
	);

	c(0) = c(0) - round_div_pow2(-d(0), 1);

	for (n = 1; n <= N-1; ++n) {
		c(n) = c(n) - round_div_pow2(
			-1*d(n-1) -1*d(n),
			2
		);
	}

#undef c
#undef d

	return RET_SUCCESS;
#endif
}

int dwtint_decode_line(int *line, ptrdiff_t size, ptrdiff_t stride)
{
	ptrdiff_t n, N;

	assert( size > 0 && is_even(size) );

	N = size / 2;

#define c(n) line[stride*(2*(n)+0)]
#define d(n) line[stride*(2*(n)+1)]

	assert( line );

	/* inverse lifting */

	c(0) = c(0) + round_div_pow2(-d(0), 1);

	for (n = 1; n <= N-1; ++n) {
		c(n) = c(n) + round_div_pow2(
			-1*d(n-1) -1*d(n),
			2
		);
	}

	d(0) = d(0) + round_div_pow2(
		-1*c(1) +9*c(0) +9*c(1) -1*c(2),
		4
	);

	for (n = 1; n <= N-3; ++n) {
		d(n) = d(n) + round_div_pow2(
			-1*c(n-1) +9*c(n) +9*c(n+1) -1*c(n+2),
			4
		);
	}

	d(N-2) = d(N-2) + round_div_pow2(
		-1*c(N-3) +9*c(N-2) +9*c(N-1) -1*c(N-1),
		4
	);

	d(N-1) = d(N-1) + round_div_pow2(
		-1*c(N-2) +9*c(N-1),
		3
	);

#undef c
#undef d

	return RET_SUCCESS;
}

void dwtint_weight_line(int *line, ptrdiff_t size, ptrdiff_t stride, int weight)
{
	ptrdiff_t n;

	assert( line );

	for (n = 0; n < size; ++n) {
		line[stride*n] <<= weight;
	}
}

/*
 * inverse function to dwt_weight_line
 */
void dwtint_unweight_line(int *line, ptrdiff_t size, ptrdiff_t stride, int weight)
{
	ptrdiff_t n;

	assert( line );

	for (n = 0; n < size; ++n) {
		line[stride*n] >>= weight;
	}
}

int dwtint_encode_band(int *band, ptrdiff_t stride_y, ptrdiff_t stride_x, ptrdiff_t height, ptrdiff_t width)
{
	ptrdiff_t y, x;

#if (CONFIG_DWT2_MODE == 0)
	/* for each row */
	for (y = 0; y < height; ++y) {
		/* invoke one-dimensional transform */
		dwtint_encode_line(band + y*stride_y, width, stride_x);
	}
	/* for each column */
	for (x = 0; x < width; ++x) {
		/* invoke one-dimensional transform */
		dwtint_encode_line(band + x*stride_x, height, stride_y);
	}
#endif
#if (CONFIG_DWT2_MODE == 2)
	int *buff_y, *buff_x;

	buff_y = malloc( (size_t) (height+4) * 5 * sizeof(int) );
	buff_x = malloc( (size_t) (width +4) * 5 * sizeof(int) );

	if (NULL == buff_y || NULL == buff_x) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	for (y = 0; y < height/2+2; ++y) {
		for (x = 0; x < width/2+2; ++x) {
			dwtint_encode_quad(band, height/2, width/2, stride_y, stride_x, buff_y, buff_x, y, x);
		}
	}

	free(buff_x);
	free(buff_y);
#endif

	return RET_SUCCESS;
}

int dwtint_decode_band(int *band, ptrdiff_t stride_y, ptrdiff_t stride_x, ptrdiff_t height, ptrdiff_t width)
{
	ptrdiff_t y, x;

#if (CONFIG_DWT2_MODE == 0) || (CONFIG_DWT2_MODE == 1)
	/* for each column */
	for (x = 0; x < width; ++x) {
		/* invoke one-dimensional transform */
		dwtint_decode_line(band + x*stride_x, height, stride_y);
	}
	/* for each row */
	for (y = 0; y < height; ++y) {
		/* invoke one-dimensional transform */
		dwtint_decode_line(band + y*stride_y, width, stride_x);
	}
#endif
#if (CONFIG_DWT2_MODE == 2)
	int *buff_y, *buff_x;

	buff_y = malloc( (size_t) (height+4) * 5 * sizeof(int) );
	buff_x = malloc( (size_t) (width +4) * 5 * sizeof(int) );

	if (NULL == buff_y || NULL == buff_x) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	for (y = 0; y < height/2+2; ++y) {
		for (x = 0; x < width/2+2; ++x) {
			dwtint_decode_quad(band, height/2, width/2, stride_y, stride_x, buff_y, buff_x, y, x);
		}
	}

	free(buff_x);
	free(buff_y);
#endif

	return RET_SUCCESS;
}

void dwtint_weight_band(int *band, ptrdiff_t stride_y, ptrdiff_t stride_x, ptrdiff_t height, ptrdiff_t width, int weight)
{
	ptrdiff_t y;

	for (y = 0; y < height; ++y) {
		dwtint_weight_line(band + y*stride_y, width, stride_x, weight);
	}
}

void dwtint_unweight_band(int *band, ptrdiff_t stride_y, ptrdiff_t stride_x, ptrdiff_t height, ptrdiff_t width, int weight)
{
	ptrdiff_t y;

	for (y = 0; y < height; ++y) {
		dwtint_unweight_line(band + y*stride_y, width, stride_x, weight);
	}
}

/* process 8x8 block using multi-scale transform */
void dwtint_encode_block(int *data, ptrdiff_t stride_y[3], ptrdiff_t stride_x[3], ptrdiff_t height[3], ptrdiff_t width[3], int *buff_y[3], int *buff_x[3], ptrdiff_t y, ptrdiff_t x)
{
	ptrdiff_t y_, x_;

	/* j = 0 */
	for (y_ = y/2-1; y_ < y/2-1+4; ++y_) {
		for (x_ = x/2-1; x_ < x/2-1+4; ++x_) {
			dwtint_encode_quad(data, height[0], width[0], stride_y[0], stride_x[0], buff_y[0], buff_x[0], y_, x_);
		}
	}
	/* j = 1 */
	for (y_ = y/4-1; y_ < y/4-1+2; ++y_) {
		for (x_ = x/4-1; x_ < x/4-1+2; ++x_) {
			dwtint_encode_quad(data, height[1], width[1], stride_y[1], stride_x[1], buff_y[1], buff_x[1], y_, x_);
		}
	}
	/* j = 2 */
	for (y_ = y/8-1; y_ < y/8-1+1; ++y_) {
		for (x_ = x/8-1; x_ < x/8-1+1; ++x_) {
			dwtint_encode_quad(data, height[2], width[2], stride_y[2], stride_x[2], buff_y[2], buff_x[2], y_, x_);
		}
	}
}

/* process strip using multi-scale transform */
void dwtint_encode_strip(int *data, ptrdiff_t stride_y[3], ptrdiff_t stride_x[3], ptrdiff_t height[3], ptrdiff_t width[3], int *buff_y[3], int *buff_x[3], ptrdiff_t y)
{
	ptrdiff_t y_, x_;

	/* j = 0 */
	for (y_ = y/2-1; y_ < y/2-1+4; ++y_) {
		for (x_ = 0; x_ < width[0]+2; ++x_) {
			dwtint_encode_quad(data, height[0], width[0], stride_y[0], stride_x[0], buff_y[0], buff_x[0], y_, x_);
		}
	}
	/* j = 1 */
	for (y_ = y/4-1; y_ < y/4-1+2; ++y_) {
		for (x_ = 0; x_ < width[1]+2; ++x_) {
			dwtint_encode_quad(data, height[1], width[1], stride_y[1], stride_x[1], buff_y[1], buff_x[1], y_, x_);
		}
	}
	/* j = 2 */
	for (y_ = y/8-1; y_ < y/8-1+1; ++y_) {
		for (x_ = 0; x_ < width[2]+2; ++x_) {
			dwtint_encode_quad(data, height[2], width[2], stride_y[2], stride_x[2], buff_y[2], buff_x[2], y_, x_);
		}
	}
}

int dwtint_encode(struct frame *frame)
{
	int j;
	ptrdiff_t height, width;
	int *data;
#if (CONFIG_DWT_MS_MODE == 1) || (CONFIG_DWT_MS_MODE == 2)
	int *buff_x_[3], *buff_y_[3];
	ptrdiff_t height_[3], width_[3];
	ptrdiff_t stride_y_[3], stride_x_[3];
	ptrdiff_t y;
#endif
#if (CONFIG_DWT_MS_MODE == 2)
	ptrdiff_t x;
#endif

	assert( frame );

	height = (ptrdiff_t) ceil_multiple8(frame->height);
	width  = (ptrdiff_t) ceil_multiple8(frame->width);

	assert( is_multiple8(width) && is_multiple8(height) );

	data = frame->data;

	assert( data );

	/* (2.2) forward two-dimensional transform */

#if (CONFIG_DWT_MS_MODE == 0)
	/* for each level */
	for (j = 0; j < 3; ++j) {
		/* number of elements for input */
		ptrdiff_t height_j = height >> j, width_j = width >> j;

		/* stride of input data (for level j) */
		ptrdiff_t stride_y = width << j, stride_x = 1 << j;

		dwtint_encode_band(data, stride_y, stride_x, height_j, width_j);
	}
#endif
#if (CONFIG_DWT_MS_MODE == 1)
	for (j = 0; j < 3; ++j) {
		height_[j] = (height >> j) >> 1;
		width_ [j] = (width  >> j) >> 1;

		stride_y_[j] = width << j;
		stride_x_[j] =     1 << j;

		buff_y_[j] = malloc( (size_t) (2 * height_[j] + (32 >> j) - 2) * 5 * sizeof(int) );
		buff_x_[j] = malloc( (size_t) (2 * width_ [j] + (32 >> j) - 2) * 5 * sizeof(int) );
	}

	for (y = 0; y < height+24; y += 8) {
		dwtint_encode_strip(data, stride_y_, stride_x_, height_, width_, buff_y_, buff_x_, y);
	}

	for (j = 0; j < 3; ++j) {
		free(buff_y_[j]);
		free(buff_x_[j]);
	}
#endif
#if (CONFIG_DWT_MS_MODE == 2)
	for (j = 0; j < 3; ++j) {
		height_[j] = (height >> j) >> 1;
		width_ [j] = (width  >> j) >> 1;

		stride_y_[j] = width << j;
		stride_x_[j] =     1 << j;

		buff_y_[j] = malloc( (size_t) (2 * height_[j] + (32 >> j) - 2) * 5 * sizeof(int) );
		buff_x_[j] = malloc( (size_t) (2 * width_ [j] + (32 >> j) - 2) * 5 * sizeof(int) );

		if (NULL == buff_y_[j] || NULL == buff_x_[j]) {
			return RET_FAILURE_MEMORY_ALLOCATION;
		}
	}

	for (y = 0; y < height+24; y += 8) {
		for (x = 0; x < width+24; x += 8) {
			dwtint_encode_block(data, stride_y_, stride_x_, height_, width_, buff_y_, buff_x_, y, x);
		}
	}

	for (j = 0; j < 3; ++j) {
		free(buff_y_[j]);
		free(buff_x_[j]);
	}
#endif

	/* (2.3) apply Subband Weights */

	for (j = 1; j < 4; ++j) {
		ptrdiff_t height_j = height >> j, width_j = width >> j;

		ptrdiff_t stride_y = width << j, stride_x = 1 << j;

		int *band_ll = data +          0 +          0;
		int *band_hl = data +          0 + stride_x/2;
		int *band_lh = data + stride_y/2 +          0;
		int *band_hh = data + stride_y/2 + stride_x/2;

		dwtint_weight_band(band_hl, stride_y, stride_x, height_j, width_j, j); /* HL */
		dwtint_weight_band(band_lh, stride_y, stride_x, height_j, width_j, j); /* LH */
		dwtint_weight_band(band_hh, stride_y, stride_x, height_j, width_j, j-1); /* HH */

		if (j < 3)
			continue;

		dwtint_weight_band(band_ll, stride_y, stride_x, height_j, width_j, j); /* LL */
	}

	return RET_SUCCESS;
}

int dwtint_decode(struct frame *frame)
{
	int j;
	ptrdiff_t height, width;
	int *data;

	assert( frame );

	height = (ptrdiff_t) ceil_multiple8(frame->height);
	width  = (ptrdiff_t) ceil_multiple8(frame->width);

	assert( is_multiple8(width) && is_multiple8(height) );

	data = frame->data;

	assert( data );

	/* undo Subband Weights */

	for (j = 1; j < 4; ++j) {
		ptrdiff_t height_j = height >> j, width_j = width >> j;

		ptrdiff_t stride_y = width << j, stride_x = 1 << j;

		int *band_ll = data +          0 +          0;
		int *band_hl = data +          0 + stride_x/2;
		int *band_lh = data + stride_y/2 +          0;
		int *band_hh = data + stride_y/2 + stride_x/2;

		dwtint_unweight_band(band_hl, stride_y, stride_x, height_j, width_j, j); /* HL */
		dwtint_unweight_band(band_lh, stride_y, stride_x, height_j, width_j, j); /* LH */
		dwtint_unweight_band(band_hh, stride_y, stride_x, height_j, width_j, j-1); /* HH */

		if (j < 3)
			continue;

		dwtint_unweight_band(band_ll, stride_y, stride_x, height_j, width_j, j); /* LL */
	}

	/* inverse two-dimensional transform */

	for (j = 2; j >= 0; --j) {
		ptrdiff_t height_j = height >> j, width_j = width >> j;

		ptrdiff_t stride_y = width << j, stride_x = 1 << j;

		dwtint_decode_band(data, stride_y, stride_x, height_j, width_j);
	}

	return RET_SUCCESS;
}
