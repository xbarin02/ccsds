#include "bpe.h"
#include "common.h"
#include <assert.h>
#include <stdlib.h>

#define DEBUG_ENCODE_BLOCKS 1

#define BLOCK_SIZE (8 * 8)

/* Mn = 2^n - 1 */
#define M2 3
#define M3 7
#define M4 15
#define M5 31
#define M8 255
#define M20 1048575
#define M27 134217727

struct block {
	int *data;
	size_t stride;
};

static INT32 int32_abs(INT32 j)
{
#if (USHRT_MAX == UINT32_MAX_)
	return (INT32) abs((int) j);
#elif (UINT_MAX == UINT32_MAX_)
	return (INT32) abs((int) j);
#elif (ULONG_MAX == UINT32_MAX_)
	return (INT32) labs((long int) j);
#else
#	error "Not implemented"
#endif
}

static UINT32 uint32_abs(INT32 j)
{
	if (j == INT32_MIN_) {
		return (UINT32)INT32_MAX_ + 1;
	}

	return (UINT32) int32_abs(j);
}

/* Round up to the next highest power of 2 */
static UINT32 uint32_ceil_pow2(UINT32 v)
{
	assert( v != 0 );

	v--;

	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;

	v++;

	return v;
}

static size_t uint32_floor_log2(UINT32 n)
{
	size_t r = 0;

	assert( n != 0 );

	while (n >>= 1) {
		r++;
	}

	return r;
}

static size_t uint32_ceil_log2(UINT32 n)
{
	return uint32_floor_log2(uint32_ceil_pow2(n));
}

/*
 * The number of bits needed to represent cm in 2's-complement representation.
 * Eq. (12) in the CCSDS 122.0.
 */
static size_t int32_bitsize(INT32 cm)
{
	if (cm < 0) {
		return 1 + uint32_ceil_log2(uint32_abs(cm));
	}

	return 1 + uint32_ceil_log2(1 + (UINT32)cm);
}

size_t BitDepthDC(struct bpe *bpe, size_t s)
{
	size_t blk;
	size_t max;

	assert(bpe != NULL);

	assert(bpe->segment != NULL);

	/* max is not defined on empty set */
	assert(s > 0);

	/* start with the first DC */
	max = int32_bitsize(*(bpe->segment + 0 * BLOCK_SIZE));

	/* for each block in the segment */
	for (blk = 0; blk < s; ++blk) {
		INT32 *dc = bpe->segment + blk * BLOCK_SIZE;

		size_t dc_bitsize = int32_bitsize(*dc);

		if (dc_bitsize > max)
			max = dc_bitsize;
	}

	return max;
}

/* max(abs(x)) */
UINT32 block_max_abs_ac(INT32 *data, size_t stride)
{
	UINT32 max;
	size_t y, x;

	assert(data != NULL);

	/* start with the first AC */
	max = uint32_abs(data[0*stride + 1]);

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			UINT32 abs_ac = uint32_abs(data[y*stride + x]);

			if (y == 0 && x == 0)
				continue;

			if (abs_ac > max)
				max = abs_ac;
		}
	}

	return max;
}

/* the maximization is over all AC coefficients x in the block */
size_t BitDepthAC_Block(INT32 *data, size_t stride)
{
	UINT32 max_abs_ac = block_max_abs_ac(data, stride);

	return uint32_ceil_log2(1 + max_abs_ac);
}

size_t BitDepthAC(struct bpe *bpe, size_t s)
{
	size_t blk;
	size_t max;

	assert(bpe != NULL);

	assert(bpe->segment != NULL);

	/* max is not defined on empty set */
	assert(s > 0);

	/* start with the first block */
	max = BitDepthAC_Block(bpe->segment + 0 * BLOCK_SIZE, 8);

	/* for each block in the segment */
	for (blk = 0; blk < s; ++blk) {
		INT32 *block = bpe->segment + blk * BLOCK_SIZE;

		size_t ac_bitsize = BitDepthAC_Block(block, 8);

		if (ac_bitsize > max)
			max = ac_bitsize;
	}

	return max;
}

static const unsigned char lut_codeword_length[8] = {
	8, 40, 16, 48, 24, 56, 32, 64
};

int bpe_init(struct bpe *bpe, const struct parameters *parameters, struct bio *bio, struct frame *frame)
{
	int i;
	int err;

	assert(bpe != NULL);
	assert(parameters != NULL);

	bpe->segment = NULL;
	bpe->quantized_dc = NULL;
	bpe->mapped_quantized_dc = NULL;

	bpe->bio = bio;

	bpe->block_index = 0;
	bpe->segment_index = 0;
	bpe->s = 0;

	bpe->frame = frame;

	/* init Segment Header */

	assert(bpe->frame != NULL);

	bpe->segment_header.StartImgFlag = 1;
	bpe->segment_header.EndImgFlag = 0;
	bpe->segment_header.SegmentCount = 0;
	bpe->segment_header.BitDepthDC = 1; /* It should be noted that the minimum value of BitDepthDC is 1. */
	bpe->segment_header.BitDepthAC = 0; /* It should be noted that the minimum value of BitDepthAC is 0. */
	bpe->segment_header.Part2Flag = 1;
	bpe->segment_header.Part3Flag = 1;
	bpe->segment_header.Part4Flag = 1;
	bpe->segment_header.PadRows = (UINT32)((8 - bpe->frame->height % 8) % 8);
	bpe->segment_header.SegByteLimit = (UINT32)parameters->SegByteLimit;
	bpe->segment_header.DCStop = 0;
	bpe->segment_header.BitPlaneStop = M5;
	bpe->segment_header.StageStop = 3; /* 3 => stage 4 */
	bpe->segment_header.UseFill = 0;
	bpe->segment_header.S = (UINT32) parameters->S;
	bpe->segment_header.OptDCSelect = 1; /* 0 => heuristic selection of k parameter, 1 => optimum selection */
	bpe->segment_header.OptACSelect = 0; /* 0 => heuristic selection of k parameter */
	bpe->segment_header.DWTtype = parameters->DWTtype;
	bpe->segment_header.ExtendedPixelBitDepthFlag = (bpe->frame->bpp >= 16); /* 0 => pixel bit depth is not larger than 16 */
	bpe->segment_header.SignedPixels = 0; /* 0 => unsigned */
	bpe->segment_header.PixelBitDepth = (UINT32)(bpe->frame->bpp % 16); /* the input pixel bit depth */
	bpe->segment_header.ImageWidth = (UINT32)bpe->frame->width;
	bpe->segment_header.TransposeImg = 0;
	bpe->segment_header.CodeWordLength = 6; /* 6 => 32-bit coded words */
	bpe->segment_header.CustomWtFlag = 0; /* no custom weights */

	for (i = 0; i < 12; ++i) {
		bpe->segment_header.weight[i] = parameters->weight[i];
	}

	err = bpe_realloc_segment(bpe, parameters->S);

	if (err) {
		return err;
	}

	return RET_SUCCESS;
}

int bpe_is_last_segment(struct bpe *bpe)
{
	assert(bpe != NULL);

	return bpe->segment_header.EndImgFlag;
}

/* the S has been changed, realloc bpe->segment[] */
int bpe_realloc_segment(struct bpe *bpe, size_t S)
{
	assert(bpe != NULL);

	bpe->S = S;
	bpe->segment_header.S = (UINT32) S;

	bpe->segment = realloc(bpe->segment, S * BLOCK_SIZE * sizeof(INT32));

	if (bpe->segment == NULL && S != 0) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	bpe->quantized_dc = realloc(bpe->quantized_dc, S * sizeof(INT32));

	if (bpe->quantized_dc == NULL && S != 0) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	bpe->mapped_quantized_dc = realloc(bpe->mapped_quantized_dc, S * sizeof(UINT32));

	if (bpe->mapped_quantized_dc == NULL && S != 0) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	return RET_SUCCESS;
}

