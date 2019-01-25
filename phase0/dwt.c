#include "config.h"
#include "dwt.h"
#include "utils.h"

#include <stddef.h>
#include <assert.h>
#include <stdlib.h>

int dwtint_encode_line(int *line, ptrdiff_t size, ptrdiff_t stride)
{
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
}

/*
 * The CCSDS standard states that pixel bit depth shall not exceed the limit
 * of 28 bits for the signed pixel type (sign bit + 27 bit magnitude).
 *
 * To support 28 bit pixels, one would need 32 (28 + 6 due to DWT) bits for
 * coefficients, 8 fractional bits for lifting scheme, and 23 < n <= 52 bits
 * for lifting constants, giving a total of 40 + n > 64 bits. Thus one would
 * need 64x64 -> 128 multiplication. Another possibility is to multiply 64-bit
 * lifting coefficients by constants in double-precision floating-point format
 * (single-precision format is not sufficient).
 *
 * The Open122 library supports 16-bit unsigned pixels.
 *
 * To support 16-bit input, one need 22 (16 + 6) for DWT coefficients,
 * at least 6 fractional bits for lifting coefficients, and at least 22
 * fractional bits for lifting constants. Thus 32x32 -> 64 multiplication
 * should be sufficient. Another possibility is to multiply the coefficients
 * by constants in single-precision (or double-) format. Yet another
 * possibility is to carry out the lifting scheme completelly in floats.
 *
 * Note that minimum long int size per the standard is 32 bits. This is
 * de facto standard on 32-bit machines (int and long int are 32-bit numbers).
 * Therefore no 32x32 -> 64 multiplication is available here. For these
 * reasons, the 32-bit integer for coefficients and 32-bit float for lifting
 * constants seems to be quite portable solution. Another portable solution is
 * to compute the lifting scheme completelly in 32-bit floats.
 */

/*
 * Lifting constants for the CDF 9/7 wavelet
 *
 * The CCSDS standard defines low-pass filter coefficients for the CDF 9/7 wavelet.
 * These cofficients were factored into the lifting scheme using the equations in
 * Daubechies, I. & Sweldens, W. The Journal of Fourier Analysis and Applications
 * (1998) 4: 247. DOI: 10.1007/BF02476026. Note that the paper also provides similar
 * lifting coefficients for the CDF 9/7 wavelet, but these differ in some rounding
 * errors.
 */
#define alpha -1.58613434201888022056773162788538f
#define beta  -0.05298011857604780601431779000503f
#define gamma +0.88291107549260031282806293551600f
#define delta +0.44350685204939829327158029348930f
#define zeta  +1.14960439885900000000000000000000f
#define rcp_zeta \
              +0.86986445162572041243487498011333f
#define sqr_zeta \
              +1.32159027387596276050188100000000f
#define rcp_sqr_zeta \
              +0.75666416420211528747583823019750f

static void dwtfloat_encode_core(float data[2], float buff[4], const int lever[4])
{
	const float w0 = +delta;
	const float w1 = +gamma;
	const float w2 = +beta;
	const float w3 = +alpha;

	float l0, l1, l2, l3;
	float c0, c1, c2, c3;
	float r0, r1, r2, r3;
	float x0, x1;
	float y0, y1;

	l0 = buff[0];
	l1 = buff[1];
	l2 = buff[2];
	l3 = buff[3];

	x0 = data[0];
	x1 = data[1];

	c0 = l1;
	c1 = l2;
	c2 = l3;
	c3 = x0;

	r3 = x1;
	r2 = c3 + w3 * ( (lever[3] < 0 ? r3 : l3) + (lever[3] > 0 ? l3 : r3) );
	r1 = c2 + w2 * ( (lever[2] < 0 ? r2 : l2) + (lever[2] > 0 ? l2 : r2) );
	r0 = c1 + w1 * ( (lever[1] < 0 ? r1 : l1) + (lever[1] > 0 ? l1 : r1) );
	y0 = c0 + w0 * ( (lever[0] < 0 ? r0 : l0) + (lever[0] > 0 ? l0 : r0) );
	y1 = r0;

	l0 = r0;
	l1 = r1;
	l2 = r2;
	l3 = r3;

	data[0] = y0;
	data[1] = y1;

	buff[0] = l0;
	buff[1] = l1;
	buff[2] = l2;
	buff[3] = l3;
}

