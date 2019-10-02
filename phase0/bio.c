#include "bio.h"
#include "common.h"

#include <assert.h>
#include <limits.h>

static void bio_reset_after_flush(struct bio *bio)
{
	assert(bio != NULL);

	bio->b = 0;
	bio->c = 0;
}

int bio_open(struct bio *bio, unsigned char *ptr, int mode)
{
	assert(bio != NULL);

	bio->mode = mode;

	bio->ptr = ptr;

	if (ptr == NULL) {
		return RET_FAILURE_LOGIC_ERROR;
	}

	switch (mode) {
		case BIO_MODE_READ:
			bio->c = CHAR_BIT;
			break;
		case BIO_MODE_WRITE:
			bio_reset_after_flush(bio);
			break;
	}

	return RET_SUCCESS;
}

int bio_write_int(struct bio *bio, UINT32 i)
{
	return bio_write_bits(bio, i, sizeof(UINT32) * CHAR_BIT);
}

int bio_read_int(struct bio *bio, UINT32 *i)
{
	return bio_read_bits(bio, i, sizeof(UINT32) * CHAR_BIT);
}

#if (CONFIG_BIO_REVERSE_BITS == 1)
static const unsigned char lut_reverse_char[256] = {
	0, 128, 64, 192, 32, 160, 96, 224, 16, 144, 80, 208, 48, 176, 112, 240,
	8, 136, 72, 200, 40, 168, 104, 232, 24, 152, 88, 216, 56, 184, 120, 248,
	4, 132, 68, 196, 36, 164, 100, 228, 20, 148, 84, 212, 52, 180, 116, 244,
	12, 140, 76, 204, 44, 172, 108, 236, 28, 156, 92, 220, 60, 188, 124, 252,
	2, 130, 66, 194, 34, 162, 98, 226, 18, 146, 82, 210, 50, 178, 114, 242,
	10, 138, 74, 202, 42, 170, 106, 234, 26, 154, 90, 218, 58, 186, 122, 250,
	6, 134, 70, 198, 38, 166, 102, 230, 22, 150, 86, 214, 54, 182, 118, 246,
	14, 142, 78, 206, 46, 174, 110, 238, 30, 158, 94, 222, 62, 190, 126, 254,
	1, 129, 65, 193, 33, 161, 97, 225, 17, 145, 81, 209, 49, 177, 113, 241,
	9, 137, 73, 201, 41, 169, 105, 233, 25, 153, 89, 217, 57, 185, 121, 249,
	5, 133, 69, 197, 37, 165, 101, 229, 21, 149, 85, 213, 53, 181, 117, 245,
	13, 141, 77, 205, 45, 173, 109, 237, 29, 157, 93, 221, 61, 189, 125, 253,
	3, 131, 67, 195, 35, 163, 99, 227, 19, 147, 83, 211, 51, 179, 115, 243,
	11, 139, 75, 203, 43, 171, 107, 235, 27, 155, 91, 219, 59, 187, 123, 251,
	7, 135, 71, 199, 39, 167, 103, 231, 23, 151, 87, 215, 55, 183, 119, 247,
	15, 143, 79, 207, 47, 175, 111, 239, 31, 159, 95, 223, 63, 191, 127, 255
};
#endif

int bio_flush_buffer(struct bio *bio)
{
	assert(bio);

	if (bio->ptr == NULL) {
		return RET_FAILURE_LOGIC_ERROR;
	}

	assert(CHAR_BIT == 8);

#if (CONFIG_BIO_REVERSE_BITS == 1)
	*bio->ptr++ = lut_reverse_char[bio->b];
#else
	*bio->ptr++ = bio->b;
#endif

	return RET_SUCCESS;
}

int bio_reload_buffer(struct bio *bio)
{
	assert(bio != NULL);

	if (bio->ptr == NULL) {
		return RET_FAILURE_LOGIC_ERROR;
	}

#if (CONFIG_BIO_REVERSE_BITS == 1)
	bio->b = lut_reverse_char[*bio->ptr++];
#else
	bio->b = *bio->ptr++;
#endif

	return RET_SUCCESS;
}

