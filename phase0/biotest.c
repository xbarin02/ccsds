#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "common.h"
#include "bio.h"

int main(int argc, char *argv[])
{
	size_t buffer_size = 4096;
	/* N-bit prologue */
	size_t prologue = 7;
	void *ptr;
	struct bio bio;
	UINT32 x;
	UINT32 y;
	int err;

	ptr = malloc(buffer_size);

	if (ptr == NULL) {
		abort();
	}

	x = 42;
	y = 57;

	/* writer */

	bio_open(&bio, ptr, BIO_MODE_WRITE);

	err = bio_write_bits(&bio, x, prologue);

	if (err) {
		abort();
	}

	err = bio_write_bits(&bio, y, prologue);

	if (err) {
		abort();
	}

	bio_close(&bio);

	dprint (("ptr[0] = %lu\n", (UINT32)*(unsigned char *)ptr));

	/* reader */

	bio_open(&bio, ptr, BIO_MODE_READ);

	err = bio_read_bits(&bio, &x, prologue);

	if (err) {
		abort();
	}

	err = bio_read_bits(&bio, &y, prologue);

	if (err) {
		abort();
	}

	dprint (("x = %lu\n", x));
	dprint (("y = %lu\n", y));

	assert(x == 42);
	assert(y == 57);

	bio_close(&bio);

	free(ptr);

	return 0;
}