static void dwtfloat_decode_core(float data[2], float buff[4], const int lever[4])
{
	const float w0 = -alpha;
	const float w1 = -beta;
	const float w2 = -gamma;
	const float w3 = -delta;

	float l0, l1, l2, l3;
	float c0, c1, c2, c3;
	float r0, r1, r2, r3;
	float x0, x1;
	float y0, y1;

	l0 = buff[0];
	l1 = buff[1];
	l2 = buff[2];
	l3 = buff[3];

	x0 = data[0];
	x1 = data[1];

	c0 = l1;
	c1 = l2;
	c2 = l3;
	c3 = x0;

	r3 = x1;
	r2 = c3 + w3 * ( (lever[3] < 0 ? r3 : l3) + (lever[3] > 0 ? l3 : r3) );
	r1 = c2 + w2 * ( (lever[2] < 0 ? r2 : l2) + (lever[2] > 0 ? l2 : r2) );
	r0 = c1 + w1 * ( (lever[1] < 0 ? r1 : l1) + (lever[1] > 0 ? l1 : r1) );
	y0 = c0 + w0 * ( (lever[0] < 0 ? r0 : l0) + (lever[0] > 0 ? l0 : r0) );
	y1 = r0;

	l0 = r0;
	l1 = r1;
	l2 = r2;
	l3 = r3;

	data[0] = y0;
	data[1] = y1;

	buff[0] = l0;
	buff[1] = l1;
	buff[2] = l2;
	buff[3] = l3;
}

/*
 * Compute a part of one-dimensional wavelet transform.
 * The n0 and n1 define coordinates of the part to be computed.
 * The n0 is the initial coordinate and n1 is the smallest coordinate behind the signal segment.
 * Valid coordinates are in range [0, N+2) with N = size/2 (the signal length must be even).
 */
void dwtfloat_encode_line_segment(int *line, ptrdiff_t size, ptrdiff_t stride, float *buff, ptrdiff_t n0, ptrdiff_t n1)
{
	ptrdiff_t n, N;
	float data[2];

	assert( size > 0 && is_even(size) );

	N = size / 2;

#	define c(n) line[stride*(2*(n)+0)]
#	define d(n) line[stride*(2*(n)+1)]

	n = n0;

	/* prologue */
	for (; n < n1 && n < 1; n++) {
		int lever[4] = { 0, 0, 0, 0 };

		data[0] = 0.f;
		data[1] = (float) c(n);
		dwtfloat_encode_core(data, buff, lever);
	}
	for (; n < n1 && n < 2; n++) {
		int lever[4] = { 0, 0, -1, 0 };

		data[0] = (float) d(n-1);
		data[1] = (float) c(n);
		dwtfloat_encode_core(data, buff, lever);
	}
	for (; n < n1 && n < 3; n++) {
		int lever[4] = { -1, 0, 0, 0 };

		data[0] = (float) d(n-1);
		data[1] = (float) c(n);
		dwtfloat_encode_core(data, buff, lever);
		c(n-2) = roundf_( data[0] * (    +zeta) );
		d(n-2) = roundf_( data[1] * (-rcp_zeta) );
	}
	/* regular */
	for (; n < n1 && n < N; n++) {
		int lever[4] = { 0, 0, 0, 0 };

		data[0] = (float) d(n-1);
		data[1] = (float) c(n);
		dwtfloat_encode_core(data, buff, lever);
		c(n-2) = roundf_( data[0] * (    +zeta) );
		d(n-2) = roundf_( data[1] * (-rcp_zeta) );
	}
	/* epilogue */
	for (; n < n1 && n == N; n++) {
		int lever[4] = { 0, 0, 0, +1 };

		data[0] = (float) d(n-1);
		data[1] = 0.f;
		dwtfloat_encode_core(data, buff, lever);
		c(n-2) = roundf_( data[0] * (    +zeta) );
		d(n-2) = roundf_( data[1] * (-rcp_zeta) );
	}
	for (; n < n1 && n == N+1; n++) {
		int lever[4] = { 0, +1, 0, 0 };

		data[0] = 0.f;
		data[1] = 0.f;
		dwtfloat_encode_core(data, buff, lever);
		c(n-2) = roundf_( data[0] * (    +zeta) );
		d(n-2) = roundf_( data[1] * (-rcp_zeta) );
	}

#undef c
#undef d
}

