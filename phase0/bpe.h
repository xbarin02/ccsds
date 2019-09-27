/**
 * \file bpe.h
 * \brief Bit-plane encoder and decoder
 */
#ifndef BPE_H_
#define BPE_H_

#include "common.h"
#include "frame.h"
#include "bio.h"

/* Segment Header */
struct segment_header {
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
	UINT32 SegByteLimit; /* The value of SegByteLimit must be an integer multiple of the word size specified by CodeWordLength */
	int DCStop;
	UINT32 BitPlaneStop;
	UINT32 StageStop;
	int UseFill;
	/* Segment Header Part 3 */
	UINT32 S;
	int OptDCSelect;
	int OptACSelect;
	/* Segment Header Part 4 */
	int DWTtype;
	int ExtendedPixelBitDepthFlag; /* Indicates an input pixel bit depth larger than 16. */
	int SignedPixels;
	UINT32 PixelBitDepth; /* Input pixel bit depth value encoded, mod 16 */
	UINT32 ImageWidth;
	int TransposeImg;
	UINT32 CodeWordLength;
	int CustomWtFlag;
	int weight[12];
};

struct bpe {
	/* the number of block in the segment,
	 * the S is given in struct parameters */
	size_t S;

	/* local copy of S blocks, the size is 64*S = 8*8*S 32-bit integers */
	INT32 *segment;

	size_t s; /* local block index */
	size_t block_index; /* global block index */
	size_t segment_index; /* global segment index */

	struct bio *bio;

	struct segment_header segment_header;

	struct frame *frame;

	/* array of S quantized DC coefficients */
	INT32 *quantized_dc;
	/* array of S mapped quantized DC coefficients */
	UINT32 *mapped_quantized_dc;
};

size_t BitShift(const struct bpe *bpe, int subband);

int bpe_init(struct bpe *bpe, const struct parameters *parameters, struct bio *bio, struct frame *frame);
int bpe_realloc_segment(struct bpe *bpe, size_t S);
int bpe_realloc_frame_width(struct bpe *bpe);

int bpe_destroy(struct bpe *bpe, struct parameters *parameters);

/* helper function (to be removed in future) */
size_t get_total_no_blocks(struct frame *frame);

int bpe_encode(struct frame *frame, const struct parameters *parameters, struct bio *bio);

int bpe_decode(struct frame *frame, struct parameters *parameters, struct bio *bio);

size_t get_maximum_stream_size(struct frame *frame);

#endif /* BPE_H_ */
