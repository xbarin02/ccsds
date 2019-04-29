#include "bio.h"
#include "common.h"

#include <assert.h>

int bio_write_int(struct bio *bio, int i)
{
	int *intptr;

	assert(bio);

	if (bio->ptr == NULL)
		return -1;

	intptr = (int *) bio->ptr;

	*intptr = i;

	bio->ptr += sizeof(int);

	return 0;
}

int bio_read_int(struct bio *bio, int *i)
{
	int *intptr;

	assert(bio);

	if (bio->ptr == NULL)
		return -1;

	intptr = (int *) bio->ptr;

	assert(i);

	*i = *intptr;

	bio->ptr += sizeof(int);

	return 0;
}