/*
 * 17x17 image => 24x24 image on input of 1st level
 *
 * 12x12 image on input of 2nd level
 *
 * 6x6 image on input of 3rd level
 *
 * 6 sample input signal:
 *
 * x(0) x(1) x(2) x(3) x(4) x(size-1)
 * c(0) d(0) c(1) d(1) c(2) d(N-1)
 */

static void encode_adjust_levers(int lever[4], ptrdiff_t n, ptrdiff_t N)
{
	lever[2] = n ==   1 ? -1 : 0;
	lever[0] = n ==   2 ? -1 : 0;
	lever[3] = n == N   ? +1 : 0;
	lever[1] = n == N+1 ? +1 : 0;
}

static void decode_adjust_levers(int lever[4], ptrdiff_t n, ptrdiff_t N)
{
	lever[3] = n ==   0 ? -1 : 0;
	lever[1] = n ==   1 ? -1 : 0;
	lever[2] = n == N   ? +1 : 0;
	lever[0] = n == N+1 ? +1 : 0;
}

/*
 * Predicate: Is a signal defined at the position 'n'?
 *
 * Note that a signal is defined on [0; N).
 */
#define signal_defined(n, N) ( (n) >= 0 && (n) < (N) )

/*
 * mirror symmetric signal extension
 */
#define signal_mirror(n, N) ( (n) < 0 ? -(n) : ( (n) >= (N) ? (2*((N)-1)-(n)) : (n) ) )

/*
 * consume line[stride*(k-1)] and line[stride*(k)]
 */
void dwtfloat_encode_step(int *line, ptrdiff_t N, ptrdiff_t stride, float *buff, ptrdiff_t n)
{
	float data[2];
	int lever[4] = { 0, 0, 0, 0 };

	encode_adjust_levers(lever, n, N);

#	define c(n) line[stride*(2*(n)+0)]
#	define d(n) line[stride*(2*(n)+1)]

	data[0] = signal_defined(n-1, N) ? (float) d(n-1) : 0;
	data[1] = signal_defined(n-0, N) ? (float) c(n-0) : 0;

	dwtfloat_encode_core(data, buff, lever);

	if (signal_defined(n-2, N)) {
		c(n-2) = roundf_( data[0] * (    +zeta) );
		d(n-2) = roundf_( data[1] * (-rcp_zeta) );
	}

#	undef c
#	undef d
}

/* transpose 2x2 matrix */
static void transpose(float core[4])
{
	float t = core[1];
	core[1] = core[2];
	core[2] = t;
}

/*static*/ void dwtfloat_encode_core2(float core[4], float *buff_y, float *buff_x, int lever[2][4])
{
	/* horizontal filtering */
	dwtfloat_encode_core(&core[0], buff_y + 4*(0), lever[1]);
	dwtfloat_encode_core(&core[2], buff_y + 4*(1), lever[1]);
	transpose(core);
	/* vertical filtering */
	dwtfloat_encode_core(&core[0], buff_x + 4*(0), lever[0]);
	dwtfloat_encode_core(&core[2], buff_x + 4*(1), lever[0]);
	transpose(core);
}

/*
 * encode 2x2 coefficients
 */