/* the ImageWidth has been changed, realloc bpe->frame */
int bpe_realloc_frame_width(struct bpe *bpe)
{
	int err;

	assert(bpe != NULL);
	assert(bpe->frame != NULL);

	/* basically, the width cannot be changed during decompression;
	 * the only allowed change is the initial change from 0 to width,
	 * or change from the width to same width */

	if (bpe->frame->width != 0 && bpe->frame->width != (size_t) bpe->segment_header.ImageWidth) {
		/* not allowed */
		return RET_FAILURE_FILE_UNSUPPORTED;
	}

	bpe->frame->width = (size_t) bpe->segment_header.ImageWidth;

	/* call frame_realloc_data */
	err = frame_realloc_data(bpe->frame);

	if (err) {
		return err;
	}

	return RET_SUCCESS;
}

int bpe_realloc_frame_bpp(struct bpe *bpe)
{
	assert(bpe != NULL);
	assert(bpe->frame != NULL);

	bpe->frame->bpp = (size_t) ((!!bpe->segment_header.ExtendedPixelBitDepthFlag * 1UL) * 16 + bpe->segment_header.PixelBitDepth);

	return RET_SUCCESS;
}

/* increase frame height (+8 rows) */
int bpe_increase_frame_height(struct bpe *bpe)
{
	int err;

	assert(bpe != NULL);
	assert(bpe->frame != NULL);

	bpe->frame->height += 8;

	err = frame_realloc_data(bpe->frame);

	if (err) {
		return err;
	}

	return RET_SUCCESS;
}

void bpe_initialize_frame_height(struct bpe *bpe)
{
	assert(bpe != NULL);
	assert(bpe->frame != NULL);

	bpe->frame->height = 0;
}

void bpe_correct_frame_height(struct bpe *bpe)
{
	assert(bpe != NULL);
	assert(bpe->frame != NULL);

	bpe->frame->height -= bpe->segment_header.PadRows;
}

int bpe_destroy(struct bpe *bpe, struct parameters *parameters)
{
	assert(bpe != NULL);

	free(bpe->segment);
	free(bpe->quantized_dc);
	free(bpe->mapped_quantized_dc);

	if (parameters != NULL) {
		parameters->DWTtype = bpe->segment_header.DWTtype;
	}

	return RET_SUCCESS;
}

int bpe_encode_block(INT32 *data, size_t stride, struct bio *bio)
{
	size_t y, x;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			bio_write_int(bio, (UINT32) data[y*stride + x]); /* HACK INT32 -> UINT32 */
		}
	}

	return RET_SUCCESS;
}

#define SET_UINT_INTO_UINT32(var, bit_index, bit_mask) ((UINT32)((var) & (bit_mask)) << (bit_index))
#define SET_BOOL_INTO_UINT32(var, bit_index) ((UINT32)((var) ? 1 : 0) << (bit_index))

/* Part 1A (3 bytes) -- Mandatory */
int bpe_write_segment_header_part1a(struct bpe *bpe)
{
	UINT32 word = 0;

	assert(bpe != NULL);

	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.StartImgFlag, 0); /* +0 Flags initial segment in an image */
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.EndImgFlag, 1); /* +1 Flags final segment in an image */
	word |= SET_UINT_INTO_UINT32(bpe->segment_header.SegmentCount, 2, M8); /* +2 Segment counter value */
	word |= SET_UINT_INTO_UINT32(bpe->segment_header.BitDepthDC, 10, M5); /* +10 value of BitDepthDC (mod 32) */
	word |= SET_UINT_INTO_UINT32(bpe->segment_header.BitDepthAC, 15, M5); /* +15 value of BitDepthAC */
	/* +20 Reserved : 1 -- shall be set to ‘0’ */
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.Part2Flag, 21); /* +21 Part2Flag */
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.Part3Flag, 22); /* +22 Part3Flag */
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.Part4Flag, 23); /* +23 Part4Flag */

	return bio_write_bits(bpe->bio, word, 24);
}

/* Part 1B (1 byte) -- Mandatory for the last segment of image, not included otherwise */
int bpe_write_segment_header_part1b(struct bpe *bpe)
{
	UINT32 word = 0;

	assert(bpe != NULL);

	word |= SET_UINT_INTO_UINT32(bpe->segment_header.PadRows, 0, M3); /* Number of ‘padding’ rows to delete after inverse DWT */
	/*  +3 Reserved : 5 */

	return bio_write_bits(bpe->bio, word, 8);
}

/* Part 2 (5 bytes) -- Optional */
int bpe_write_segment_header_part2(struct bpe *bpe)
{
	UINT32 word = 0;
	int err;

	assert(bpe != NULL);

	/* SegByteLimit (27 bits) */
	word |= SET_UINT_INTO_UINT32(bpe->segment_header.SegByteLimit, 0, M27);

	err = bio_write_bits(bpe->bio, word, 27);

	if (err) {
		return err;
	}

	/* all other fields (13 bits) */
	word = 0; /* reset word */
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.DCStop, 0);
	word |= SET_UINT_INTO_UINT32(bpe->segment_header.BitPlaneStop, 1, M5);
	word |= SET_UINT_INTO_UINT32(bpe->segment_header.StageStop, 6, M2);
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.UseFill, 8);
	/* Reserved : 4 */

	return bio_write_bits(bpe->bio, word, 13);
}

/* Part 3 (3 bytes) */
int bpe_write_segment_header_part3(struct bpe *bpe)
{
	UINT32 word = 0;

	assert(bpe != NULL);

	word |= SET_UINT_INTO_UINT32(bpe->segment_header.S, 0, M20);
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.OptDCSelect, 20);
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.OptACSelect, 21);
	/* Reserved : 2 */

	return bio_write_bits(bpe->bio, word, 24);
}

/* Part 4 (8 bytes) */
int bpe_write_segment_header_part4(struct bpe *bpe)
{
	UINT32 word = 0;
	int err;

	assert(bpe != NULL);

	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.DWTtype, 0);
	/* Reserved : 1 */
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.ExtendedPixelBitDepthFlag, 2);
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.SignedPixels, 3);
	word |= SET_UINT_INTO_UINT32(bpe->segment_header.PixelBitDepth, 4, M4);
	word |= SET_UINT_INTO_UINT32(bpe->segment_header.ImageWidth, 8, M20);
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.TransposeImg, 28);
	word |= SET_UINT_INTO_UINT32(bpe->segment_header.CodeWordLength, 29, M3);

	err = bio_write_bits(bpe->bio, word, 32);

	if (err) {
		return err;
	}

	 word = 0;
	 word |= SET_BOOL_INTO_UINT32(bpe->segment_header.CustomWtFlag, 0);
	 if (bpe->segment_header.CustomWtFlag) {
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[DWT_HH0] - 1,  1, M2); /* CustomWtHH1 (DWT_HH0) */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[DWT_HL0] - 1,  3, M2); /* CustomWtHL1 (DWT_HL0) */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[DWT_LH0] - 1,  5, M2); /* CustomWtLH1 (DWT_LH0) */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[DWT_HH1] - 1,  7, M2); /* CustomWtHH2 (DWT_HH1) */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[DWT_HL1] - 1,  9, M2); /* CustomWtHL2 (DWT_HL1) */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[DWT_LH1] - 1, 11, M2); /* CustomWtLH2 (DWT_LH1) */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[DWT_HH2] - 1, 13, M2); /* CustomWtHH3 (DWT_HH2) */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[DWT_HL2] - 1, 15, M2); /* CustomWtHL3 (DWT_HL2) */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[DWT_LH2] - 1, 17, M2); /* CustomWtLH3 (DWT_LH2) */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[DWT_LL2] - 1, 19, M2); /* CustomWtLL3 (DWT_LL2) */
	 }
	 /* +21 Reserved : 11 */

	 return bio_write_bits(bpe->bio, word, 32);
}

