#ifndef BIO_H_
#define BIO_H_

struct bio {
	unsigned char *ptr;
};

int bio_init(struct bio *bio, unsigned char *ptr);

int bio_write_int(struct bio *bio, int i);

int bio_read_int(struct bio *bio, int *i);

#endif /* BIO_H_ */
