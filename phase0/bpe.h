#ifndef BPE_H_
#define BPE_H_

#include "common.h"
#include "frame.h"
#include "bio.h"

int bpe_encode(struct frame *frame, const struct parameters *parameters, struct bio *bio);

int bpe_decode(struct frame *frame, const struct parameters *parameters, struct bio *bio);

#endif /* BPE_H_ */