#define GET_UINT_FROM_UINT32(word, bit_index, bit_mask) (((word) >> (bit_index)) & (bit_mask))
#define GET_BOOL_FROM_UINT32(word, bit_index) ((((word) >> (bit_index)) & 1) == 1)

/* Part 1A (3 bytes) */
int bpe_read_segment_header_part1a(struct bpe *bpe)
{
	UINT32 word;
	int err;

	assert(bpe != NULL);

	err = bio_read_bits(bpe->bio, &word, 24);

	if (err) {
		return err;
	}

	bpe->segment_header.StartImgFlag = GET_BOOL_FROM_UINT32(word, 0);
	bpe->segment_header.EndImgFlag = GET_BOOL_FROM_UINT32(word, 1);
	bpe->segment_header.SegmentCount = GET_UINT_FROM_UINT32(word, 2, M8);
	bpe->segment_header.BitDepthDC = GET_UINT_FROM_UINT32(word, 10, M5);
	bpe->segment_header.BitDepthAC = GET_UINT_FROM_UINT32(word, 15, M5);
	bpe->segment_header.Part2Flag = GET_BOOL_FROM_UINT32(word, 21);
	bpe->segment_header.Part3Flag = GET_BOOL_FROM_UINT32(word, 22);
	bpe->segment_header.Part4Flag = GET_BOOL_FROM_UINT32(word, 23);

	return RET_SUCCESS;
}

/* Part 1B (1 byte) */
int bpe_read_segment_header_part1b(struct bpe *bpe)
{
	UINT32 word;
	int err;

	assert(bpe != NULL);

	err = bio_read_bits(bpe->bio, &word, 8);

	if (err) {
		return err;
	}

	bpe->segment_header.PadRows = GET_UINT_FROM_UINT32(word, 0, M3);

	return RET_SUCCESS;
}

/* Part 2 (5 bytes) */
int bpe_read_segment_header_part2(struct bpe *bpe)
{
	UINT32 word;
	int err;

	assert(bpe != NULL);

	err = bio_read_bits(bpe->bio, &word, 27);

	if (err) {
		return err;
	}

	bpe->segment_header.SegByteLimit = GET_UINT_FROM_UINT32(word, 0, M27); /* SegByteLimit : 27 */

	err = bio_read_bits(bpe->bio, &word, 13);

	if (err) {
		return err;
	}

	bpe->segment_header.DCStop = GET_BOOL_FROM_UINT32(word, 0);
	bpe->segment_header.BitPlaneStop = GET_UINT_FROM_UINT32(word, 1, M5);
	bpe->segment_header.StageStop = GET_UINT_FROM_UINT32(word, 6, M2);
	bpe->segment_header.UseFill = GET_BOOL_FROM_UINT32(word, 8);

	return RET_SUCCESS;
}

/* Part 3 (3 bytes) */
int bpe_read_segment_header_part3(struct bpe *bpe)
{
	UINT32 word;
	int err;

	assert(bpe != NULL);

	err = bio_read_bits(bpe->bio, &word, 24);

	if (err) {
		return err;
	}

	bpe->segment_header.S = GET_UINT_FROM_UINT32(word, 0, M20); /* NOTE: S has been changed ==> reallocate bpe->segment[] */
	bpe->segment_header.OptDCSelect = GET_BOOL_FROM_UINT32(word, 20);
	bpe->segment_header.OptACSelect = GET_BOOL_FROM_UINT32(word, 21);

	return RET_SUCCESS;
}

/* Part 4 (8 bytes) */
int bpe_read_segment_header_part4(struct bpe *bpe)
{
	UINT32 word;
	int err;

	assert(bpe != NULL);

	err = bio_read_bits(bpe->bio, &word, 32);

	if (err) {
		return err;
	}

	bpe->segment_header.DWTtype = GET_BOOL_FROM_UINT32(word, 0);
	/* Reserved : 1 */
	bpe->segment_header.ExtendedPixelBitDepthFlag = GET_BOOL_FROM_UINT32(word, 2); /* NOTE bpp has been changed */
	bpe->segment_header.SignedPixels = GET_BOOL_FROM_UINT32(word, 3);
	bpe->segment_header.PixelBitDepth = GET_UINT_FROM_UINT32(word, 4, M4); /* NOTE bpp has been changed */
	bpe->segment_header.ImageWidth = GET_UINT_FROM_UINT32(word, 8, M20); /* NOTE width has been changed ==> reallocate frame->data[] */
	bpe->segment_header.TransposeImg = GET_BOOL_FROM_UINT32(word, 28);
	bpe->segment_header.CodeWordLength = GET_UINT_FROM_UINT32(word, 29, M3);

	err = bio_read_bits(bpe->bio, &word, 32);

	if (err) {
		return err;
	}

	bpe->segment_header.CustomWtFlag = GET_BOOL_FROM_UINT32(word, 0);
	if (bpe->segment_header.CustomWtFlag) {
		bpe->segment_header.weight[DWT_HH0] = 1 + (int)GET_UINT_FROM_UINT32(word,  1, M2); /* CustomWtHH1 (DWT_HH0) */
		bpe->segment_header.weight[DWT_HL0] = 1 + (int)GET_UINT_FROM_UINT32(word,  3, M2); /* CustomWtHL1 (DWT_HL0) */
		bpe->segment_header.weight[DWT_LH0] = 1 + (int)GET_UINT_FROM_UINT32(word,  5, M2); /* CustomWtLH1 (DWT_LH0) */
		bpe->segment_header.weight[DWT_HH1] = 1 + (int)GET_UINT_FROM_UINT32(word,  7, M2); /* CustomWtHH2 (DWT_HH1) */
		bpe->segment_header.weight[DWT_HL1] = 1 + (int)GET_UINT_FROM_UINT32(word,  9, M2); /* CustomWtHL2 (DWT_HL1) */
		bpe->segment_header.weight[DWT_LH1] = 1 + (int)GET_UINT_FROM_UINT32(word, 11, M2); /* CustomWtLH2 (DWT_LH1) */
		bpe->segment_header.weight[DWT_HH2] = 1 + (int)GET_UINT_FROM_UINT32(word, 13, M2); /* CustomWtHH3 (DWT_HH2) */
		bpe->segment_header.weight[DWT_HL2] = 1 + (int)GET_UINT_FROM_UINT32(word, 15, M2); /* CustomWtHL3 (DWT_HL2) */
		bpe->segment_header.weight[DWT_LH2] = 1 + (int)GET_UINT_FROM_UINT32(word, 17, M2); /* CustomWtLH3 (DWT_LH2) */
		bpe->segment_header.weight[DWT_LL2] = 1 + (int)GET_UINT_FROM_UINT32(word, 19, M2); /* CustomWtLL3 (DWT_LL2) */
	}
	/* +21 Reserved : 11 */

	return RET_SUCCESS;
}

