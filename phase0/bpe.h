/**
 * \file bpe.h
 * \brief Bit-plane encoder and decoder
 */
#ifndef BPE_H_
#define BPE_H_

#include "common.h"
#include "frame.h"
#include "bio.h"

struct segment_info {
	/* Segment Header Part 1A */
	int StartImgFlag;
	int EndImgFlag;
	UINT32 SegmentCount;
	UINT32 BitDepthDC;
	UINT32 BitDepthAC;
	int Part2Flag;
	int Part3Flag;
	int Part4Flag;
	/* Segment Header Part 1B */
	UINT32 PadRows;
	/* Segment Header Part 2 */
	UINT32 SegByteLimit;
	int DCStop;
	UINT32 BitPlaneStop;
	UINT32 StageStop;
	int UseFill;
	/* Segment Header Part 3 */
	int OptDCSelect;
	int OptACSelect;
	/* Segment Header Part 4 */
	int ExtendedPixelBitDepthFlag;
	int SignedPixels;
	UINT32 PixelBitDepth;
	int TransposeImg;
	UINT32 CodeWordLength;
	int CustomWtFlag;
};

struct bpe {
	/* the number of block in the segment,
	 * the S is given in struct parameters */
	size_t S;

	/* local copy of S blocks, the size is 64*S = 8*8*S 32-bit integers */
	INT32 *segment;

	size_t block_index;

	struct bio *bio;

	struct segment_info segment_info;

	int DWTtype;

	size_t height; /**< \brief number of rows, range [17; infty) */
	size_t width;  /**< \brief number of columns, range [17; 1<<20] */

	int weight[12];
};

int bpe_init(struct bpe *bpe, const struct parameters *parameters, struct bio *bio);

int bpe_push_block(struct bpe *bpe, INT32 *data, size_t stride);
int bpe_pop_block(struct bpe *bpe, INT32 *data, size_t stride, size_t total_no_blocks);

int bpe_destroy(struct bpe *bpe);

/* helper function (to be removed in future) */
size_t get_total_no_blocks(struct frame *frame);

int bpe_encode(struct frame *frame, const struct parameters *parameters, struct bio *bio);

int bpe_decode(struct frame *frame, const struct parameters *parameters, struct bio *bio);

size_t get_maximum_stream_size(struct frame *frame);

#endif /* BPE_H_ */
