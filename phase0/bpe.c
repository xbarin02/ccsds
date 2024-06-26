#include "bpe.h"
#include "common.h"
#include <assert.h>
#include <stdlib.h>

/* HACK: until the BPE is fully implemented */
#define DEBUG_ENCODE_BLOCKS 0

#define BLOCK_SIZE (8 * 8)

/* Mn = 2^n - 1 */
#define M2 3
#define M3 7
#define M4 15
#define M5 31
#define M8 255
#define M20 1048575
#define M27 134217727
#define M31 2147483647

struct block {
	int *data;
	size_t stride;
};

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

size_t BitDepthDC(struct bpe *bpe)
{
	size_t blk;
	size_t max;
	size_t S;

	assert(bpe != NULL);

	S = bpe->S;

	assert(bpe->segment != NULL);

	/* max is not defined on empty set */
	assert(S > 0);

	/* start with the first DC */
	max = int32_bitsize(*(bpe->segment + 0 * BLOCK_SIZE));

	/* for each block in the segment */
	for (blk = 0; blk < S; ++blk) {
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

size_t BitDepthAC(struct bpe *bpe)
{
	size_t blk;
	size_t max;
	size_t S;

	assert(bpe != NULL);

	S = bpe->S;

	assert(bpe->segment != NULL);

	/* max is not defined on empty set */
	assert(S > 0);

	/* start with the first block */
	max = BitDepthAC_Block(bpe->segment + 0 * BLOCK_SIZE, 8);

	/* for each block in the segment */
	for (blk = 0; blk < S; ++blk) {
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
	bpe->bitDepthAC_Block = NULL;
	bpe->mapped_BitDepthAC_Block = NULL;
	bpe->type = NULL;
	bpe->sign = NULL;
	bpe->magnitude = NULL;

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
	bpe->segment_header.DCStop = parameters->DCStop; /* 1 => Terminate coded segment after coding quantized DC coefficient information and additional DC bit planes */
	bpe->segment_header.BitPlaneStop = 0; /* When BitPlaneStop = b and StageStop = s, coded segment terminates once stage s of bit plane b has been completed */
	bpe->segment_header.StageStop = 3; /* 3 => stage 4 */
	bpe->segment_header.UseFill = 0;
	bpe->segment_header.S = (UINT32) parameters->S;
	bpe->segment_header.OptDCSelect = parameters->OptDCSelect; /* 0 => heuristic selection of k parameter, 1 => optimum selection */
	bpe->segment_header.OptACSelect = parameters->OptACSelect; /* 0 => heuristic selection of k parameter, 1 => optimum selection */
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

	bpe->bitDepthAC_Block = realloc(bpe->bitDepthAC_Block, S * sizeof(UINT32));

	if (bpe->bitDepthAC_Block == NULL && S != 0) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	bpe->mapped_BitDepthAC_Block = realloc(bpe->mapped_BitDepthAC_Block, S * sizeof(UINT32));

	if (bpe->mapped_BitDepthAC_Block == NULL && S != 0) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	bpe->type = realloc(bpe->type, S * BLOCK_SIZE * sizeof(int));

	if (bpe->type == NULL && S != 0) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	bpe->sign = realloc(bpe->sign, S * BLOCK_SIZE * sizeof(INT32));

	if (bpe->sign == NULL && S != 0) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	bpe->magnitude = realloc(bpe->magnitude, S * BLOCK_SIZE * sizeof(UINT32));

	if (bpe->magnitude == NULL && S != 0) {
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
	free(bpe->bitDepthAC_Block);
	free(bpe->mapped_BitDepthAC_Block);
	free(bpe->type);
	free(bpe->sign);
	free(bpe->magnitude);

	if (parameters != NULL) {
		parameters->DWTtype = bpe->segment_header.DWTtype;
	}

	return RET_SUCCESS;
}

#if (DEBUG_ENCODE_BLOCKS == 1)
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
#endif

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

static UINT32 heuristic_select_code_option(size_t size, size_t N, size_t g, UINT32 *mapped)
{
	size_t i;
	int first = (g == 0);
	size_t delta = 0;
	size_t J;
	UINT32 k;

	assert(mapped != NULL);

	assert(size > (size_t)first);

	J = size - (size_t)first;

	/* delta = sum over mapped[] in the gaggle */
	for (i = (size_t)first; i < size; ++i) {
		size_t m = g * 16 + i;

		assert(delta <= SIZE_MAX_ - mapped[m]);

		delta += mapped[m];
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

	assert(J <= (SIZE_MAX_ >> (N + 5)));
	assert(128 * delta <= SIZE_MAX_ - 49 * J);

	if ((J << (N + 5)) <= 128 * delta + 49 * J) {
		return (UINT32)(N - 2); /* k=N-2 */
	}

	/* k is the largest nonnegative integer less
	 * than or equal to N-2 such that ... */
	for (k = (UINT32)(N - 2); ; --k) {
		assert(J <= (SIZE_MAX_ >> (k + 7)));

		if ((J << (k + 7)) <= 128 * delta + 49 * J) {
			return k;
		}

		assert(k != 0 && "internal error");
	}

	assert(0 && "internal error");
}

/* adapt from heuristic_select_code_option_DC() */
static UINT32 heuristic_select_code_option_AC(struct bpe *bpe, size_t size, size_t N, size_t g)
{
	UINT32 *mapped_BitDepthAC_Block;

	assert(bpe != NULL);

	mapped_BitDepthAC_Block = bpe->mapped_BitDepthAC_Block;

	return heuristic_select_code_option(size, N, g, mapped_BitDepthAC_Block);
}

/* Section 4.3.2.11 b) heuristic procedure */
static UINT32 heuristic_select_code_option_DC(struct bpe *bpe, size_t size, size_t N, size_t g)
{
	UINT32 *mapped_quantized_dc;

	assert(bpe != NULL);

	mapped_quantized_dc = bpe->mapped_quantized_dc;

	return heuristic_select_code_option(size, N, g, mapped_quantized_dc);
}

static UINT32 optimum_select_code_option(size_t size, size_t N, size_t g, UINT32 *mapped)
{
	size_t i;
	int first = (g == 0);
	UINT32 k = 8; /* start with the largest possible k */
	size_t min_bits = SIZE_MAX_;
	UINT32 min_k;

	assert(mapped != NULL);

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
			size_t m = g * 16 + i;

			bits += bio_sizeof_gr(k, mapped[m]);
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
	if (min_bits == (size - (size_t)first) * N) {
		min_k = (UINT32)-1;
	}

	return min_k;
}

/* adapt from optimum_select_code_option_DC() */
static UINT32 optimum_select_code_option_AC(struct bpe *bpe, size_t size, size_t N, size_t g)
{
	UINT32 *mapped_BitDepthAC_Block;

	assert(bpe != NULL);

	mapped_BitDepthAC_Block = bpe->mapped_BitDepthAC_Block;

	return optimum_select_code_option(size, N, g, mapped_BitDepthAC_Block);
}

static UINT32 optimum_select_code_option_DC(struct bpe *bpe, size_t size, size_t N, size_t g)
{
	UINT32 *mapped_quantized_dc;

	assert(bpe != NULL);

	mapped_quantized_dc = bpe->mapped_quantized_dc;

	return optimum_select_code_option(size, N, g, mapped_quantized_dc);
}

/* adapt from the bpe_encode_segment_initial_coding_of_DC_coefficients_1st_step_gaggle() */
static int bpe_encode_segment_coding_of_AC_coefficients_1st_step_gaggle(struct bpe *bpe, size_t size, size_t N, size_t g)
{
	UINT32 k = (UINT32)-1; /* uncoded by default */
	UINT32 *bitDepthAC_Block;
	UINT32 *mapped_BitDepthAC_Block;
	int err;
	int first = (g == 0);

	assert(bpe != NULL);

	bitDepthAC_Block = bpe->bitDepthAC_Block;
	mapped_BitDepthAC_Block = bpe->mapped_BitDepthAC_Block;

	assert(first == 0 || bitDepthAC_Block != NULL);
	assert(mapped_BitDepthAC_Block != NULL);
	assert(size > 0);

	if (size == 1 && (size_t)first == 1) {
		dprint (("the gaggle consists of a single reference sample (J = 0)\n"));
	} else {
		switch (bpe->segment_header.OptACSelect) {
			case 0:
				k = heuristic_select_code_option_AC(bpe, size, N, g);
				break;
			case 1:
				k = optimum_select_code_option_AC(bpe, size, N, g);
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
		err = bio_write_bits(bpe->bio, (UINT32) bitDepthAC_Block[0], N);

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
			size_t m = g * 16 + i;

			/* 4.3.2.8 */
			assert(mapped_BitDepthAC_Block[m] < (1U << N));

			err = bio_write_bits(bpe->bio, mapped_BitDepthAC_Block[m], N);

			if (err) {
				return err;
			}
		}
	} else {
		size_t i;

		for (i = (size_t)first; i < size; ++i) {
			/* write mapped sample difference */
			size_t m = g * 16 + i;

			/* first part words */
			err = bio_write_gr_1st_part(bpe->bio, (size_t)k, mapped_BitDepthAC_Block[m]);

			if (err) {
				return err;
			}
		}

		for (i = (size_t)first; i < size; ++i) {
			/* write mapped sample difference */
			size_t m = g * 16 + i;

			/* second part words */
			err = bio_write_gr_2nd_part(bpe->bio, (size_t)k, mapped_BitDepthAC_Block[m]);

			if (err) {
				return err;
			}
		}
	}

	return RET_SUCCESS;
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
				k = heuristic_select_code_option_DC(bpe, size, N, g);
				break;
			case 1:
				k = optimum_select_code_option_DC(bpe, size, N, g);
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
			size_t m = g * 16 + i;

			dprint (("BPE(4.3.2.8): writing mapped_quantized_dc[%lu]\n", m));

			/* 4.3.2.8 */
			assert(mapped_quantized_dc[m] < (1U << N));

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
			size_t m = g * 16 + i;

			/* first part words */
			err = bio_write_gr_1st_part(bpe->bio, (size_t)k, mapped_quantized_dc[m]);

			if (err) {
				return err;
			}
		}

		for (i = (size_t)first; i < size; ++i) {
			/* write mapped sample difference */
			size_t m = g * 16 + i;

			/* second part words */
			err = bio_write_gr_2nd_part(bpe->bio, (size_t)k, mapped_quantized_dc[m]);

			if (err) {
				return err;
			}
		}
	}

	return RET_SUCCESS;
}

/* adapt from bpe_decode_segment_initial_coding_of_DC_coefficients_1st_step_gaggle() */
static int bpe_decode_segment_coding_of_AC_coefficients_1st_step_gaggle(struct bpe *bpe, size_t size, size_t N, size_t g)
{
	UINT32 k;
	UINT32 *bitDepthAC_Block;
	UINT32 *mapped_BitDepthAC_Block;
	int err;
	int first = (g == 0);

	assert(bpe != NULL);

	bitDepthAC_Block = bpe->bitDepthAC_Block;
	mapped_BitDepthAC_Block = bpe->mapped_BitDepthAC_Block;

	assert(first == 0 || bitDepthAC_Block != NULL);
	assert(mapped_BitDepthAC_Block != NULL);
	assert(size > 0);

	/* read code option */
	err = bio_read_dc_bits(bpe->bio, &k, code_option_length[N]);

	if (err) {
		return err;
	}

	if (k != (UINT32)-1) {
		k &= ((UINT32)1 << code_option_length[N]) - 1;
	}

	if (first) {
		/* first gaggle in a segment */

		/* N-bit reference */
		err = bio_read_bits(bpe->bio, (UINT32 *) &bitDepthAC_Block[0], N);

		if (err) {
			return err;
		}
	}

	if (k == (UINT32)-1) {
		/* UNCODED */
		size_t i;

		for (i = (size_t)first; i < size; ++i) {
			/* write mapped sample difference */
			size_t m = g * 16 + i;

			/* 4.3.2.8 */
			err = bio_read_bits(bpe->bio, &mapped_BitDepthAC_Block[m], N);

			assert(mapped_BitDepthAC_Block[m] < (1U << N));

			if (err) {
				return err;
			}
		}
	} else {
		size_t i;

		for (i = (size_t)first; i < size; ++i) {
			size_t m = g * 16 + i;

			/* first part words */
			err = bio_read_gr_1st_part(bpe->bio, (size_t)k, &mapped_BitDepthAC_Block[m]);

			if (err) {
				return err;
			}
		}

		for (i = (size_t)first; i < size; ++i) {
			size_t m = g * 16 + i;

			/* second part words */
			err = bio_read_gr_2nd_part(bpe->bio, (size_t)k, &mapped_BitDepthAC_Block[m]);

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
			size_t m = g * 16 + i;

			dprint (("BPE(4.3.2.8): reading mapped_quantized_dc[%lu]\n", m));

			/* 4.3.2.8 */
			err = bio_read_bits(bpe->bio, &mapped_quantized_dc[m], N);

			assert(mapped_quantized_dc[m] < (1U << N));

			if (err) {
				return err;
			}
		}
	} else {
		/* CODED Data Format for a Gaggle When a Coding Option Is Selected */
		size_t i;

		for (i = (size_t)first; i < size; ++i) {
			size_t m = g * 16 + i;

			/* first part words */
			err = bio_read_gr_1st_part(bpe->bio, (size_t)k, &mapped_quantized_dc[m]);

			if (err) {
				return err;
			}
		}

		for (i = (size_t)first; i < size; ++i) {
			size_t m = g * 16 + i;

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
		if (d_ < 0) assert(sign == -1);
		if (d_ > 0) assert(sign == +1);

#ifdef NDEBUG
		(void)sign;
#endif

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

static void map_ACs_to_mapped_ACs(struct bpe *bpe, size_t N)
{
	size_t S;
	UINT32 *bitDepthAC_Block;
	UINT32 *mapped_BitDepthAC_Block;
	size_t m;

	assert(bpe != NULL);

	S = bpe->S;
	bitDepthAC_Block = bpe->bitDepthAC_Block;
	mapped_BitDepthAC_Block = bpe->mapped_BitDepthAC_Block;

	assert(bitDepthAC_Block != NULL);
	assert(mapped_BitDepthAC_Block != NULL);

	assert(S > 0);
	assert(N > 1);

	/* for the remaining S-1 values, the difference between successive
	 * values shall be encoded */
	for (m = 1; m < S; ++m) {
		INT32 d_ = (INT32)bitDepthAC_Block[m] - (INT32)bitDepthAC_Block[m-1];
		UINT32 x_min = 0;
		UINT32 x_max = ((UINT32)1 << N) - 1;
		UINT32 theta = uint32_min(bitDepthAC_Block[m-1] - x_min, x_max - bitDepthAC_Block[m-1]);
		INT32 sign = bitDepthAC_Block[m-1] > (x_max - bitDepthAC_Block[m-1]) ? -1 : +1;

		assert(bitDepthAC_Block[m-1] <= x_max);

		mapped_BitDepthAC_Block[m] = map_quantized_dc(d_, theta, sign);
	}
}

/* 4.3.2.4 */
static void map_quantized_DCs_to_mapped_quantized_DCs(struct bpe *bpe, size_t N)
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
		INT32 sign = (UINT32)(quantized_dc[m-1] - x_min) > (UINT32)(x_max - quantized_dc[m-1]) ? -1 : +1; /* the sign when d' is outside [-theta;+theta] */

		/* NOTE see also CCSDS 121.0-B-2 */
		assert(quantized_dc[m] <= x_max);
		assert(quantized_dc[m] >= x_min);

		assert(quantized_dc[m-1] - x_min >= 0);
		assert(x_max - quantized_dc[m-1] >= 0);

		/* Each difference value ... shall be mapped to a non-negative integer ... */
		mapped_quantized_dc[m] = map_quantized_dc(d_, theta, sign); /* (19) = mapped quantized coefficients */
	}
}

/* adapt from map_mapped_quantized_DCs_to_quantized_DCs() */
static void map_mapped_ACs_to_ACs(struct bpe *bpe, size_t N)
{
	size_t S;
	UINT32 *bitDepthAC_Block;
	UINT32 *mapped_BitDepthAC_Block;
	size_t m;

	assert(bpe != NULL);

	S = bpe->S;
	bitDepthAC_Block = bpe->bitDepthAC_Block;
	mapped_BitDepthAC_Block = bpe->mapped_BitDepthAC_Block;

	assert(bitDepthAC_Block != NULL);
	assert(mapped_BitDepthAC_Block != NULL);

	assert(S > 0);
	assert(N > 1);

	for (m = 1; m < S; ++m) {
		UINT32 x_min = 0;
		UINT32 x_max = (1U << N) - 1;
		UINT32 theta = uint32_min(bitDepthAC_Block[m-1] - x_min, x_max - bitDepthAC_Block[m-1]);
		INT32 sign = (bitDepthAC_Block[m-1] - x_min) > (x_max - bitDepthAC_Block[m-1]) ? -1 : +1;

		assert(x_max >= bitDepthAC_Block[m-1]);

		bitDepthAC_Block[m] = (UINT32)inverse_map_quantized_dc(mapped_BitDepthAC_Block[m], theta, sign) + bitDepthAC_Block[m-1];

		assert(bitDepthAC_Block[m] <= x_max);
	}
}

static void map_mapped_quantized_DCs_to_quantized_DCs(struct bpe *bpe, size_t N)
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

/* Section 4.4 c) --> Sect. 4.3.2 */
int bpe_encode_segment_coding_of_AC_coefficients_1st_step(struct bpe *bpe)
{
	size_t bitDepthAC;
	size_t N;
	size_t S;
	size_t g, full_G, G;

	assert(bpe != NULL);

	bitDepthAC = (size_t) bpe->segment_header.BitDepthAC;

	N = uint32_ceil_log2(1 + (UINT32)bitDepthAC); /* Eq. (21) */

	/* bitDepthAC > 1 ==> N > 1 */
	assert(N > 1 && N <= 5);

	S = bpe->S;

	assert(S > 0);

	map_ACs_to_mapped_ACs(bpe, N);

	full_G = S / 16;
	G = (S + 15) / 16;

	for (g = 0; g < G; ++g) {
		size_t ge = (g < full_G) ? 16 : (S % 16);
		int err;

		err = bpe_encode_segment_coding_of_AC_coefficients_1st_step_gaggle(bpe, ge, N, g);

		if (err) {
			return err;
		}
	}

	return RET_SUCCESS;
}

/* Section 4.3.2 CODING QUANTIZED DC COEFFICIENTS */
int bpe_encode_segment_initial_coding_of_DC_coefficients_1st_step(struct bpe *bpe)
{
	size_t bitDepthDC;
	size_t N;
	INT32 *quantized_dc;
	size_t S;
	size_t q;

	assert(bpe != NULL);

	S = bpe->S;
	q = bpe->q;

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

		map_quantized_DCs_to_mapped_quantized_DCs(bpe, N);

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
int bpe_encode_segment_initial_coding_of_DC_coefficients_2nd_step(struct bpe *bpe)
{
	size_t bitDepthAC;
	size_t blk;
	size_t S;
	size_t q;

	assert(bpe != NULL);

	S = bpe->S;
	q = bpe->q;

	bitDepthAC = (size_t) bpe->segment_header.BitDepthAC;

	if (q > size_max(bitDepthAC, BitShift(bpe, DWT_LL2))) {
		/* 4.3.3.1 */
		size_t B = q - size_max(bitDepthAC, BitShift(bpe, DWT_LL2));
		size_t b;

		dprint (("BPE(4.3.3): encoding additional %lu bits\n", B));

		assert(B > 0);

		for (b = 0; b < B; ++b) {
			size_t p; /* bit plane */

			assert(q - 1 >= b);

			p = q - 1 - b;

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

/* adapt from bpe_decode_segment_initial_coding_of_DC_coefficients_1st_step() */
int bpe_decode_segment_coding_of_AC_coefficients_1st_step(struct bpe *bpe)
{
	size_t bitDepthAC;
	size_t N;
	size_t S;
	size_t g, full_G, G;

	assert(bpe != NULL);

	bitDepthAC = (size_t) bpe->segment_header.BitDepthAC;

	N = uint32_ceil_log2(1 + (UINT32)bitDepthAC); /* Eq. (21) */

	/* bitDepthAC > 1 ==> N > 1 */
	assert(N > 1 && N <= 5);

	S = bpe->S;

	assert(S > 0);

	full_G = S / 16;
	G = (S + 15) / 16;

	for (g = 0; g < G; ++g) {
		size_t ge = (g < full_G) ? 16 : (S % 16);
		int err;

		err = bpe_decode_segment_coding_of_AC_coefficients_1st_step_gaggle(bpe, ge, N, g);

		if (err) {
			return err;
		}
	}

	map_mapped_ACs_to_ACs(bpe, N);

	return RET_SUCCESS;
}

/* Section 4.3.2 CODING QUANTIZED DC COEFFICIENTS */
int bpe_decode_segment_initial_coding_of_DC_coefficients_1st_step(struct bpe *bpe)
{
	size_t bitDepthDC;
	size_t N;
	INT32 *quantized_dc;
	size_t S;
	size_t q;

	assert(bpe != NULL);

	S = bpe->S;
	q = bpe->q;

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
			unsigned char bit;

			assert(quantized_dc != NULL);

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

		map_mapped_quantized_DCs_to_quantized_DCs(bpe, N);
	}

	return RET_SUCCESS;
}

/* Section 4.3.3 ADDITIONAL BIT PLANES OF DC COEFFICIENTS */
int bpe_decode_segment_initial_coding_of_DC_coefficients_2nd_step(struct bpe *bpe)
{
	size_t blk;
	size_t S;
	size_t bitDepthAC;
	size_t q;

	assert(bpe != NULL);

	S = bpe->S;
	q = bpe->q;

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

	q = size_max(q_, BitShift(bpe, DWT_LL2)); /* LL3 in (15) */

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

	bpe->q = q;

	/* 4.3.1.5 Next, given a sequence of DC coefficients in a segment,
	 * the BPE shall compute quantized coefficients */

	quantized_dc = bpe->quantized_dc;

	assert(quantized_dc != NULL);

	for (blk = 0; blk < S; ++blk) {
		/* NOTE in general, DC coefficients are INT32 and can be negative */
		quantized_dc[blk] = *(bpe->segment + blk * BLOCK_SIZE) >> q; /* Eq. (16) */
	}

	/* NOTE Section 4.3.2 */
	err = bpe_encode_segment_initial_coding_of_DC_coefficients_1st_step(bpe);

	if (err) {
		return err;
	}

	/* NOTE Section 4.3.3 */
	err = bpe_encode_segment_initial_coding_of_DC_coefficients_2nd_step(bpe);

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

	bpe->q = q;

	/* 4.3.1.5 Next, given a sequence of DC coefficients in a segment,
	 * the BPE shall compute quantized coefficients */

	quantized_dc = bpe->quantized_dc;

	assert(quantized_dc != NULL);

	/* NOTE Section 4.3.2 */
	err = bpe_decode_segment_initial_coding_of_DC_coefficients_1st_step(bpe);

	if (err) {
		return err;
	}

	for (blk = 0; blk < S; ++blk) {
		*(bpe->segment + blk * BLOCK_SIZE) = quantized_dc[blk] << q; /* inverse of Eq. (16) */
	}

	/* NOTE Section 4.3.3 */
	err = bpe_decode_segment_initial_coding_of_DC_coefficients_2nd_step(bpe);

	if (err) {
		return err;
	}

	return RET_SUCCESS;
}

/* Section 4.4 */
int bpe_encode_segment_specifying_the_ac_bit_depth_in_each_block(struct bpe *bpe)
{
	UINT32 *bitDepthAC_Block;
	size_t bitDepthAC;
	size_t S;
	size_t m;

	assert(bpe != NULL);

	S = bpe->S;
	bitDepthAC_Block = bpe->bitDepthAC_Block;
	bitDepthAC = (size_t) bpe->segment_header.BitDepthAC;

	assert(bitDepthAC_Block != NULL);

	/* The first step in encoding AC coefficient magnitudes in a segment
	 * is to specify the sequence of BitDepthAC_Blockm values for
	 * the segment... bitDepthAC_Block[] */
	for (m = 0; m < S; ++m) {
		INT32 *block = bpe->segment + m * BLOCK_SIZE;

		size_t ac_bitsize = BitDepthAC_Block(block, 8);

		assert(sizeof(size_t) >= sizeof(UINT32));
		assert(ac_bitsize <= (size_t)UINT32_MAX_);

		bitDepthAC_Block[m] = (UINT32)ac_bitsize;
	}

	switch (bitDepthAC) {
			int err;
		case 0:
			/* cf. Sect. 4.4 a) */
			break;
		case 1:
			/* cf. Sect. 4.4 b) */
			for (m = 0; m < S; ++m) {
				int err = bio_put_bit(bpe->bio, (unsigned char)bitDepthAC_Block[m]);

				if (err) {
					return err;
				}
			}

			break;
		default:
			/* cf. Sect. 4.4 c) */
			err = bpe_encode_segment_coding_of_AC_coefficients_1st_step(bpe);

			if (err) {
				return err;
			}

			break;
	}

	return RET_SUCCESS;
}

/* Section 4.4 */
int bpe_decode_segment_specifying_the_ac_bit_depth_in_each_block(struct bpe *bpe)
{
	UINT32 *bitDepthAC_Block;
	size_t bitDepthAC;
	size_t S;
	size_t m;

	assert(bpe != NULL);

	S = bpe->S;
	bitDepthAC_Block = bpe->bitDepthAC_Block;
	bitDepthAC = (size_t) bpe->segment_header.BitDepthAC;

	assert(bitDepthAC_Block != NULL);

	switch (bitDepthAC) {
			int err;
		case 0:
			/* cf. Sect. 4.4 a) */
			for (m = 0; m < S; ++m) {
				bitDepthAC_Block[m] = 0;
			}
			break;
		case 1:
			/* cf. Sect. 4.4 b) */
			for (m = 0; m < S; ++m) {
				unsigned char b;
				int err = bio_get_bit(bpe->bio, &b);

				if (err) {
					return err;
				}

				bitDepthAC_Block[m] = (UINT32)b;
			}
			break;
		default:
			/* cf. Sect. 4.4 c) */

			err = bpe_decode_segment_coding_of_AC_coefficients_1st_step(bpe);

			if (err) {
				return err;
			}

			break;
	}

	return RET_SUCCESS;
}

int bpe_encode_segment_bit_plane_coding_stage0(struct bpe *bpe, size_t b)
{
	size_t S;
	size_t q;
	size_t m;
	size_t bitShift;

	assert(bpe != NULL);

	S = bpe->S;
	q = bpe->q;
	bitShift = BitShift(bpe, DWT_LL2);

	for (m = 0; m < S; ++m) {
		INT32 dc;
		unsigned char bit;
		int err;

		if (b >= q)
			continue;
		if (b < bitShift)
			continue;

		assert(bpe->segment != NULL);

		dc = *(bpe->segment + m * BLOCK_SIZE);

		/* b-th most significant bit of the two's-complement representation of the DC coefficient */

		/* NOTE per C89 standard, the right shift of negative signed type is implementation-defined */
		if (dc < 0) {
			assert( ~-1 == 0 );
			bit = ~(~dc >> b) & 1;
		} else {
			bit = (dc >> b) & 1;
		}

		err = bio_put_bit(bpe->bio, bit);

		if (err) {
			return err;
		}
	}

	return RET_SUCCESS;
}

int bpe_decode_segment_bit_plane_coding_stage0(struct bpe *bpe, size_t b)
{
	size_t S;
	size_t q;
	size_t m;
	size_t bitShift;

	assert(bpe != NULL);

	S = bpe->S;
	q = bpe->q;
	bitShift = BitShift(bpe, DWT_LL2);

	for (m = 0; m < S; ++m) {
		INT32 *dc;
		unsigned char bit;
		int err;

		if (b >= q)
			continue;
		if (b < bitShift)
			continue;

		assert(bpe->segment != NULL);

		dc = bpe->segment + m * BLOCK_SIZE;

		/* b-th most significant bit of the two's-complement representation of the DC coefficient */

		err = bio_get_bit(bpe->bio, &bit);

		if (err) {
			return err;
		}

		if (b < 32) {
			*dc |= (INT32)bit << b;
		} else {
			*dc = ~(*dc);
			*dc ^= M31;
		}
	}

	return RET_SUCCESS;
}

/* t_b: computes the type for the bitplane b */
static int query_type(struct bpe *bpe, UINT32 *magn, size_t b, int subband)
{
	UINT32 threshold;

	assert(b <= 32);

	threshold = (UINT32)1 << b;

	/* must be zero at this bit plane due to subband scaling */
	if (b < BitShift(bpe, subband))
		return -1;

	assert(magn != NULL);

	/* not significant */
	if (*magn < threshold)
		return 0;

	/* significant */
	if (*magn >= threshold && *magn < 2*threshold)
		return 1;

	/* was significant at some previous bitplane */
	if (*magn >= 2*threshold)
		return 2;

	assert(0 && "internal error");
}

/* Type 0 at the previous bit plane */
static int was_type0(int *type)
{
	assert(type != NULL);

	return *type == 0;
}

static void update_type(int *type, struct bpe *bpe, UINT32 *magn, size_t b, int subband)
{
	assert(type != NULL);

	*type = query_type(bpe, magn, b, subband);
}

static int is_significant(size_t b, UINT32 *magn)
{
	assert(magn != NULL);

	return (*magn >> b) & 1;
}

static void set_significance(size_t b, UINT32 *magn, int bit)
{
	assert(magn != NULL);

	*magn |= (UINT32)(bit & 1) << b;
}

static int get_sign(INT32 *sign)
{
	assert(sign != NULL);

	return *sign != 0;
}

static void set_sign(INT32 *sign, int bit)
{
	assert(sign != NULL);

	*sign = bit;
}

/* variable-length word */
struct vlw {
	UINT32 word;
	size_t size;
};

void vlw_init(struct vlw *vlw)
{
	assert(vlw != NULL);

	vlw->word = 0;
	vlw->size = 0;
}

void vlw_reset_after_read(struct vlw *vlw)
{
	assert(vlw != NULL);

	vlw->size = 0;
}

static void vlw_push_bit(int bit, struct vlw *vlw)
{
	assert(vlw != NULL);

	vlw->word |= (UINT32)(bit & 1) << vlw->size;

	vlw->size ++;
}

static int vlw_pop_bit(struct vlw *vlw)
{
	int bit;

	assert(vlw != NULL);

	bit = (vlw->word >> vlw->size) & 1;

	vlw->size ++;

	return bit;
}

static void stage1_encode_significance(size_t b, int *type, UINT32 *magn, struct vlw *vlw)
{
	if (was_type0(type)) {
		vlw_push_bit(is_significant(b, magn), vlw); /* magnitude bit */
	}
}

static void stage1_decode_significance(size_t b, int *type, UINT32 *magn, struct vlw *vlw)
{
	if (was_type0(type)) {
		set_significance(b, magn, vlw_pop_bit(vlw)); /* magnitude bit */
	}
}

static void stage1_decode_significance_stub(int *type, struct vlw *vlw)
{
	if (was_type0(type)) {
		assert(vlw != NULL);

		vlw->size ++; /* the encoder encoded the magnitude bit */
	}
}

static void stage1_encode_sign(size_t b, int *type, UINT32 *magn, INT32 *sign, struct vlw *vlw)
{
	if (was_type0(type) && is_significant(b, magn)) {
		vlw_push_bit(get_sign(sign), vlw); /* sign bit */
	}
}

static void stage1_decode_sign_stub(size_t b, int *type, UINT32 *magn, struct vlw *vlw)
{
	if (was_type0(type) && is_significant(b, magn)) {
		assert(vlw != NULL);

		vlw->size ++; /* the encoder encoded the sign bit */
	}
}

static void stage1_decode_sign(size_t b, int *type, UINT32 *magn, INT32 *sign, struct vlw *vlw)
{
	if (was_type0(type) && is_significant(b, magn)) {
		set_sign(sign, vlw_pop_bit(vlw));
	}
}

/* parent p_i | i : family number */
/* returns one of { DWT_HL2, DWT_LH2, DWT_HH2 } */
static int dwt_parent(int i)
{
	assert(i >= 0 && i < 3);

	return DWT_LL2 + 1 + i;
}

/* children group C_i | i : family number */
/* returns one of { DWT_HL1, DWT_LH1, DWT_HH1 }, then use stride = 4 to address the individial children */
static int dwt_child(int i)
{
	assert(i >= 0 && i < 3);

	return DWT_LL1 + 1 + i;
}

/* grandchildren group G_i | i : family number */
/* returns one of { DWT_HL0, DWT_LH0, DWT_HH0 }, then use stride = 2 to address the individial grandchildren */
static int dwt_grandchildren(int i)
{
	assert(i >= 0 && i < 3);

	return DWT_LL0 + 1 + i;
}

/* pointer to the first (top left) coefficient of the subband */
int *block_level_subband_int(int *block, size_t stride, int level, int subband)
{
	assert(block != NULL);
	assert(level >= 0 && level < 3);
	assert(subband >= DWT_LL && subband <= DWT_HH);

	switch (level) {
		case 2:
			switch (subband) {
				case DWT_LL: return block + 0*stride + 0; /* stride = 8, size = 1 */
				case DWT_HL: return block + 0*stride + 4; /* stride = 8, size = 1 */
				case DWT_LH: return block + 4*stride + 0; /* stride = 8, size = 1 */
				case DWT_HH: return block + 4*stride + 4; /* stride = 8, size = 1 */
			}
		case 1:
			switch (subband) {
				case DWT_LL: abort();
				case DWT_HL: return block + 0*stride + 2; /* stride = 4, size = 2 */
				case DWT_LH: return block + 2*stride + 0; /* stride = 4, size = 2 */
				case DWT_HH: return block + 2*stride + 2; /* stride = 4, size = 2 */
			}
		case 0:
			switch (subband) {
				case DWT_LL: abort();
				case DWT_HL: return block + 0*stride + 1; /* stride = 2, size = 4 */
				case DWT_LH: return block + 1*stride + 0; /* stride = 2, size = 4 */
				case DWT_HH: return block + 1*stride + 1; /* stride = 2, size = 4 */
			}
	}

	abort();
}

INT32 *block_level_subband_INT32(INT32 *block, size_t stride, int level, int subband)
{
	assert(block != NULL);
	assert(level >= 0 && level < 3);
	assert(subband >= DWT_LL && subband <= DWT_HH);

	switch (level) {
		case 2:
			switch (subband) {
				case DWT_LL: return block + 0*stride + 0; /* stride = 8, size = 1 */
				case DWT_HL: return block + 0*stride + 4; /* stride = 8, size = 1 */
				case DWT_LH: return block + 4*stride + 0; /* stride = 8, size = 1 */
				case DWT_HH: return block + 4*stride + 4; /* stride = 8, size = 1 */
			}
		case 1:
			switch (subband) {
				case DWT_LL: abort();
				case DWT_HL: return block + 0*stride + 2; /* stride = 4, size = 2 */
				case DWT_LH: return block + 2*stride + 0; /* stride = 4, size = 2 */
				case DWT_HH: return block + 2*stride + 2; /* stride = 4, size = 2 */
			}
		case 0:
			switch (subband) {
				case DWT_LL: abort();
				case DWT_HL: return block + 0*stride + 1; /* stride = 2, size = 4 */
				case DWT_LH: return block + 1*stride + 0; /* stride = 2, size = 4 */
				case DWT_HH: return block + 1*stride + 1; /* stride = 2, size = 4 */
			}
	}

	abort();
}

UINT32 *block_level_subband_UINT32(UINT32 *block, size_t stride, int level, int subband)
{
	assert(block != NULL);
	assert(level >= 0 && level < 3);
	assert(subband >= DWT_LL && subband <= DWT_HH);

	switch (level) {
		case 2:
			switch (subband) {
				case DWT_LL: return block + 0*stride + 0; /* stride = 8, size = 1 */
				case DWT_HL: return block + 0*stride + 4; /* stride = 8, size = 1 */
				case DWT_LH: return block + 4*stride + 0; /* stride = 8, size = 1 */
				case DWT_HH: return block + 4*stride + 4; /* stride = 8, size = 1 */
			}
		case 1:
			switch (subband) {
				case DWT_LL: abort();
				case DWT_HL: return block + 0*stride + 2; /* stride = 4, size = 2 */
				case DWT_LH: return block + 2*stride + 0; /* stride = 4, size = 2 */
				case DWT_HH: return block + 2*stride + 2; /* stride = 4, size = 2 */
			}
		case 0:
			switch (subband) {
				case DWT_LL: abort();
				case DWT_HL: return block + 0*stride + 1; /* stride = 2, size = 4 */
				case DWT_LH: return block + 1*stride + 0; /* stride = 2, size = 4 */
				case DWT_HH: return block + 1*stride + 1; /* stride = 2, size = 4 */
			}
	}

	abort();
}

/* pointer to the first (top left) coefficient of the subband */
int *block_subband_int(int *block, size_t stride, int subband_level)
{
	int subband = subband_level % 4;
	int level = subband_level / 4;

	assert(subband_level >= DWT_LL0 && subband_level <= DWT_HH2);

	return block_level_subband_int(block, stride, level, subband);
}

INT32 *block_subband_INT32(INT32 *block, size_t stride, int subband_level)
{
	int subband = subband_level % 4;
	int level = subband_level / 4;

	assert(subband_level >= DWT_LL0 && subband_level <= DWT_HH2);

	return block_level_subband_INT32(block, stride, level, subband);
}

UINT32 *block_subband_UINT32(UINT32 *block, size_t stride, int subband_level)
{
	int subband = subband_level % 4;
	int level = subband_level / 4;

	assert(subband_level >= DWT_LL0 && subband_level <= DWT_HH2);

	return block_level_subband_UINT32(block, stride, level, subband);
}

/* iterate through 4 children */
int t_max_B_C(int *type_C)
{
	size_t stride = 8;
	size_t x, y;

	int max = INT_MIN;

	assert(type_C != NULL);

	for (y = 0; y < 2; ++y) {
		for (x = 0; x < 2; ++x) {
			int *type_child = type_C + y*stride*4 + x*4;

			if (*type_child > max) {
				max = *type_child;
			}
		}
	}

	return max;
}

/* iterate through 16 grandchildren */
int t_max_B_G(int *type_G)
{
	size_t stride = 8;
	size_t x, y;

	int max = INT_MIN;

	assert(type_G != NULL);

	for (y = 0; y < 4; ++y) {
		for (x = 0; x < 4; ++x) {
			int *type_grandchild = type_G + y*stride*2 + x*2;

			if (*type_grandchild > max) {
				max = *type_grandchild;
			}
		}
	}

	return max;
}

/* t_max(D_i) */
int t_max_Di(int *type, int i)
{
	size_t stride = 8;
	int max = INT_MIN;

	/* 4 children */
	int Ci = dwt_child(i);
	int *type_Ci = block_subband_int(type, stride, Ci);
	int C_max = t_max_B_C(type_Ci);

	/* 16 grandchildren */
	int Gi = dwt_grandchildren(i);
	int *type_Gi = block_subband_int(type, stride, Gi);
	int G_max = t_max_B_G(type_Gi);

	if (C_max > max) {
		max = C_max;
	}

	if (G_max > max) {
		max = G_max;
	}

	return max;
}

/* t_max(B) */
int t_max_B(int *type)
{
	int i;
	int max = INT_MIN;

	/* for each coeff in B */
	for (i = 0; i < 3; ++i) {
		/* family i */

		int D_max = t_max_Di(type, i);

		if (D_max > max) {
			max = D_max;
		}
	}

	return max;
}

/* update type[] of parents according to magn[] at the bitplane b */
void update_parent_types(struct bpe *bpe, size_t b, int *type, UINT32 *magn)
{
	int i;
	size_t stride = 8;

	for (i = 0; i < 3; ++i) {
		int subband = dwt_parent(i);

		int *type_parent = block_subband_int(type, stride, subband);
		UINT32 *magn_parent = block_subband_UINT32(magn, stride, subband);

		update_type(type_parent, bpe, magn_parent, b, subband);
	}
}

void update_children_types(struct bpe *bpe, size_t b, int *type, UINT32 *magn)
{
	int i;
	size_t stride = 8;
	size_t x, y;

	for (i = 0; i < 3; ++i) {
		int subband = dwt_child(i);

		int *type_children = block_subband_int(type, stride, subband);
		UINT32 *magn_children = block_subband_UINT32(magn, stride, subband);

		for (y = 0; y < 2; ++y) {
			for (x = 0; x < 2; ++x) {
				int *type_child = type_children + y*stride*4 + x*4;
				UINT32 *magn_child = magn_children + y*stride*4 + x*4;

				update_type(type_child, bpe, magn_child, b, subband);
			}
		}
	}
}

/* Stage 1 (encode parents) on particular block */
int bpe_encode_segment_bit_plane_coding_stage1_block(struct bpe *bpe, size_t b, int *type, INT32 *sign, UINT32 *magn)
{
	int err;
	int i;
	size_t stride = 8;

	int *type_p[3];
	INT32 *sign_p[3];
	UINT32 *magn_p[3];

	/* variable-length words */
	struct vlw vlw_types_b_P; /* types_b[P] */
	struct vlw vlw_signs_b_P; /* signs_b[P] */

	vlw_init(&vlw_types_b_P);
	vlw_init(&vlw_signs_b_P);

	/* (p_i): the list of parents */
	for (i = 0; i < 3; ++i) {
		type_p[i] = block_subband_int(type, stride, dwt_parent(i));
		sign_p[i] = block_subband_INT32(sign, stride, dwt_parent(i));
		magn_p[i] = block_subband_UINT32(magn, stride, dwt_parent(i));
	}

	assert(bpe != NULL);

	/* update all of the AC coefficients in the block that were Type 0 at the previous bit plane */

	/* fill types_b[P] from magnitude bits */
	for (i = 0; i < 3; ++i) {
		stage1_encode_significance(b, type_p[i], magn_p[i], &vlw_types_b_P);
	}

	/* fill signs_b[P] from sign bits */
	for (i = 0; i < 3; ++i) {
		stage1_encode_sign(b, type_p[i], magn_p[i], sign_p[i], &vlw_signs_b_P);
	}

	/* FIXME: this should be entropy-encoded */
	/* send types_b[P] */
	err = bio_write_bits(bpe->bio, vlw_types_b_P.word, vlw_types_b_P.size);

	if (err) {
		return err;
	}

	/* send signs_b[P] */
	err = bio_write_bits(bpe->bio, vlw_signs_b_P.word, vlw_signs_b_P.size);

	if (err) {
		return err;
	}

	/* update types according to the just sent information */
	update_parent_types(bpe, b, type, magn);

	return RET_SUCCESS;
}

/* TODO */
/* Stage 2 (encode children) on particular block */
int bpe_encode_segment_bit_plane_coding_stage2_block(struct bpe *bpe, size_t b, int *type, INT32 *sign, UINT32 *magn)
{
	struct vlw vlw_tran_B;
	struct vlw vlw_tran_D;
	int old_t_max_B;
	int old_t_max_D[3];
	int i;

	vlw_init(&vlw_tran_B);
	vlw_init(&vlw_tran_D);

	dprint (("BPE(Stage 2): t_max(B)=%i t_max(D0)=%i t_max(D1)=%i t_max(D2)=%i\n", t_max_B(type), t_max_Di(type, 0), t_max_Di(type, 1), t_max_Di(type, 2)));

	assert(bpe != NULL);

	old_t_max_B = t_max_B(type);

	for (i = 0; i < 3; ++i) {
		old_t_max_D[i] = t_max_Di(type, i);
	}

	/* update types */
	update_children_types(bpe, b, type, magn);

	/* cf. 4.5.3.1.7 */

	/* as long, as the t_max_B(type) == 0, send tran_B;
	 * once the tranB becomes > 0, do not send anything (tran_B = null) */
	if (old_t_max_B == 0) {
		vlw_push_bit((t_max_B(type) != 0), &vlw_tran_B);
	}

	/* if the currently signaled tran_B > 0, send tran_D */
	if (t_max_B(type) > 0) {
		for (i = 0; i < 3; ++i) {
			if (old_t_max_D[i] == 0) {
				vlw_push_bit((t_max_Di(type, i) != 0), &vlw_tran_D);
			}
		}
	}

	return RET_SUCCESS;
}

/* encode parents */
int bpe_encode_segment_bit_plane_coding_stage1(struct bpe *bpe, size_t b)
{
	size_t S;
	size_t m;

	assert(bpe != NULL);

	S = bpe->S;

	/* for each block in the segment */
	for (m = 0; m < S; ++m) {
		int err;

		/* Stage 1 @ block[m] */
		int *type = bpe->type + m * BLOCK_SIZE; /* type at the previous bit plane */
		INT32 *sign = bpe->sign + m * BLOCK_SIZE;
		UINT32 *magn = bpe->magnitude + m * BLOCK_SIZE;

		err = bpe_encode_segment_bit_plane_coding_stage1_block(bpe, b, type, sign, magn);

		if (err) {
			return err;
		}
	}

	return RET_SUCCESS;
}

/* TODO */
/* encode children */
int bpe_encode_segment_bit_plane_coding_stage2(struct bpe *bpe, size_t b)
{
	size_t S;
	size_t m;

	assert(bpe != NULL);

	S = bpe->S;

	/* for each block in the segment */
	for (m = 0; m < S; ++m) {
		int err;

		/* Stage 1 @ block[m] */
		int *type = bpe->type + m * BLOCK_SIZE; /* type at the previous bit plane */
		INT32 *sign = bpe->sign + m * BLOCK_SIZE;
		UINT32 *magn = bpe->magnitude + m * BLOCK_SIZE;

		err = bpe_encode_segment_bit_plane_coding_stage2_block(bpe, b, type, sign, magn);

		if (err) {
			return err;
		}
	}

	return RET_SUCCESS;
}

int bpe_decode_segment_bit_plane_coding_stage1_block(struct bpe *bpe, size_t b, int *type, INT32 *sign, UINT32 *magn)
{
	int err;
	int i;
	size_t stride = 8;

	int *type_p[3];
	INT32 *sign_p[3];
	UINT32 *magn_p[3];

	/* variable-length words */
	struct vlw vlw_types_b_P; /* types_b[P] */
	struct vlw vlw_signs_b_P; /* signs_b[P] */

	vlw_init(&vlw_types_b_P);
	vlw_init(&vlw_signs_b_P);

	/* (p_i): the list of parents */
	for (i = 0; i < 3; ++i) {
		type_p[i] = block_subband_int(type, stride, dwt_parent(i));
		sign_p[i] = block_subband_INT32(sign, stride, dwt_parent(i));
		magn_p[i] = block_subband_UINT32(magn, stride, dwt_parent(i));
	}

	assert(bpe != NULL);

	/* update all of the AC coefficients in the block that were Type 0 at the previous bit plane */

	/* compute size of types_b[P] */
	for (i = 0; i < 3; ++i) {
		stage1_decode_significance_stub(type_p[i], &vlw_types_b_P);
	}

	/* FIXME: this should be entropy-encoded */
	/* receive types_b[P] */
	err = bio_read_bits(bpe->bio, &vlw_types_b_P.word, vlw_types_b_P.size);

	if (err) {
		return err;
	}

	vlw_reset_after_read(&vlw_types_b_P);

	/* set magnitude bits from types_b[P] */
	for (i = 0; i < 3; ++i) {
		stage1_decode_significance(b, type_p[i], magn_p[i], &vlw_types_b_P);
	}

	/* compute size of signs_b[P] */
	for (i = 0; i < 3; ++i) {
		stage1_decode_sign_stub(b, type_p[i], magn_p[i], &vlw_signs_b_P);
	}

	/* receive signs_b[P] */
	err = bio_read_bits(bpe->bio, &vlw_signs_b_P.word, vlw_signs_b_P.size);

	if (err) {
		return err;
	}

	vlw_reset_after_read(&vlw_signs_b_P);

	/* set sign bits from signs_b[P] */
	for (i = 0; i < 3; ++i) {
		stage1_decode_sign(b, type_p[i], magn_p[i], sign_p[i], &vlw_signs_b_P);
	}

	/* update types according to the currently indicated information */
	update_parent_types(bpe, b, type, magn);

	return RET_SUCCESS;
}

/* TODO */
int bpe_decode_segment_bit_plane_coding_stage2_block(struct bpe *bpe, size_t b, int *type, INT32 *sign, UINT32 *magn)
{
	struct vlw vlw_tran_B;

	vlw_init(&vlw_tran_B);

	dprint (("BPE(Stage 2): t_max(B)=%i t_max(D0)=%i t_max(D1)=%i t_max(D2)=%i\n", t_max_B(type), t_max_Di(type, 0), t_max_Di(type, 1), t_max_Di(type, 2)));

	assert(bpe != NULL);

	/* update types */
	update_children_types(bpe, b, type, magn);

	return RET_SUCCESS;}

/* decode parents */
int bpe_decode_segment_bit_plane_coding_stage1(struct bpe *bpe, size_t b)
{
	size_t S;
	size_t m;

	assert(bpe != NULL);

	S = bpe->S;

	for (m = 0; m < S; ++m) {
		int err;

		/* Stage 1 @ block[m] */
		int *type = bpe->type + m * BLOCK_SIZE; /* type at the previous bit plane */
		INT32 *sign = bpe->sign + m * BLOCK_SIZE;
		UINT32 *magn = bpe->magnitude + m * BLOCK_SIZE;

		err = bpe_decode_segment_bit_plane_coding_stage1_block(bpe, b, type, sign, magn);

		if (err) {
			return err;
		}
	}

	return RET_SUCCESS;
}

/* TODO */
/* decode children */
int bpe_decode_segment_bit_plane_coding_stage2(struct bpe *bpe, size_t b)
{
	size_t S;
	size_t m;

	assert(bpe != NULL);

	S = bpe->S;

	for (m = 0; m < S; ++m) {
		int err;

		/* Stage 1 @ block[m] */
		int *type = bpe->type + m * BLOCK_SIZE; /* type at the previous bit plane */
		INT32 *sign = bpe->sign + m * BLOCK_SIZE;
		UINT32 *magn = bpe->magnitude + m * BLOCK_SIZE;

		err = bpe_decode_segment_bit_plane_coding_stage2_block(bpe, b, type, sign, magn);

		if (err) {
			return err;
		}
	}

	return RET_SUCCESS;
}

/* set type to 0 (at the start of encoding/decoding) */
void block_type_reset(int *data, size_t stride)
{
	size_t y, x;

	assert(data != NULL);

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			data[y*stride + x] = 0;
		}
	}
}

/* convert AC coefficients in bpe->segment[] into sign-magnitude representation in bpe->sign[] & bpe->magnitude[] */
void block_magnitude_sign_get(INT32 *data, INT32 *sign, UINT32 *magnitude, size_t stride)
{
	size_t y, x;

	assert(data != NULL);
	assert(sign != NULL);
	assert(magnitude != NULL);

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			if (x == 0 && y == 0)
				continue;

			sign[y*stride + x] = data[y*stride + x] < 0;
			magnitude[y*stride + x] = uint32_abs(data[y*stride + x]);
		}
	}
}

/* reset AC sign-magnitudes at the start of decoding */
void block_magnitude_sign_reset(INT32 *sign, UINT32 *magnitude, size_t stride)
{
	size_t y, x;

	assert(sign != NULL);
	assert(magnitude != NULL);

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			if (x == 0 && y == 0)
				continue;

			sign[y*stride + x] = 0;
			magnitude[y*stride + x] = 0;
		}
	}
}

/* convert AC sign-magnitude representation into pbe->segment[] (after decoding) */
void block_magnitude_sign_set(INT32 *data, INT32 *sign, UINT32 *magnitude, size_t stride)
{
	size_t y, x;

	assert(data != NULL);
	assert(sign != NULL);
	assert(magnitude != NULL);

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			if (x == 0 && y == 0)
				continue;

			data[y*stride + x] = (sign[y*stride + x] ? -(INT32)1 : +(INT32)1) * (INT32)magnitude[y*stride + x];
		}
	}
}

