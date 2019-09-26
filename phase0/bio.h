/**
 * \file bio.h
 * \brief Bit input/output routines
 */
#ifndef BIO_H_
#define BIO_H_

#include "common.h"

enum {
	BIO_MODE_READ,
	BIO_MODE_WRITE
};

struct bio {
	int mode;

	unsigned char *ptr;

	unsigned char b; /* buffer */
	size_t c; /* counter */
};

int bio_open(struct bio *bio, unsigned char *ptr, int mode);
int bio_close(struct bio *bio);

/* write entire UINT32 */
int bio_write_int(struct bio *bio, UINT32 i);
/* read entire UINT32 */
int bio_read_int(struct bio *bio, UINT32 *i);

/* write n least-significant bits in b */
int bio_write_bits(struct bio *bio, UINT32 b, size_t n);
/* read n least-significant bits into *b */
int bio_read_bits(struct bio *bio, UINT32 *b, size_t n);

int bio_read_dc_bits(struct bio *bio, UINT32 *b, size_t n);

/* write a single bit */
int bio_put_bit(struct bio *bio, unsigned char b);
/* read a single bit */
int bio_get_bit(struct bio *bio, unsigned char *b);

#endif /* BIO_H_ */
