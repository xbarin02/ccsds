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
			bio->c = 0;
			break;
	}

	bio->b = 0;

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

int bio_flush_buffer(struct bio *bio)
{
	assert(bio);

	if (bio->ptr == NULL) {
		return RET_FAILURE_LOGIC_ERROR;
	}

	*bio->ptr++ = bio->b;

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

	bio->b |= (unsigned char) (b << bio->c);

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

	*b = (bio->b >> bio->c) & 1;

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