void dwtfloat_encode_quad(int *data, ptrdiff_t N_y, ptrdiff_t N_x, ptrdiff_t stride_y, ptrdiff_t stride_x, float *buff_y, float *buff_x, ptrdiff_t n_y, ptrdiff_t n_x)
{
	/* vertical lever at [0], horizontal at [1] */
	int lever[2][4];
	/* order on input: 0=HH, 1=LH, 2=HH, 3=LL */
	float core[4];

	/* we cannot access buff_x[] and buff_y[] at negative indices */
	if ( n_y < 0 || n_x < 0 )
		return;

	encode_adjust_levers(lever[0], n_y, N_y);
	encode_adjust_levers(lever[1], n_x, N_x);

#	define cc(n_y, n_x) data[ stride_y*(2*(n_y)+0) + stride_x*(2*(n_x)+0) ] /* LL */
#	define dc(n_y, n_x) data[ stride_y*(2*(n_y)+0) + stride_x*(2*(n_x)+1) ] /* HL */
#	define cd(n_y, n_x) data[ stride_y*(2*(n_y)+1) + stride_x*(2*(n_x)+0) ] /* LH */
#	define dd(n_y, n_x) data[ stride_y*(2*(n_y)+1) + stride_x*(2*(n_x)+1) ] /* HH */

	core[0] = signal_defined(n_y-1, N_y) && signal_defined(n_x-1, N_x) ? (float) dd(n_y-1, n_x-1) : 0; /* HH */
	core[1] = signal_defined(n_y-1, N_y) && signal_defined(n_x-0, N_x) ? (float) cd(n_y-1, n_x-0) : 0; /* LH */
	core[2] = signal_defined(n_y-0, N_y) && signal_defined(n_x-1, N_x) ? (float) dc(n_y-0, n_x-1) : 0; /* HL */
	core[3] = signal_defined(n_y-0, N_y) && signal_defined(n_x-0, N_x) ? (float) cc(n_y-0, n_x-0) : 0; /* LL */

	dwtfloat_encode_core2(core, buff_y + 4*(2*n_y+0), buff_x + 4*(2*n_x+0), lever);

	if (signal_defined(n_y-2, N_y) && signal_defined(n_x-2, N_x)) {
		cc(n_y-2, n_x-2) = roundf_( core[0] * sqr_zeta     ); /* LL */
		dc(n_y-2, n_x-2) = roundf_( core[1] * -1           ); /* HL */
		cd(n_y-2, n_x-2) = roundf_( core[2] * -1           ); /* LH */
		dd(n_y-2, n_x-2) = roundf_( core[3] * rcp_sqr_zeta ); /* HH */
	}

#	undef cc
#	undef dc
#	undef cd
#	undef dd
}

/*static*/ void dwtfloat_decode_core2(float core[4], float *buff_y, float *buff_x, int lever[2][4])
{
	/* horizontal filtering */
	dwtfloat_decode_core(&core[0], buff_y + 4*(0), lever[1]);
	dwtfloat_decode_core(&core[2], buff_y + 4*(1), lever[1]);
	transpose(core);
	/* vertical filtering */
	dwtfloat_decode_core(&core[0], buff_x + 4*(0), lever[0]);
	dwtfloat_decode_core(&core[2], buff_x + 4*(1), lever[0]);
	transpose(core);
}

void dwtfloat_decode_quad(int *data, ptrdiff_t N_y, ptrdiff_t N_x, ptrdiff_t stride_y, ptrdiff_t stride_x, float *buff_y, float *buff_x, ptrdiff_t n_y, ptrdiff_t n_x)
{
	/* vertical lever at [0], horizontal at [1] */
	int lever[2][4];
	/* order on input: 0=LL, 1=HL, 2=LH, 3=HH */
	float core[4];

	decode_adjust_levers(lever[0], n_y, N_y);
	decode_adjust_levers(lever[1], n_x, N_x);

#	define cc(n_y, n_x) data[ stride_y*(2*(n_y)+0) + stride_x*(2*(n_x)+0) ] /* LL */
#	define dc(n_y, n_x) data[ stride_y*(2*(n_y)+0) + stride_x*(2*(n_x)+1) ] /* HL */
#	define cd(n_y, n_x) data[ stride_y*(2*(n_y)+1) + stride_x*(2*(n_x)+0) ] /* LH */
#	define dd(n_y, n_x) data[ stride_y*(2*(n_y)+1) + stride_x*(2*(n_x)+1) ] /* HH */

	if ( signal_defined(n_y-0, N_y) && signal_defined(n_x-0, N_x) ) {
		core[0] = (float) cc(n_y, n_x) * rcp_sqr_zeta; /* LL */
		core[1] = (float) dc(n_y, n_x) * -1;           /* HL */
		core[2] = (float) cd(n_y, n_x) * -1;           /* LH */
		core[3] = (float) dd(n_y, n_x) * sqr_zeta;     /* HH */
	} else {
		core[0] = 0;
		core[1] = 0;
		core[2] = 0;
		core[3] = 0;
	}

	dwtfloat_decode_core2(core, buff_y + 4*(2*n_y+0), buff_x + 4*(2*n_x+0), lever);

	if ( signal_defined(n_y-1, N_y) && signal_defined(n_x-1, N_x) )
		cc(n_y-1, n_x-1) = roundf_( core[3] ); /* LL */
	if ( signal_defined(n_y-1, N_y) && signal_defined(n_x-2, N_x) )
		dc(n_y-1, n_x-2) = roundf_( core[2] ); /* HL */
	if ( signal_defined(n_y-2, N_y) && signal_defined(n_x-1, N_x) )
		cd(n_y-2, n_x-1) = roundf_( core[1] ); /* LH */
	if ( signal_defined(n_y-2, N_y) && signal_defined(n_x-2, N_x) )
		dd(n_y-2, n_x-2) = roundf_( core[0] ); /* HH */

#	undef cc
#	undef dc
#	undef cd
#	undef dd
}

