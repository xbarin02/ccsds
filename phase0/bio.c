#include "bio.h"
#include "common.h"

#include <assert.h>
#include <limits.h>

int bio_open(struct bio *bio, unsigned char *ptr, int mode)
{
	assert(bio);

	bio->mode = mode;

	bio->ptr = ptr;

	switch (mode) {
		case BIO_MODE_READ:
			bio->c = CHAR_BIT;
			break;
		case BIO_MODE_WRITE:
			bio->b = 0;
			bio->c = 0;
			break;
	}

	return RET_SUCCESS;
}

int bio_write_int(struct bio *bio, int i)
{
	return bio_write(bio, (uint_t) i, sizeof(uint_t) * CHAR_BIT); /* FIXME int -> uint_t */
}

int bio_read_int(struct bio *bio, int *i)
{
	return bio_read(bio, (uint_t *) i, sizeof(uint_t) * CHAR_BIT); /* FIXME uint_t -> int */
}

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

int bio_flush_buffer(struct bio *bio)
{
	assert(bio);

	if (bio->ptr == NULL) {
		return RET_FAILURE_LOGIC_ERROR;
	}

	assert(CHAR_BIT == 8);

	*bio->ptr++ = lut_reverse_char[bio->b];

	return RET_SUCCESS;
}

int bio_reload_buffer(struct bio *bio)
{
	assert(bio);

	if (bio->ptr == NULL) {
		return RET_FAILURE_LOGIC_ERROR;
	}

	bio->b = *bio->ptr++;

	return RET_SUCCESS;
}

int bio_put_bit(struct bio *bio, unsigned char b)
{
	assert(bio);

	assert(bio->c < CHAR_BIT);

	bio->b = (unsigned char) ( (bio->b << 1) | (b & 1) );

	bio->c ++;

	if (bio->c == CHAR_BIT) {
		if (bio_flush_buffer(bio))
			return -1;

		bio->b = 0;

		bio->c = 0;
	}

	return RET_SUCCESS;
}

/* c' = CHAR_BIT - c */
int bio_get_bit(struct bio *bio, unsigned char *b)
{
	assert(bio);

	if (bio->c == CHAR_BIT) {
		if (bio_reload_buffer(bio))
			return -1;

		bio->c = 0;
	}

	assert(b);

	*b = bio->b & 1;
	bio->b >>= 1;

	bio->c ++;

	return RET_SUCCESS;
}

int bio_write(struct bio *bio, uint_t b, size_t n)
{
	size_t i;

	for (i = 0; i < n; ++i) {
		if (bio_put_bit(bio, (b >> i) & 1))
			return -1;
	}

	return RET_SUCCESS;
}

int bio_read(struct bio *bio, uint_t *bptr, size_t n)
{
	size_t i;

	uint_t b = 0;

	for (i = 0; i < n; ++i) {
		unsigned char a;

		if (bio_get_bit(bio, &a))
			return -1;

		b |= (uint_t)a << i;
	}

	assert(bptr);

	*bptr = b;

	return RET_SUCCESS;
}

int bio_close(struct bio *bio)
{
	assert(bio);

	if (bio->mode == BIO_MODE_WRITE && bio->c > 0) {
		bio_flush_buffer(bio);
	}

	return RET_SUCCESS;
}