/* Section 4.5 */
int bpe_encode_segment_bit_plane_coding(struct bpe *bpe)
{
	size_t bitDepthAC;
	size_t b_;
	size_t S;
	size_t m;

	assert(bpe != NULL);

	bitDepthAC = (size_t) bpe->segment_header.BitDepthAC;

	S = bpe->S;

	/* init encoding */
	for (m = 0; m < S; ++m) {
		int *block_type = bpe->type + m * BLOCK_SIZE;
		INT32 *block_coeff = bpe->segment + m * BLOCK_SIZE;
		INT32 *block_sign = bpe->sign + m * BLOCK_SIZE;
		UINT32 *block_magnitude = bpe->magnitude + m * BLOCK_SIZE;

		block_type_reset(block_type, 8);
		block_magnitude_sign_get(block_coeff, block_sign, block_magnitude, 8);
	}

	for (b_ = 0; b_ < bitDepthAC; ++b_) {
		size_t b = bitDepthAC - 1 - b_;
		int err;
		/* encode bit plane 'b' */

		dprint (("BPE(4.5) bit plane b = %lu\n", b));

		/* Stage 0 */
		err = bpe_encode_segment_bit_plane_coding_stage0(bpe, b);

		if (err) {
			return err;
		}

		/* TODO Stage 1 */
		err = bpe_encode_segment_bit_plane_coding_stage1(bpe, b);

		if (err) {
			return err;
		}

		if (b == bpe->segment_header.BitPlaneStop && bpe->segment_header.StageStop == 0) {
			break;
		}

		/* TODO Stage 2 */
		err = bpe_encode_segment_bit_plane_coding_stage2(bpe, b);

		if (err) {
			return err;
		}

		if (b == bpe->segment_header.BitPlaneStop && bpe->segment_header.StageStop == 1) {
			break;
		}

		/* TODO Stage 3 */

		if (b == bpe->segment_header.BitPlaneStop && bpe->segment_header.StageStop == 2) {
			break;
		}

		/* TODO Stage 4 */

		if (b == bpe->segment_header.BitPlaneStop && bpe->segment_header.StageStop == 3) {
			break;
		}
	}

	return RET_SUCCESS;
}