int dwtfloat_encode_line(int *line, ptrdiff_t size, ptrdiff_t stride)
{
#if (CONFIG_DWT1_MODE == 2)
	ptrdiff_t N;
	float buff[4] = { .0f, .0f, .0f, .0f };

	assert( size > 0 && is_even(size) );

	N = size / 2;

	dwtfloat_encode_line_segment(line, size, stride, buff, 0, N+2);

	return RET_SUCCESS;
#endif
#if (CONFIG_DWT1_MODE == 1)
	float *line_;
	ptrdiff_t n, N;

	assert( size > 0 && is_even(size) );

	N = size / 2;

	line_ = malloc( (size_t) size * sizeof(float) );

	if (NULL == line_) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	assert( line );

	/* lifting */

#	define c(n) line_[2*(n)+0]
#	define d(n) line_[2*(n)+1]

	for (n = 0; n < N; ++n) {
		c(n) = (float) line[stride*(2*n+0)];
		d(n) = (float) line[stride*(2*n+1)];
	}

	/* alpha: predict D from C */
	for (n = 0; n < N-1; ++n) {
		d(n)   += alpha * (c(n) + c(n+1));
	}
		d(N-1) += alpha * (c(N-1) + c(N-1));

	/* beta: update C from D */
	for (n = 1; n < N; ++n) {
		c(n) += beta  * (d(n) + d(n-1));
	}
		c(0) += beta  * (d(0) + d(0));

	/* gamma: predict D from C */
	for (n = 0; n < N-1; ++n) {
		d(n)   += gamma * (c(n) + c(n+1));
	}
		d(N-1) += gamma  * (c(N-1) + c(N-1));

	/* delta: update C from D */
	for (n = 1; n < N; ++n) {
		c(n) += delta * (d(n) + d(n-1));
	}
		c(0) += delta * (d(0) + d(0));

	/* zeta: scaling */
	for (n = 0; n < N; ++n) {
		c(n) = c(n) * (  +zeta);
		d(n) = d(n) * (1/-zeta);
	}

	for (n = 0; n < N; ++n) {
		line[stride*(2*n+0)] = roundf_( c(n) );
		line[stride*(2*n+1)] = roundf_( d(n) );
	}

#	undef c
#	undef d

	free(line_);

	return RET_SUCCESS;
#endif
#if (CONFIG_DWT1_MODE == 0)
	int *line_;
	ptrdiff_t n, N;

	assert( size > 0 && is_even(size) );

	N = size / 2;

	line_ = malloc( (size_t) size * sizeof(int) );

	if (NULL == line_) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	assert( line );

	/* convolution (using float32) */

#	define c(n) line_[2*(n)+0]
#	define d(n) line_[2*(n)+1]
#	define x(n) line[ stride * signal_mirror((n), size) ]

	for (n = 0; n < N; ++n) {
		c(n) = (int) roundf_ (
			+0.037828455507f * (float) x(2*n+0-4)
			-0.023849465020f * (float) x(2*n+0-3)
			-0.110624404418f * (float) x(2*n+0-2)
			+0.377402855613f * (float) x(2*n+0-1)
			+0.852698679009f * (float) x(2*n+0+0)
			+0.377402855613f * (float) x(2*n+0+1)
			-0.110624404418f * (float) x(2*n+0+2)
			-0.023849465020f * (float) x(2*n+0+3)
			+0.037828455507f * (float) x(2*n+0+4)
		);

		d(n) = (int) roundf_ (
			-0.064538882629f * (float) x(2*n+1-3)
			+0.040689417609f * (float) x(2*n+1-2)
			+0.418092273222f * (float) x(2*n+1-1)
			-0.788485616406f * (float) x(2*n+1+0)
			+0.418092273222f * (float) x(2*n+1+1)
			+0.040689417609f * (float) x(2*n+1+2)
			-0.064538882629f * (float) x(2*n+1+3)
		);
	}

	/* keep interleaved */
	for (n = 0; n < N; ++n) {
		line[stride*(2*n+0)] = c(n);
		line[stride*(2*n+1)] = d(n);
	}

#	undef c
#	undef d
#	undef x

	free(line_);

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

int dwtfloat_decode_line(int *line, ptrdiff_t size, ptrdiff_t stride)
{
	int *line_;
	ptrdiff_t n, N;

	assert( size > 0 && is_even(size) );

	N = size / 2;

	line_ = malloc( (size_t) size * sizeof(float) );

	if (NULL == line_) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	assert( line );

	/* inverse lifting */

#	define c(n) ((float *)line_)[2*(n)+0]
#	define d(n) ((float *)line_)[2*(n)+1]

	for (n = 0; n < N; ++n) {
		c(n) = (float) line[stride*(2*n+0)];
		d(n) = (float) line[stride*(2*n+1)];
	}

	/* zeta: scaling */
	for (n = 0; n < N; ++n) {
		c(n) = c(n) * (1/+zeta);
		d(n) = d(n) * (  -zeta);
	}

	/* delta: update C from D */
	for (n = 1; n < N; ++n) {
		c(n) -= delta * (d(n) + d(n-1));
	}
		c(0) -= delta * (d(0) + d(0));

	/* gamma: predict D from C */
	for (n = 0; n < N-1; ++n) {
		d(n)   -= gamma * (c(n) + c(n+1));
	}
		d(N-1) -= gamma  * (c(N-1) + c(N-1));

	/* beta: update C from D */
	for (n = 1; n < N; ++n) {
		c(n) -= beta  * (d(n) + d(n-1));
	}
		c(0) -= beta  * (d(0) + d(0));

	/* alpha: predict D from C */
	for (n = 0; n < N-1; ++n) {
		d(n)   -= alpha * (c(n) + c(n+1));
	}
		d(N-1) -= alpha * (c(N-1) + c(N-1));

	for (n = 0; n < N; ++n) {
		line[stride*(2*n+0)] = roundf_( c(n) );
		line[stride*(2*n+1)] = roundf_( d(n) );
	}

#	undef c
#	undef d

	free(line_);

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

	return RET_SUCCESS;
}

int dwtfloat_encode_band(int *band, ptrdiff_t stride_y, ptrdiff_t stride_x, ptrdiff_t height, ptrdiff_t width)
{
	ptrdiff_t y, x;

#if (CONFIG_DWT2_MODE == 0)
	/* for each row */
	for (y = 0; y < height; ++y) {
		/* invoke one-dimensional transform */
		dwtfloat_encode_line(band + y*stride_y, width, stride_x);
	}
	/* for each column */
	for (x = 0; x < width; ++x) {
		/* invoke one-dimensional transform */
		dwtfloat_encode_line(band + x*stride_x, height, stride_y);
	}
#endif
#if (CONFIG_DWT2_MODE == 1)
	float *buff;

	buff = malloc( (size_t) width * 4 * sizeof(float) );

	if (NULL == buff) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	/* for each row */
	for (y = 0; y < height; y += 2) {
		/* invoke one-dimensional transform */
		dwtfloat_encode_line(band + (y+0)*stride_y, width, stride_x);
		dwtfloat_encode_line(band + (y+1)*stride_y, width, stride_x);

		for (x = 0; x < width; ++x) {
			dwtfloat_encode_step(band + x*stride_x, height/2, stride_y, buff + 4*x, y/2);
		}
	}

	for (; y < height+4; y += 2) {
		for (x = 0; x < width; ++x) {
			dwtfloat_encode_step(band + x*stride_x, height/2, stride_y, buff + 4*x, y/2);
		}
	}

	free(buff);
#endif
#if (CONFIG_DWT2_MODE == 2)
	float *buff_y, *buff_x;

	buff_y = malloc( (size_t) (height+4) * 4 * sizeof(float) );
	buff_x = malloc( (size_t) (width +4) * 4 * sizeof(float) );

	if (NULL == buff_y || NULL == buff_x) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	for (y = 0; y < height/2+2; ++y) {
		for (x = 0; x < width/2+2; ++x) {
			dwtfloat_encode_quad(band, height/2, width/2, stride_y, stride_x, buff_y, buff_x, y, x);
		}
	}

	free(buff_x);
	free(buff_y);
#endif

	return RET_SUCCESS;
}

int dwtfloat_decode_band(int *band, ptrdiff_t stride_y, ptrdiff_t stride_x, ptrdiff_t height, ptrdiff_t width)
{
	ptrdiff_t y, x;

#if (CONFIG_DWT2_MODE == 0) || (CONFIG_DWT2_MODE == 1)
	/* for each column */
	for (x = 0; x < width; ++x) {
		/* invoke one-dimensional transform */
		dwtfloat_decode_line(band + x*stride_x, height, stride_y);
	}
	/* for each row */
	for (y = 0; y < height; ++y) {
		/* invoke one-dimensional transform */
		dwtfloat_decode_line(band + y*stride_y, width, stride_x);
	}
#endif
#if (CONFIG_DWT2_MODE == 2)
	float *buff_y, *buff_x;

	buff_y = malloc( (size_t) (height+4) * 4 * sizeof(float) );
	buff_x = malloc( (size_t) (width +4) * 4 * sizeof(float) );

	if (NULL == buff_y || NULL == buff_x) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	for (y = 0; y < height/2+2; ++y) {
		for (x = 0; x < width/2+2; ++x) {
			dwtfloat_decode_quad(band, height/2, width/2, stride_y, stride_x, buff_y, buff_x, y, x);
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

int dwtint_encode(struct frame *frame)
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

	/* (2.2) forward two-dimensional transform */

	/* for each level */
	for (j = 0; j < 3; ++j) {
		/* number of elements for input */
		ptrdiff_t height_j = height >> j, width_j = width >> j;

		/* stride of input data (for level j) */
		ptrdiff_t stride_y = width << j, stride_x = 1 << j;

		dwtint_encode_band(data, stride_y, stride_x, height_j, width_j);
	}

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

/* process 8x8 block using multi-scale transform */
void dwtfloat_encode_block(int *data, ptrdiff_t stride_y[3], ptrdiff_t stride_x[3], ptrdiff_t height[3], ptrdiff_t width[3], float *buff_y[3], float *buff_x[3], ptrdiff_t y, ptrdiff_t x)
{
	ptrdiff_t y_, x_;

	/* j = 0 */
	for (y_ = y/2-1; y_ < y/2-1+4; ++y_) {
		for (x_ = x/2-1; x_ < x/2-1+4; ++x_) {
			dwtfloat_encode_quad(data, height[0], width[0], stride_y[0], stride_x[0], buff_y[0], buff_x[0], y_, x_);
		}
	}
	/* j = 1 */
	for (y_ = y/4-1; y_ < y/4-1+2; ++y_) {
		for (x_ = x/4-1; x_ < x/4-1+2; ++x_) {
			dwtfloat_encode_quad(data, height[1], width[1], stride_y[1], stride_x[1], buff_y[1], buff_x[1], y_, x_);
		}
	}
	/* j = 2 */
	for (y_ = y/8-1; y_ < y/8-1+1; ++y_) {
		for (x_ = x/8-1; x_ < x/8-1+1; ++x_) {
			dwtfloat_encode_quad(data, height[2], width[2], stride_y[2], stride_x[2], buff_y[2], buff_x[2], y_, x_);
		}
	}
}

/* process strip using multi-scale transform */
void dwtfloat_encode_strip(int *data, ptrdiff_t stride_y[3], ptrdiff_t stride_x[3], ptrdiff_t height[3], ptrdiff_t width[3], float *buff_y[3], float *buff_x[3], ptrdiff_t y)
{
	ptrdiff_t y_, x_;

	/* j = 0 */
	for (y_ = y/2-1; y_ < y/2-1+4; ++y_) {
		for (x_ = 0; x_ < width[0]+2; ++x_) {
			dwtfloat_encode_quad(data, height[0], width[0], stride_y[0], stride_x[0], buff_y[0], buff_x[0], y_, x_);
		}
	}
	/* j = 1 */
	for (y_ = y/4-1; y_ < y/4-1+2; ++y_) {
		for (x_ = 0; x_ < width[1]+2; ++x_) {
			dwtfloat_encode_quad(data, height[1], width[1], stride_y[1], stride_x[1], buff_y[1], buff_x[1], y_, x_);
		}
	}
	/* j = 2 */
	for (y_ = y/8-1; y_ < y/8-1+1; ++y_) {
		for (x_ = 0; x_ < width[2]+2; ++x_) {
			dwtfloat_encode_quad(data, height[2], width[2], stride_y[2], stride_x[2], buff_y[2], buff_x[2], y_, x_);
		}
	}
}

int dwtfloat_encode(struct frame *frame)
{
	int j;
	ptrdiff_t height, width;
	int *data;
#if (CONFIG_DWT_MS_MODE == 1) || (CONFIG_DWT_MS_MODE == 2)
	float *buff_x_[3], *buff_y_[3];
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

		dwtfloat_encode_band(data, stride_y, stride_x, height_j, width_j);
	}
#endif
#if (CONFIG_DWT_MS_MODE == 1)
	for (j = 0; j < 3; ++j) {
		height_[j] = (height >> j) >> 1;
		width_ [j] = (width  >> j) >> 1;

		stride_y_[j] = width << j;
		stride_x_[j] =     1 << j;

		buff_y_[j] = malloc( (size_t) (2 * height_[j] + (32 >> j) - 2) * 4 * sizeof(float) );
		buff_x_[j] = malloc( (size_t) (2 * width_ [j] + (32 >> j) - 2) * 4 * sizeof(float) );
	}

	for (y = 0; y < height+24; y += 8) {
		dwtfloat_encode_strip(data,  stride_y_, stride_x_, height_, width_, buff_y_, buff_x_, y);
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

		buff_y_[j] = malloc( (size_t) (2 * height_[j] + (32 >> j) - 2) * 4 * sizeof(float) );
		buff_x_[j] = malloc( (size_t) (2 * width_ [j] + (32 >> j) - 2) * 4 * sizeof(float) );
	}

	for (y = 0; y < height+24; y += 8) {
		for (x = 0; x < width+24; x += 8) {
			dwtfloat_encode_block(data, stride_y_, stride_x_, height_, width_, buff_y_, buff_x_, y, x);
		}
	}

	for (j = 0; j < 3; ++j) {
		free(buff_y_[j]);
		free(buff_x_[j]);
	}
#endif

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

int dwtfloat_decode(struct frame *frame)
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

	/* inverse two-dimensional transform */

	for (j = 2; j >= 0; --j) {
		ptrdiff_t height_j = height >> j, width_j = width >> j;

		ptrdiff_t stride_y = width << j, stride_x = 1 << j;

		dwtfloat_decode_band(data, stride_y, stride_x, height_j, width_j);
	}

	return RET_SUCCESS;
}

int dwt_encode(struct frame *frame, const struct parameters *parameters)
{
	assert( parameters );

	switch (parameters->DWTtype) {
		case 0:
			return dwtfloat_encode(frame);
		case 1:
			return dwtint_encode(frame);
		default:
			return RET_FAILURE_LOGIC_ERROR;
	}
}

int dwt_decode(struct frame *frame, const struct parameters *parameters)
{
	assert( parameters );

	switch (parameters->DWTtype) {
		case 0:
			return dwtfloat_decode(frame);
		case 1:
			return dwtint_decode(frame);
		default:
			return RET_FAILURE_LOGIC_ERROR;
	}
}
