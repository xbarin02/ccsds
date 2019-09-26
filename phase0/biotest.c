#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "common.h"
#include "bio.h"

int main()
{
	size_t buffer_size = 4096;
	void *ptr;
	struct bio bio;
	UINT32 x, y, z;
	int err;

	ptr = malloc(buffer_size);

	if (ptr == NULL) {
		abort();
	}

	x = 42;
	y = 57;
	z = 3238002945UL;

	/* writer */

	bio_open(&bio, ptr, BIO_MODE_WRITE);

	err = bio_write_bits(&bio, x, 7);

	if (err) {
		abort();
	}

	err = bio_write_bits(&bio, y, 7);

	if (err) {
		abort();
	}

	err = bio_write_bits(&bio, z, 32);

	if (err) {
		abort();
	}

	bio_close(&bio);

	dprint (("ptr[0] = %lu\n", (UINT32)*(unsigned char *)ptr));

	/* reader */

	x = 0;
	y = 0;
	z = 0;

	bio_open(&bio, ptr, BIO_MODE_READ);

	err = bio_read_bits(&bio, &x, 7);

	if (err) {
		abort();
	}

	err = bio_read_bits(&bio, &y, 7);

	if (err) {
		abort();
	}

	err = bio_read_bits(&bio, &z, 32);

	if (err) {
		abort();
	}

	dprint (("x = %lu\n", x));
	dprint (("y = %lu\n", y));
	dprint (("z = %lu\n", z));

	assert(x == 42);
	assert(y == 57);
	assert(z == 3238002945UL);

	bio_close(&bio);

	free(ptr);

	return 0;
}