int bpe_write_segment_header(struct bpe *bpe)
{
	int err;

	assert(bpe != NULL);

	if (bpe->segment_header.StartImgFlag) {
		bpe->segment_header.Part2Flag = 1;
		bpe->segment_header.Part3Flag = 1;
		bpe->segment_header.Part4Flag = 1;
	}

	/* Segment Header Part 1A (mandatory) */
	err = bpe_write_segment_header_part1a(bpe);

	if (err) {
		return err;
	}

	/* Segment Header Part 1B */
	if (bpe->segment_header.EndImgFlag != 0) {
		err = bpe_write_segment_header_part1b(bpe);

		if (err) {
			return err;
		}
	}

	/* Segment Header Part 2 */
	if (bpe->segment_header.Part2Flag) {
		err = bpe_write_segment_header_part2(bpe);

		if (err) {
			return err;
		}
	}

	/* Segment Header Part 3 */
	if (bpe->segment_header.Part3Flag) {
		err = bpe_write_segment_header_part3(bpe);

		if (err) {
			return err;
		}
	}

	/* Segment Header Part 4 */
	if (bpe->segment_header.Part4Flag) {
		err = bpe_write_segment_header_part4(bpe);

		if (err) {
			return err;
		}
	}

	return RET_SUCCESS;
}

int bpe_read_segment_header(struct bpe *bpe)
{
	int err;

	/* Segment Header Part 1A (mandatory) */
	err = bpe_read_segment_header_part1a(bpe);

	if (err) {
		return err;
	}

	/* Segment Header Part 1B */
	if (bpe->segment_header.EndImgFlag != 0) {
		err = bpe_read_segment_header_part1b(bpe);

		if (err) {
			return err;
		}
	}

	/* Segment Header Part 2 */
	if (bpe->segment_header.Part2Flag) {
		err = bpe_read_segment_header_part2(bpe);

		if (err) {
			return err;
		}
	}

	/* Segment Header Part 3 */
	if (bpe->segment_header.Part3Flag) {
		err = bpe_read_segment_header_part3(bpe);
		/* NOTE S has been changed */

		if (err) {
			return err;
		}
	}

	/* Segment Header Part 4 */
	if (bpe->segment_header.Part4Flag) {
		err = bpe_read_segment_header_part4(bpe);
		/* NOTE bpp & width have been changed */

		if (err) {
			return err;
		}
	}

	return RET_SUCCESS;
}

static size_t size_max(size_t a, size_t b)
{
	return a > b ? a : b;
}

#if 0
static INT32 int32_min(INT32 a, INT32 b)
{
	return a < b ? a : b;
}
#endif

static UINT32 uint32_min(UINT32 a, UINT32 b)
{
	return a < b ? a : b;
}

size_t BitShift(const struct bpe *bpe, int subband)
{
	assert(bpe != NULL);

	switch (bpe->segment_header.DWTtype) {
		case 0: /* Float DWT */
			return 0;
		case 1: /* Integer DWT */
			assert(bpe->segment_header.weight[subband] >= 0);

			return (size_t) bpe->segment_header.weight[subband];
		default:
			dprint (("[ERROR] invalid DWTtype\n"));
			abort();
	}
}

/* maps N to length of the Code Option Identifiers in Table 4-9 */
static const size_t code_option_length[11] = {
	0, /* N = 0 */
	0, /* N = 1 */
	1, /* N = 2 */
	2, /* N = 3 */
	2, /* N = 4 */
	3, /* N = 5 */
	3, /* N = 6 */
	3, /* N = 7 */
	3, /* N = 8 */
	4, /* N = 9 */
	4, /* N = 10 */
};

/* Section 4.3.2.11 b) heuristic procedure */
static UINT32 heuristic_select_code_option(struct bpe *bpe, size_t size, size_t N, size_t g)
{
	size_t i;
	UINT32 *mapped_quantized_dc;
	int first = (g == 0);
	size_t delta = 0;
	size_t J;
	UINT32 k;

	assert(bpe != NULL);

	mapped_quantized_dc = bpe->mapped_quantized_dc;

	assert(mapped_quantized_dc != NULL);

	assert(size > (size_t)first);

	J = size - (size_t)first;

	/* delta = sum over mapped_quantized_dc[] in the gaggle */
	for (i = (size_t)first; i < size; ++i) {
		size_t m = g*16 + i;

		assert(delta <= SIZE_MAX_ - mapped_quantized_dc[m]);

		delta += mapped_quantized_dc[m];
	}

	/* Table 4-10 */

	assert(delta <= SIZE_MAX_ / 64);
	assert(J <= (SIZE_MAX_ >> N));
	assert((J << N) <= SIZE_MAX_ / 23);

	if (64 * delta >= 23 * (J << N)) {
		return (UINT32)-1; /* uncoded */
	}

	assert(delta <= SIZE_MAX_ / 128);
	assert(J <= SIZE_MAX_ / 207);

	if (207 * J > 128 * delta) {
		return 0; /* k=0 */
	}

	assert(J <= (SIZE_MAX_ >> (N+5)));
	assert(128 * delta <= SIZE_MAX_ - 49 * J);

	if ((J << (N+5)) <= 128 * delta + 49 * J) {
		return (UINT32)(N-2); /* k=N-2 */
	}

	/* k is the largest nonnegative integer less
	 * than or equal to N-2 such that ... */
	for (k = (UINT32)(N-2); ; --k) {
		assert(J <= (SIZE_MAX_ >> (k+7)));
		if ((J << (k+7)) <= 128 * delta + 49 * J) {
			return k;
		}
		assert(k != 0 && "internal error");
	}

	assert(0 && "internal error");
}

static UINT32 optimum_select_code_option(struct bpe *bpe, size_t size, size_t N, size_t g)
{
	size_t i;
	int first = (g == 0);
	UINT32 k = 8; /* start with the largest possible k */
	size_t min_bits = SIZE_MAX_;
	UINT32 *mapped_quantized_dc;
	UINT32 min_k;

	assert(bpe != NULL);

	mapped_quantized_dc = bpe->mapped_quantized_dc;

	assert(mapped_quantized_dc != NULL);

	assert(N >= 2 && N <= 10);

	/* see Table 4-9 */
	if (N <= 8)
		k = 6;
	if (N <= 4)
		k = 2;
	if (N == 2)
		k = 0;

	min_k = k;

	/* select the value of k that minimizes the number of encoded bits */
	/* When two or more code parameters minimize the number of encoded bits,
	 * the smallest code parameter option shall be selected */
	do {
		size_t bits = 0;

		/* compute the number of encoded bits with given k */
		for (i = (size_t)first; i < size; ++i) {
			size_t m = g*16 + i;

			bits += bio_sizeof_gr(k, mapped_quantized_dc[m]);
		}

		if (bits <= min_bits) {
			min_bits = bits;
			min_k = k;
		}

		if (k == 0) {
			break;
		}

		k--;
	} while (1);

	/* The uncoded option shall be selected whenever it minimizes the number
	 * of encoded bits, even if another option gives the same number of bits. */
	if (min_bits == (size - (size_t)first)*N) {
		min_k = (UINT32)-1;
	}

	return min_k;
}