/* Section 4.5 */
int bpe_decode_segment_bit_plane_coding(struct bpe *bpe)
{
	size_t bitDepthAC;
	size_t b_;
	size_t S;
	size_t m;

	assert(bpe != NULL);

	bitDepthAC = (size_t) bpe->segment_header.BitDepthAC;

	S = bpe->S;

	/* init decoding */
	for (m = 0; m < S; ++m) {
		int *block_type = bpe->type + m * BLOCK_SIZE;
		INT32 *block_sign = bpe->sign + m * BLOCK_SIZE;
		UINT32 *block_magnitude = bpe->magnitude + m * BLOCK_SIZE;

		block_type_reset(block_type, 8);
		block_magnitude_sign_reset(block_sign, block_magnitude, 8);
	}

	for (b_ = 0; b_ < bitDepthAC; ++b_) {
		size_t b = bitDepthAC - 1 - b_;
		int err;
		/* encode bit plane 'b' */

		dprint (("BPE(4.5) bit plane b = %lu\n", b));

		/* Stage 0 */
		err = bpe_decode_segment_bit_plane_coding_stage0(bpe, b);

		if (err) {
			return err;
		}

		/* TODO Stage 1 */
		err = bpe_decode_segment_bit_plane_coding_stage1(bpe, b);

		if (err) {
			return err;
		}

		if (b == bpe->segment_header.BitPlaneStop && bpe->segment_header.StageStop == 0) {
			break;
		}

		/* TODO Stage 2 */
		err = bpe_decode_segment_bit_plane_coding_stage2(bpe, b);

		if (err) {
			return err;
		}

		if (b == bpe->segment_header.BitPlaneStop && bpe->segment_header.StageStop == 1) {
			break;
		}

		/* TODO Stage 3 */

		if (b == bpe->segment_header.BitPlaneStop && bpe->segment_header.StageStop == 2) {
			break;
		}

		/* TODO Stage 4 */

		if (b == bpe->segment_header.BitPlaneStop && bpe->segment_header.StageStop == 3) {
			break;
		}
	}

	/* after decoding */
	for (m = 0; m < S; ++m) {
		INT32 *block_coeff = bpe->segment + m * BLOCK_SIZE;
		INT32 *block_sign = bpe->sign + m * BLOCK_SIZE;
		UINT32 *block_magnitude = bpe->magnitude + m * BLOCK_SIZE;

		block_magnitude_sign_set(block_coeff, block_sign, block_magnitude, 8);
	}

	return RET_SUCCESS;
}

