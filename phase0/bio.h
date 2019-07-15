#ifndef BIO_H_
#define BIO_H_

#include "common.h"

struct bio {
	unsigned char *ptr;

	unsigned char b; /* buffer */
	size_t c; /* counter */
};

int bio_init(struct bio *bio, unsigned char *ptr);
int bio_close(struct bio *bio);

int bio_write_int(struct bio *bio, int i);
int bio_read_int(struct bio *bio, int *i);

/* write n least-significant bits in b */
int bio_write(struct bio *bio, uint_t b, size_t n);
int bio_read(struct bio *bio, uint_t *bptr, size_t n);

int bio_put_bit(struct bio *bio, unsigned char b);
int bio_get_bit(struct bio *bio, unsigned char *b);

#endif /* BIO_H_ */