/* Section 4.3.2.6 */
static int bpe_encode_segment_initial_coding_of_DC_coefficients_1st_step_gaggle(struct bpe *bpe, size_t size, size_t N, size_t g)
{
	UINT32 k = (UINT32)-1; /* uncoded by default */
	INT32 *quantized_dc;
	UINT32 *mapped_quantized_dc;
	int err;
	int first = (g == 0);

	assert(bpe != NULL);

	quantized_dc = bpe->quantized_dc;
	mapped_quantized_dc = bpe->mapped_quantized_dc;

	assert(first == 0 || quantized_dc != NULL);
	assert(mapped_quantized_dc != NULL);
	assert(size > 0);

	if (size == 1 && (size_t)first == 1) {
		dprint (("the gaggle consists of a single reference sample (J = 0)\n"));
	} else {
		switch (bpe->segment_header.OptDCSelect) {
			case 0:
				k = heuristic_select_code_option(bpe, size, N, g);
				break;
			case 1:
				k = optimum_select_code_option(bpe, size, N, g);
				break;
			default:
				dprint (("[ERROR] invalid value for OptDCSelect\n"));
				return RET_FAILURE_LOGIC_ERROR;
		}
	}

	/* write code option k */
	err = bio_write_bits(bpe->bio, k, code_option_length[N]);

	if (err) {
		return err;
	}

	if (first) {
		/* first gaggle in a segment */

		/* N-bit reference */
		err = bio_write_bits(bpe->bio, (UINT32) quantized_dc[0], N);

		if (err) {
			return err;
		}
	}

	if (k == (UINT32)-1) {
		/* UNCODED */
		/* that is, each delta_m is encoded using the conventional N-bit unsigned binary integer representation */
		size_t i;

		for (i = (size_t)first; i < size; ++i) {
			/* write mapped sample difference */
			size_t m = g*16 + i;

			dprint (("BPE(4.3.2.8): writing mapped_quantized_dc[%lu]\n", m));

			/* 4.3.2.8 */
			assert(mapped_quantized_dc[m] < (1U<<N));

			err = bio_write_bits(bpe->bio, mapped_quantized_dc[m], N);

			if (err) {
				return err;
			}
		}
	} else {
		/* CODED Data Format for a Gaggle When a Coding Option Is Selected */
		/* encoded via one of several variable-length codes parameterized by a nonnegative integer k */
		size_t i;

		for (i = (size_t)first; i < size; ++i) {
			/* write mapped sample difference */
			size_t m = g*16 + i;

			/* first part words */
			err = bio_write_gr_1st_part(bpe->bio, (size_t)k, mapped_quantized_dc[m]);

			if (err) {
				return err;
			}
		}

		for (i = (size_t)first; i < size; ++i) {
			/* write mapped sample difference */
			size_t m = g*16 + i;

			/* second part words */
			err = bio_write_gr_2nd_part(bpe->bio, (size_t)k, mapped_quantized_dc[m]);

			if (err) {
				return err;
			}
		}
	}

	return RET_SUCCESS;
}

static int bpe_decode_segment_initial_coding_of_DC_coefficients_1st_step_gaggle(struct bpe *bpe, size_t size, size_t N, size_t g)
{
	UINT32 k;
	INT32 *quantized_dc;
	UINT32 *mapped_quantized_dc;
	int err;
	int first = (g == 0);

	assert(bpe != NULL);

	quantized_dc = bpe->quantized_dc;
	mapped_quantized_dc = bpe->mapped_quantized_dc;

	assert(first == 0 || quantized_dc != NULL);
	assert(mapped_quantized_dc != NULL);
	assert(size > 0);

	/* read code option */
	err = bio_read_dc_bits(bpe->bio, &k, code_option_length[N]);

	if (err) {
		return err;
	}

	/* e.g. 100 (in binary) should not be considered as negative number */
	if (k != (UINT32)-1) {
		k &= ((UINT32)1 << code_option_length[N]) - 1;
	}

	if (first) {
		/* first gaggle in a segment */

		/* N-bit reference */
		err = bio_read_dc_bits(bpe->bio, (UINT32 *) &quantized_dc[0], N);

		if (err) {
			return err;
		}
	}

	if (k == (UINT32)-1) {
		/* UNCODED */
		size_t i;

		for (i = (size_t)first; i < size; ++i) {
			/* write mapped sample difference */
			size_t m = g*16 + i;

			dprint (("BPE(4.3.2.8): reading mapped_quantized_dc[%lu]\n", m));

			/* 4.3.2.8 */
			err = bio_read_bits(bpe->bio, &mapped_quantized_dc[m], N);

			assert( mapped_quantized_dc[m] < (1U<<N) );

			if (err) {
				return err;
			}
		}
	} else {
		/* CODED Data Format for a Gaggle When a Coding Option Is Selected */
		size_t i;

		for (i = (size_t)first; i < size; ++i) {
			size_t m = g*16 + i;

			/* first part words */
			err = bio_read_gr_1st_part(bpe->bio, (size_t)k, &mapped_quantized_dc[m]);

			if (err) {
				return err;
			}
		}

		for (i = (size_t)first; i < size; ++i) {
			size_t m = g*16 + i;

			/* second part words */
			err = bio_read_gr_2nd_part(bpe->bio, (size_t)k, &mapped_quantized_dc[m]);

			if (err) {
				return err;
			}
		}
	}

	return RET_SUCCESS;
}

/* Eq. (19) */
static UINT32 map_quantized_dc(INT32 d_, UINT32 theta, INT32 sign)
{
	UINT32 d; /* (19) = mapped quantized coefficients */

	/* Each difference value ... shall be mapped to a non-negative integer ... */
	if (d_ >= 0 && (UINT32)d_ <= theta) {
		/* case 0: d is even && d <= 2*theta */
		d = 2 * (UINT32)d_;
	} else if (d_ < 0 && (UINT32)-d_ <= theta) {
		/* case 1: d is odd && d < 2*theta */
		d = 2 * uint32_abs(d_) - 1;
	} else {
		/* case 2: d > 2*theta */
		if (d_ < 0) assert( sign == -1 );
		if (d_ > 0) assert( sign == +1 );

		d = theta + uint32_abs(d_);
	}

	return d;
}

static INT32 inverse_map_quantized_dc(UINT32 d, UINT32 theta, INT32 sign)
{
	INT32 d_;

	if ( (d & 1) == 0 && d <= 2*theta) {
		/* case 1 */
		d_ = (INT32)d / 2;
	} else if ( (d & 0) == 0 && d <= 2*theta) {
		/* case 1 */
		d_ = -(INT32)( (d + 1) / 2 );
	} else {
		/* case 2 */
		d_ = sign*(INT32)((INT32)d - (INT32)theta);
	}

	return d_;
}

/* 4.3.2.4 */
static void map_quantized_dcs_to_mapped_quantized_dcs(struct bpe *bpe, size_t N)
{
	size_t S;
	INT32 *quantized_dc;
	UINT32 *mapped_quantized_dc;
	size_t m;

	assert(bpe != NULL);

	S = bpe->S;
	quantized_dc = bpe->quantized_dc;
	mapped_quantized_dc = bpe->mapped_quantized_dc;

	assert(quantized_dc != NULL);
	assert(mapped_quantized_dc != NULL);

	assert(S > 0);
	assert(N > 1);

	/* 4.3.2.4 For the remaining S-1 DC coefficients, the difference between successive quantized
	 * coefficient values (taken in raster scan order) shall be encoded. */
	for (m = 1; m < S; ++m) {
		INT32 d_ = quantized_dc[m] - quantized_dc[m-1]; /* (18) */
		INT32 x_min = -((INT32)1 << (N-1)); /* the minimum signal value */
		INT32 x_max = +((INT32)1 << (N-1)) - 1; /* the maximum signal value */
		/* x_min - quantized_dc[m-1] .. the smallest prediction error value */
		/* x_max - quantized_dc[m-1] .. the largest prediction error value */
		UINT32 theta = uint32_min((UINT32)(quantized_dc[m-1] - x_min), (UINT32)(x_max - quantized_dc[m-1]));
		INT32 sign = (UINT32)(quantized_dc[m-1] - x_min) > (UINT32)(x_max - quantized_dc[m-1]) ? -1 : +1; /* FIXME: sign when d' is outside [-theta;+theta] */

		/* NOTE see also CCSDS 121.0-B-2 */
		assert(quantized_dc[m] <= x_max);
		assert(quantized_dc[m] >= x_min);

		assert(quantized_dc[m-1] - x_min >= 0);
		assert(x_max - quantized_dc[m-1] >= 0);

		/* Each difference value ... shall be mapped to a non-negative integer ... */
		mapped_quantized_dc[m] = map_quantized_dc(d_, theta, sign); /* (19) = mapped quantized coefficients */
	}
}