/* write segment into bitstream */
int bpe_encode_segment(struct bpe *bpe, int flush)
{
#if (DEBUG_ENCODE_BLOCKS == 1)
	size_t blk;
#endif
	int err;

	assert(bpe != NULL);

	dprint (("BPE: encoding segment %lu (%lu blocks)\n", bpe->segment_index, bpe->S));

	/* next block is not valid block (behind the image) */
	if (flush) {
		dprint (("BPE: the last segment indicated\n"));
		bpe->segment_header.EndImgFlag = 1;
		bpe->segment_header.Part3Flag = 1; /* signal new "S" */
	}

	bpe->segment_header.StartImgFlag = (bpe->block_index == 0);
	bpe->segment_header.SegmentCount = bpe->segment_index & M8;
	bpe->segment_header.BitDepthDC = (UINT32) BitDepthDC(bpe);
	bpe->segment_header.BitDepthAC = (UINT32) BitDepthAC(bpe);
	/* Part 2: */
	/* SegByteLimit */

	err = bpe_write_segment_header(bpe);

	if (err) {
		return err;
	}

	/* after writing of the first segment, set some flags to zero */
	bpe->segment_header.StartImgFlag = 0;
	bpe->segment_header.Part2Flag = 0;
	bpe->segment_header.Part3Flag = 0;
	bpe->segment_header.Part4Flag = 0;

	bpe->segment_index ++;

	/* Section 4.3 The initial coding of DC coefficients in a segment is performed in two steps. */
	bpe_encode_segment_initial_coding_of_DC_coefficients(bpe);

	if (bpe->segment_header.DCStop == 1) {
		dprint (("DCStop is set, stopping the encoding process\n"));

		return RET_SUCCESS;
	}

	/* Section 4.4 */
	err = bpe_encode_segment_specifying_the_ac_bit_depth_in_each_block(bpe);

	if (err) {
		return err;
	}

	/* Section 4.5 */
	err = bpe_encode_segment_bit_plane_coding(bpe);

	if (err) {
		return err;
	}

#if (DEBUG_ENCODE_BLOCKS == 1)
	for (blk = 0; blk < bpe->S; ++blk) {
		/* encode the block */
		bpe_encode_block(bpe->segment + blk * BLOCK_SIZE, 8, bpe->bio);
	}
#endif

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

#if (DEBUG_ENCODE_BLOCKS == 1)
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
#endif

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

		assert(bpe->segment_header.CodeWordLength < 8);

		if (lut_codeword_length[bpe->segment_header.CodeWordLength] > 32){
			return RET_FAILURE_FILE_UNSUPPORTED;
		}

		/* what about DWTtype, etc.? */
	}

	dprint (("BPE: decoding segment %lu (%lu blocks)\n", bpe->segment_index, S));

	bpe->segment_index ++;

#if (DEBUG_ENCODE_BLOCKS == 0)
	for (blk = 0; blk < S; ++blk) {
		bpe_zero_block(bpe->segment + blk * BLOCK_SIZE, 8);
	}
#endif

	err = bpe_decode_segment_initial_coding_of_DC_coefficients(bpe);

	if (err) {
		return err;
	}

	if (bpe->segment_header.DCStop == 1) {
		return RET_SUCCESS;
	}

	/* Section 4.4 */
	err = bpe_decode_segment_specifying_the_ac_bit_depth_in_each_block(bpe);

	if (err) {
		return err;
	}

	/* Section 4.5 */
	err = bpe_decode_segment_bit_plane_coding(bpe);

	if (err) {
		return err;
	}

#if (DEBUG_ENCODE_BLOCKS == 1)
	for (blk = 0; blk < S; ++blk) {
		/* decode the block */
		bpe_decode_block(bpe->segment + blk * BLOCK_SIZE, 8, bpe->bio);
	}
#endif

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

	assert(s < S);

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