int bio_put_bit(struct bio *bio, unsigned char b)
{
	assert(bio != NULL);

	assert(bio->c < CHAR_BIT);

	/* do not trust the input, mask the LSB here */
#if 0
	bio->b = (unsigned char) ( (bio->b << 1) | (b & 1) );
#else
	bio->b |= (unsigned char)((b & 1) << bio->c);
#endif

	bio->c ++;

	if (bio->c == CHAR_BIT) {
		int err = bio_flush_buffer(bio);

		if (err) {
			return err;
		}

		bio_reset_after_flush(bio);
	}

	return RET_SUCCESS;
}

/* c' = CHAR_BIT - c */
int bio_get_bit(struct bio *bio, unsigned char *b)
{
	assert(bio != NULL);

	if (bio->c == CHAR_BIT) {
		int err = bio_reload_buffer(bio);

		if (err) {
			return err;
		}

		bio->c = 0;
	}

	assert(b);

	*b = bio->b & 1;

	bio->b >>= 1;

	bio->c ++;

	return RET_SUCCESS;
}

int bio_write_bits(struct bio *bio, UINT32 b, size_t n)
{
	size_t i;

	assert(n <= 32);

	for (i = 0; i < n; ++i) {
		/* masking the LSB omitted */
		int err = bio_put_bit(bio, (unsigned char)b);

		b >>= 1;

		if (err) {
			return err;
		}
	}

	return RET_SUCCESS;
}

int bio_read_bits(struct bio *bio, UINT32 *b, size_t n)
{
	size_t i;
	UINT32 word = 0;

	assert(n <= 32);

	for (i = 0; i < n; ++i) {
		unsigned char bit;

		int err = bio_get_bit(bio, &bit);

		if (err) {
			return err;
		}

#if 1
		word |= (UINT32)bit << i;
#else
		word <<= 1;
		word |= (UINT32)bit;
#endif
	}

	assert(b);

	*b = word;

	return RET_SUCCESS;
}

int bio_read_dc_bits(struct bio *bio, UINT32 *b, size_t n)
{
	size_t i;
	unsigned char bit;
	UINT32 word = 0;

	for (i = 0; i < n; ++i) {
		int err = bio_get_bit(bio, &bit);

		if (err) {
			return err;
		}

#if 1
		word |= (UINT32)bit << i;
#else
		word <<= 1;
		word |= (UINT32)bit;
#endif
	}

	for (; i < 32; ++i) {
		word |= (UINT32)bit << i;
	}

	assert(b);

	*b = word;

	return RET_SUCCESS;
}

int bio_close(struct bio *bio)
{
	assert(bio != NULL);

	if (bio->mode == BIO_MODE_WRITE && bio->c > 0) {
		bio_flush_buffer(bio);
	}

	return RET_SUCCESS;
}

int bio_write_unary(struct bio *bio, UINT32 N)
{
	UINT32 n;
	int err;

	for (n = 0; n < N; ++n) {
		int err = bio_put_bit(bio, 0);

		if (err) {
			return err;
		}
	}

	err = bio_put_bit(bio, 1);

	if (err) {
		return err;
	}

	return RET_SUCCESS;
}

int bio_read_unary(struct bio *bio, UINT32 *N)
{
	UINT32 Q = 0;

	do {
		unsigned char b;
		int err = bio_get_bit(bio, &b);

		if (err) {
			return err;
		}

		if (b == 0)
			Q++;
		else
			break;
	} while (1);

	assert(N != NULL);

	*N = Q;

	return RET_SUCCESS;
}

int bio_write_gr_1st_part(struct bio *bio, size_t k, UINT32 N)
{
	UINT32 Q = N >> k;

	return bio_write_unary(bio, Q);
}

int bio_write_gr_2nd_part(struct bio *bio, size_t k, UINT32 N)
{
	assert(k <= 32);

	return bio_write_bits(bio, N, k);
}

int bio_read_gr_1st_part(struct bio *bio, size_t k, UINT32 *N)
{
	UINT32 Q;
	int err = bio_read_unary(bio, &Q);

	if (err) {
		return err;
	}

	assert(N != NULL);

	*N = Q << k;

	return RET_SUCCESS;
}

int bio_read_gr_2nd_part(struct bio *bio, size_t k, UINT32 *N)
{
	UINT32 w;
	int err;

	assert(k <= 32);

	err = bio_read_bits(bio, &w, k);

	if (err) {
		return err;
	}

	assert(N != NULL);

	*N |= w;

	return RET_SUCCESS;
}

size_t bio_sizeof_gr(size_t k, UINT32 N)
{
	size_t size;
	UINT32 Q = N >> k;

	size = Q + 1;

	size += k;

	return size;
}