static void map_mapped_quantized_dcs_to_quantized_dcs(struct bpe *bpe, size_t N)
{
	size_t S;
	INT32 *quantized_dc;
	UINT32 *mapped_quantized_dc;
	size_t m;

	assert(bpe != NULL);

	S = bpe->S;
	quantized_dc = bpe->quantized_dc;
	mapped_quantized_dc = bpe->mapped_quantized_dc;

	assert(quantized_dc != NULL);
	assert(mapped_quantized_dc != NULL);

	assert(S > 0);
	assert(N > 1);

	/* 4.3.2.4 For the remaining S-1 DC coefficients, the difference between successive quantized
	 * coefficient values (taken in raster scan order) shall be encoded. */
	for (m = 1; m < S; ++m) {
		INT32 x_min = -((INT32)1 << (N-1));
		INT32 x_max = +((INT32)1 << (N-1)) - 1;
		UINT32 theta = uint32_min((UINT32)(quantized_dc[m-1] - x_min), (UINT32)(x_max - quantized_dc[m-1]));
		INT32 sign = (UINT32)(quantized_dc[m-1] - x_min) > (UINT32)(x_max - quantized_dc[m-1]) ? -1 : +1;

		assert(quantized_dc[m-1] - x_min >= 0);
		assert(x_max - quantized_dc[m-1] >= 0);

		quantized_dc[m] = inverse_map_quantized_dc(mapped_quantized_dc[m], theta, sign) + quantized_dc[m-1];

		assert(quantized_dc[m] <= x_max);
		assert(quantized_dc[m] >= x_min);
	}
}

/* Section 4.3.2 CODING QUANTIZED DC COEFFICIENTS */
int bpe_encode_segment_initial_coding_of_DC_coefficients_1st_step(struct bpe *bpe, size_t q)
{
	size_t bitDepthDC;
	size_t N;
	INT32 *quantized_dc;
	size_t S;

	assert(bpe != NULL);

	S = bpe->S;

	quantized_dc = bpe->quantized_dc;

	bitDepthDC = (size_t) bpe->segment_header.BitDepthDC;

	/* 4.3.2.1 The number of bits needed to represent each quantized DC coefficient */
	if (q > bitDepthDC)
		N = 1;
	else
		N = size_max(bitDepthDC - q, 1);

	assert(N <= 10);

	/* 4.3.2.2 When N is 1, each quantized DC coefficient c'm consists of a single bit. */
	if (N == 1) {
		size_t blk;

		/* In this case, the coded quantized DC coefficients for a segment consist of these bits, concatenated together. */

		for (blk = 0; blk < S; ++blk) {
			int err;

			assert(quantized_dc != NULL);

			assert(quantized_dc[blk] == 0 || quantized_dc[blk] == -1);

			err = bio_put_bit(bpe->bio, (unsigned char) (quantized_dc[blk] != 0));

			if (err) {
				return err;
			}
		}
	} else {
		size_t g, full_G, G;

		/* DC coefficients are represented using two's-complement representation. */

		assert(S > 0);

		map_quantized_dcs_to_mapped_quantized_dcs(bpe, N);

		/* 4.3.2.5 Each gaggle contains up to 16 mapped quantized coefficients */
		full_G = S / 16;
		G = (S + 15) / 16;

		/* g .. gaggle number */
		for (g = 0; g < G; ++g) {
			size_t ge = (g < full_G) ? 16 : (S % 16);
			int err;

			err = bpe_encode_segment_initial_coding_of_DC_coefficients_1st_step_gaggle(bpe, ge, N, g);

			if (err) {
				return err;
			}
		}
	}

	return RET_SUCCESS;
}

/* Section 4.3.3 ADDITIONAL BIT PLANES OF DC COEFFICIENTS */
int bpe_encode_segment_initial_coding_of_DC_coefficients_2nd_step(struct bpe *bpe, size_t q)
{
	size_t bitDepthAC;
	size_t blk;
	size_t S;

	assert(bpe != NULL);

	S = bpe->S;

	bitDepthAC = (size_t) bpe->segment_header.BitDepthAC;

	if (q > size_max(bitDepthAC, BitShift(bpe, DWT_LL2))) {
		/* 4.3.3.1 */
		size_t B = q - size_max(bitDepthAC, BitShift(bpe, DWT_LL2));
		size_t b;

		dprint (("BPE(4.3.3): encoding additional %lu bits\n", B));

		assert(B > 0);

		for (b = 0; b < B; ++b) {
			size_t p; /* bit plane */

			assert(q-1 >= b);

			p = q-1-b;

			/* 4.3.3.2: encode p-th most-significant bit of each DC coefficient */
			for (blk = 0; blk < S; ++blk) {
				INT32 dc_bit = (*(bpe->segment + blk * BLOCK_SIZE) >> p) & 1;

				int err = bio_put_bit(bpe->bio, (unsigned char)dc_bit);

				if (err) {
					return err;
				}
			}
		}
	}

	return RET_SUCCESS;
}

int bpe_decode_segment_initial_coding_of_DC_coefficients_1st_step(struct bpe *bpe, size_t q)
{
	size_t bitDepthDC;
	size_t N;
	INT32 *quantized_dc;
	size_t S;

	assert(bpe != NULL);

	S = bpe->S;

	quantized_dc = bpe->quantized_dc;

	bitDepthDC = (size_t) bpe->segment_header.BitDepthDC;

	/* 4.3.2.1 The number of bits needed to represent each quantized DC coefficient */
	if (q > bitDepthDC)
		N = 1;
	else
		N = size_max(bitDepthDC - q, 1);

	assert(N <= 10);

	assert(quantized_dc != NULL);

	/* 4.3.2.2 When N is 1, each quantized DC coefficient c'm consists of a single bit. */
	if (N == 1) {
		size_t blk;

		/* In this case, the coded quantized DC coefficients for a segment consist of these bits, concatenated together. */

		for (blk = 0; blk < S; ++blk) {
			int err;
			unsigned char bit;

			err = bio_get_bit(bpe->bio, (unsigned char *)&bit);

			if (err) {
				return err;
			}

			quantized_dc[blk] = bit ? -1 : 0;
		}
	} else {
		size_t g, full_G, G;

		/* DC coefficients are represented using two's-complement representation. */

		assert(S > 0);

		/* 4.3.2.5 Each gaggle contains up to 16 mapped quantized coefficients */
		full_G = S / 16;
		G = (S + 15) / 16;

		/* g .. gaggle number */
		for (g = 0; g < G; ++g) {
			size_t ge = (g < full_G) ? 16 : (S % 16);
			int err;

			err = bpe_decode_segment_initial_coding_of_DC_coefficients_1st_step_gaggle(bpe, ge, N, g);

			if (err) {
				return err;
			}
		}

		map_mapped_quantized_dcs_to_quantized_dcs(bpe, N);
	}

	return RET_SUCCESS;
}

/* Section 4.3.3 ADDITIONAL BIT PLANES OF DC COEFFICIENTS */
int bpe_decode_segment_initial_coding_of_DC_coefficients_2nd_step(struct bpe *bpe, size_t q)
{
	size_t blk;
	size_t S;
	size_t bitDepthAC;

	assert(bpe != NULL);

	S = bpe->S;

	bitDepthAC = (size_t) bpe->segment_header.BitDepthAC;

	if (q > size_max(bitDepthAC, BitShift(bpe, DWT_LL2))) {
		/* 4.3.3.1 */
		size_t B = q - size_max(bitDepthAC, BitShift(bpe, DWT_LL2));
		size_t b;

		dprint (("BPE(4.3.3): decoding additional %lu bits\n", B));

		assert(B > 0);

		for (b = 0; b < B; ++b) {
			size_t p; /* bit plane */

			assert(q-1 >= b);

			p = q-1-b;

			/* 4.3.3.2: encode p-th most-significant bit of each DC coefficient */
			for (blk = 0; blk < S; ++blk) {
				unsigned char bit;

				int err = bio_get_bit(bpe->bio, &bit);

				if (err) {
					return err;
				}

				*(bpe->segment + blk * BLOCK_SIZE) |= ((INT32)bit << p);
			}
		}
	}

	return RET_SUCCESS;
}

/* Sections 4.3.1.2 and 4.3.1.3, returns q, using Eq. (15) and Table 4-8 */
static size_t DC_quantization_factor(struct bpe *bpe)
{
	size_t bitDepthDC; /* BitDepthDC */
	size_t bitDepthAC; /* BitDepthAC */
	size_t q_; /* q' in Table 4-8 */
	size_t q; /* q in Eq. (15) */

	assert(bpe != NULL);

	bitDepthDC = (size_t) bpe->segment_header.BitDepthDC;
	bitDepthAC = (size_t) bpe->segment_header.BitDepthAC;

	if (bitDepthDC <= 3)
		q_ = 0;
	else if (bitDepthDC <= 1 + (1 + bitDepthAC/2) && bitDepthDC > 3)
		q_ = bitDepthDC - 3;
	else if (bitDepthDC > 10 + (1 + bitDepthAC/2) && bitDepthDC > 3)
		q_ = bitDepthDC - 10;
	else
		q_ = 1 + bitDepthAC/2;

	q = size_max(q_, BitShift(bpe, DWT_LL2)); /* FIXME LL3 in (15) */

	/* The value of q indicates the number of least-significant bits
	 * in each DC coefficient that are not encoded in the quantized
	 * DC coefficient values. */

	return q;
}

/* Section 4.3 */
int bpe_encode_segment_initial_coding_of_DC_coefficients(struct bpe *bpe)
{
	size_t blk;
	size_t q;
	size_t S;
	INT32 *quantized_dc;
	int err;

	assert(bpe != NULL);

	S = bpe->S;

	q = DC_quantization_factor(bpe);

	assert(q <= 32);

	/* 4.3.1.5 Next, given a sequence of DC coefficients in a segment,
	 * the BPE shall compute quantized coefficients */

	quantized_dc = bpe->quantized_dc;

	assert(quantized_dc != NULL);

	for (blk = 0; blk < S; ++blk) {
		/* NOTE in general, DC coefficients are INT32 and can be negative */
		quantized_dc[blk] = *(bpe->segment + blk * BLOCK_SIZE) >> q; /* Eq. (16) */
	}

	/* NOTE Section 4.3.2 */
	err = bpe_encode_segment_initial_coding_of_DC_coefficients_1st_step(bpe, q);

	if (err) {
		return err;
	}

	/* NOTE Section 4.3.3 */
	err = bpe_encode_segment_initial_coding_of_DC_coefficients_2nd_step(bpe, q);

	if (err) {
		return err;
	}

	return RET_SUCCESS;
}

int bpe_decode_segment_initial_coding_of_DC_coefficients(struct bpe *bpe)
{
	size_t blk;
	size_t q;
	size_t S;
	INT32 *quantized_dc;
	int err;

	assert(bpe != NULL);

	S = bpe->S;

	q = DC_quantization_factor(bpe);

	/* 4.3.1.5 Next, given a sequence of DC coefficients in a segment,
	 * the BPE shall compute quantized coefficients */

	quantized_dc = bpe->quantized_dc;

	assert(quantized_dc != NULL);

	/* NOTE Section 4.3.2 */
	err = bpe_decode_segment_initial_coding_of_DC_coefficients_1st_step(bpe, q);

	if (err) {
		return err;
	}

	for (blk = 0; blk < S; ++blk) {
		*(bpe->segment + blk * BLOCK_SIZE) = quantized_dc[blk] << q; /* inverse of Eq. (16) */
	}

	/* NOTE Section 4.3.3 */
	err = bpe_decode_segment_initial_coding_of_DC_coefficients_2nd_step(bpe, q);

	if (err) {
		return err;
	}

	return RET_SUCCESS;
}

/* write segment into bitstream */
int bpe_encode_segment(struct bpe *bpe, int flush)
{
	size_t S;
#if (DEBUG_ENCODE_BLOCKS == 1)
	size_t blk;
#endif
	int err;

	assert(bpe != NULL);

	S = bpe->S;

	dprint (("BPE: encoding segment %lu (%lu blocks)\n", bpe->segment_index, S));

	/* next block is not valid block (behind the image) */
	if (flush) {
		dprint (("BPE: the last segment indicated\n"));
		bpe->segment_header.EndImgFlag = 1;
		bpe->segment_header.Part3Flag = 1; /* signal new "S" */
	}

	bpe->segment_header.StartImgFlag = (bpe->block_index == 0);
	bpe->segment_header.SegmentCount = bpe->segment_index & M8;
	bpe->segment_header.BitDepthDC = (UINT32) BitDepthDC(bpe, S);
	bpe->segment_header.BitDepthAC = (UINT32) BitDepthAC(bpe, S);
	/* Part 2: */
	/* SegByteLimit */

	err = bpe_write_segment_header(bpe);

	if (err) {
		return err;
	}

	/* Section 4.3 The initial coding of DC coefficients in a segment is performed in two steps. */
	bpe_encode_segment_initial_coding_of_DC_coefficients(bpe);

#if (DEBUG_ENCODE_BLOCKS == 1)
	for (blk = 0; blk < S; ++blk) {
		/* encode the block */
		bpe_encode_block(bpe->segment + blk * BLOCK_SIZE, 8, bpe->bio);
	}
#endif

	/* after writing of the first segment, set some flags to zero */
	bpe->segment_header.StartImgFlag = 0;
	bpe->segment_header.Part2Flag = 0;
	bpe->segment_header.Part3Flag = 0;
	bpe->segment_header.Part4Flag = 0;

	bpe->segment_index ++;

	return RET_SUCCESS;
}

int bpe_zero_block(INT32 *data, size_t stride)
{
	size_t y, x;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			data[y*stride + x] = 0;
		}
	}

	return RET_SUCCESS;
}

int bpe_decode_block(INT32 *data, size_t stride, struct bio *bio)
{
	size_t y, x;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			bio_read_int(bio, (UINT32 *) &data[y*stride + x]); /* HACK UINT32 -> INT32 */
		}
	}

	return RET_SUCCESS;
}

int bpe_decode_segment(struct bpe *bpe)
{
	size_t S;
	size_t blk;
	int err;

	assert(bpe != NULL);

	S = bpe->S;

	/* the 'S' in the last block should be decoded from Part 4 of the Segment Header */

	err = bpe_read_segment_header(bpe);

	if (err) {
		return err;
	}

#if 0
	dprint (("BPE :: Segment Header :: StartImgFlag  = %i\n", bpe->segment_header.StartImgFlag));
	dprint (("BPE :: Segment Header :: EndImgFlag    = %i\n", bpe->segment_header.EndImgFlag));
	dprint (("BPE :: Segment Header :: Part3Flag     = %i\n", bpe->segment_header.Part3Flag));
	dprint (("BPE :: Segment Header :: Part4Flag     = %i\n", bpe->segment_header.Part4Flag));
	dprint (("BPE :: Segment Header :: S             = %lu\n", bpe->segment_header.S));
	dprint (("BPE :: Segment Header :: PixelBitDepth = %u\n", bpe->segment_header.PixelBitDepth));
#endif

	if (bpe->segment_header.Part3Flag) {
		int err;

		err = bpe_realloc_segment(bpe, bpe->segment_header.S);

		if (err) {
			return err;
		}

		S = bpe->S;
	}

	if (bpe->segment_header.Part4Flag) {
		int err;

		err = bpe_realloc_frame_width(bpe);

		if (err) {
			return err;
		}

		bpe_realloc_frame_bpp(bpe);

		/* what about DWTtype, etc.? */
	}

	dprint (("BPE: decoding segment %lu (%lu blocks)\n", bpe->segment_index, S));

	/* HACK: until the BPE is fully implemented */
#if (DEBUG_ENCODE_BLOCKS == 0)
	for (blk = 0; blk < S; ++blk) {
		bpe_zero_block(bpe->segment + blk * BLOCK_SIZE, 8);
	}
#endif

	bpe_decode_segment_initial_coding_of_DC_coefficients(bpe);

#if (DEBUG_ENCODE_BLOCKS == 1)
	for (blk = 0; blk < S; ++blk) {
		/* decode the block */
		bpe_decode_block(bpe->segment + blk * BLOCK_SIZE, 8, bpe->bio);
	}
#endif

	bpe->segment_index ++;

	return RET_SUCCESS;
}

/* push block into bpe->segment[] */
int bpe_push_block(struct bpe *bpe, INT32 *data, size_t stride, int flush)
{
	size_t S;
	size_t s;
	INT32 *local;
	size_t y, x;

	assert(bpe != NULL);

	S = bpe->S;
	s = bpe->s;
	local = bpe->segment + s * BLOCK_SIZE;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			local[y*8 + x] = data[y*stride + x];
		}
	}

	if (flush) {
		bpe_realloc_segment(bpe, s + 1);
		S = s + 1;
	}

	if (s + 1 == S) {
		int err;

		/* encode this segment */
		err = bpe_encode_segment(bpe, flush);

		if (err) {
			return err;
		}

		bpe->s = 0;
	} else {
		bpe->s ++;
	}

	/* next block will be... */
	bpe->block_index ++;

	return RET_SUCCESS;
}

int bpe_pop_block_decode(struct bpe *bpe)
{
	assert(bpe != NULL);

	/* if this is the first block in the segment, deserialize it */
	if (bpe->s == 0) {
		int err;

		err = bpe_decode_segment(bpe);

		if (err) {
			return err;
		}
	}

	return RET_SUCCESS;
}

int bpe_pop_block_copy_data(struct bpe *bpe, INT32 *data, size_t stride)
{
	INT32 *local;
	size_t y, x;

	assert(bpe != NULL);

	/* pop the block from bpe->segment[] */
	local = bpe->segment + bpe->s * BLOCK_SIZE;

	/* access frame->data[] */
	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			data[y*stride + x] = local[y*8 + x];
		}
	}

	/* next block will be */
	bpe->s ++;

	if (bpe->s == bpe->S) {
		bpe->s = 0;
	}
	bpe->block_index ++;

	return RET_SUCCESS;
}

size_t get_total_no_blocks(struct frame *frame)
{
	size_t height, width;

	assert(frame != NULL);

	height = ceil_multiple8(frame->height);
	width  = ceil_multiple8(frame->width);

	return height / 8 * width / 8;
}

int block_by_index(struct block *block, struct frame *frame, size_t block_index)
{
	size_t width;
	size_t y, x;
	int *data;

	assert(frame != NULL);

	width  = ceil_multiple8(frame->width);

	y = block_index / (width / 8) * 8;
	x = block_index % (width / 8) * 8;

	data = frame->data;

	assert(block != NULL);

	block->data = data + y*width + x;
	block->stride = width;

	return RET_SUCCESS;
}

int block_starts_new_stripe(struct frame *frame, size_t block_index)
{
	size_t width;
	size_t x;

	assert(frame != NULL);

	width  = ceil_multiple8(frame->width);

	x = block_index % (width / 8) * 8;

	return x == 0;
}

int bpe_encode(struct frame *frame, const struct parameters *parameters, struct bio *bio)
{
	size_t block_index;
	size_t total_no_blocks;
	struct bpe bpe;
	int err;

	assert(frame != NULL);

	total_no_blocks = get_total_no_blocks(frame);

	err = bpe_init(&bpe, parameters, bio, frame);

	if (err) {
		return err;
	}

	/* push all blocks into the BPE engine */
	for (block_index = 0; block_index < total_no_blocks; ++block_index) {
		int err;
		struct block block;

		block_by_index(&block, frame, block_index);

		err = bpe_push_block(&bpe, block.data, block.stride, (block_index + 1 == total_no_blocks));

		if (err) {
			return err;
		}
	}

	bpe_destroy(&bpe, NULL);

	return RET_SUCCESS;
}

int bpe_decode(struct frame *frame, struct parameters *parameters, struct bio *bio)
{
	size_t block_index;
	struct bpe bpe;
	int err;

	assert(frame != NULL);

	err = bpe_init(&bpe, parameters, bio, frame);

	if (err) {
		return err;
	}

	/* NOTE bpe_init already called bpe_realloc_segment */

	/* initialize frame->height */
	bpe_initialize_frame_height(&bpe);

	/* initialize frame->width & frame->data[] */
	err = bpe_realloc_frame_width(&bpe);

	if (err) {
		return err;
	}

	/* initialize frame->bpp */
	bpe_realloc_frame_bpp(&bpe);

	/* push all blocks into the BPE engine */
	for (block_index = 0; ; ++block_index) {
		int err;
		struct block block;

		/* NOTE: the bpe_pop_block_decode reallocates frame->data[] */
		err = bpe_pop_block_decode(&bpe);

		/* other error */
		if (err) {
			return err;
		}

		if (block_starts_new_stripe(frame, block_index)) {
			int err;

			/* increase height */
			err = bpe_increase_frame_height(&bpe);

			if (err) {
				return err;
			}
		}

		block_by_index(&block, frame, block_index);

		bpe_pop_block_copy_data(&bpe, block.data, block.stride);

		if (bpe_is_last_segment(&bpe) && bpe.s == 0) {
			dprint (("BPE: the last segment indicated, breaking the decoding loop!\n"));
			break;
		}
	}

	bpe_correct_frame_height(&bpe);

	bpe_destroy(&bpe, parameters);

	return RET_SUCCESS;
}

size_t get_maximum_stream_size(struct frame *frame)
{
	size_t width, height;

	assert(frame != NULL);

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	return height * width * sizeof(int) + 4096 * get_total_no_blocks(frame) + 4096;
}
