#include "bpe.h"
#include <assert.h>
#include <stdlib.h>

#define M2 3
#define M3 7
#define M4 15
#define M5 31
#define M8 255
#define M20 1048575
#define M27 134217727

static const unsigned char lut_codeword_length[8] = {
	8, 40, 16, 48, 24, 56, 32, 64
};

int bpe_init(struct bpe *bpe, const struct parameters *parameters, struct bio *bio, struct frame *frame)
{
	int i;
	int err;

	assert(bpe);
	assert(parameters);

	bpe->segment = NULL;

	bpe->bio = bio;

	bpe->block_index = 0;

	bpe->frame = frame;

	/* init Segment Header */

	assert(bpe->frame);

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
	bpe->segment_header.OptDCSelect = 0; /* 0 => heuristic selection of k parameter */
	bpe->segment_header.OptACSelect = 0; /* 0 => heuristic selection of k parameter */
	bpe->segment_header.DWTtype = parameters->DWTtype;
	bpe->segment_header.ExtendedPixelBitDepthFlag = 0; /* 0 => pixel bit depth is not larger than 16 */
	bpe->segment_header.SignedPixels = 0; /* 0 => unsigned */
	bpe->segment_header.PixelBitDepth = (UINT32)bpe->frame->bpp; /* the input pixel bit depth */
	bpe->segment_header.ImageWidth = (UINT32)bpe->frame->width;
	bpe->segment_header.TransposeImg = 0;
	bpe->segment_header.CodeWordLength = 6; /* 6 => 32-bit coded words */
	bpe->segment_header.CustomWtFlag = 0; /* no custom weights */

	for (i = 0; i < 12; ++i) {
		bpe->segment_header.weight[i] = parameters->weight[i];
	}

	err = bpe_realloc_segment(bpe);

	if (err) {
		return err;
	}

	return RET_SUCCESS;
}

/* the S has been changed, realloc bpe->segment[] */
int bpe_realloc_segment(struct bpe *bpe)
{
	size_t S;

	assert(bpe);

	S = (size_t) bpe->segment_header.S;

	free(bpe->segment);

	bpe->segment = malloc( S * 8 * 8 * sizeof(INT32) );

	if (bpe->segment == NULL && S != 0) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	bpe->S = S;

	return RET_SUCCESS;
}

/* the ImageWidth has been changed, realloc bpe->frame */
int bpe_realloc_frame_width(struct bpe *bpe)
{
	int err;

	assert(bpe);
	assert(bpe->frame);

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

	/* what about bpp, DWTtype, etc.? */

	return RET_SUCCESS;
}

/* increase frame height (+8 rows) */
int bpe_increase_frame_height(struct bpe *bpe)
{
	int err;

	assert(bpe);
	assert(bpe->frame);

	bpe->frame->height += 8; /* FIXME ??? */

	err = frame_realloc_data(bpe->frame);

	if (err) {
		return err;
	}

	return RET_SUCCESS;
}

int bpe_destroy(struct bpe *bpe)
{
	assert(bpe);

	free(bpe->segment);

	return RET_SUCCESS;
}

int bpe_encode_block(INT32 *data, size_t stride, struct bio *bio)
{
	size_t y, x;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			bio_write_int(bio, (UINT32) data[y*stride + x]); /* FIXME INT32 -> UINT32 */
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

	assert(bpe);

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

	assert(bpe);

	word |= SET_UINT_INTO_UINT32(bpe->segment_header.PadRows, 0, M3); /* Number of ‘padding’ rows to delete after inverse DWT */
	/*  +3 Reserved : 5 */

	return bio_write_bits(bpe->bio, word, 8);
}

/* Part 2 (5 bytes) -- Optional */
int bpe_write_segment_header_part2(struct bpe *bpe)
{
	UINT32 word = 0;
	int err;

	assert(bpe);

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

	assert(bpe);

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

	assert(bpe);

	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.DWTtype, 0);
	/* Reserved : 1 */
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.ExtendedPixelBitDepthFlag, 2);
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.SignedPixels, 3);
	word |= SET_BOOL_INTO_UINT32(bpe->segment_header.PixelBitDepth, 4);
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
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[ 3] - 1,  1, M2); /* CustomWtHH1 */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[ 1] - 1,  3, M2); /* CustomWtHL1 */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[ 2] - 1,  5, M2); /* CustomWtLH1 */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[ 7] - 1,  7, M2); /* CustomWtHH2 */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[ 5] - 1,  9, M2); /* CustomWtHL2 */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[ 6] - 1, 11, M2); /* CustomWtLH2 */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[11] - 1, 13, M2); /* CustomWtHH3 */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[ 9] - 1, 15, M2); /* CustomWtHL3 */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[10] - 1, 17, M2); /* CustomWtLH3 */
		word |= SET_UINT_INTO_UINT32(bpe->segment_header.weight[ 8] - 1, 19, M2); /* CustomWtLL3 */
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

	assert(bpe);

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

	assert(bpe);

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

	assert(bpe);

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

	assert(bpe);

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

	assert(bpe);

	err = bio_read_bits(bpe->bio, &word, 32);

	if (err) {
		return err;
	}

	bpe->segment_header.DWTtype = GET_BOOL_FROM_UINT32(word, 0);
	/* Reserved : 1 */
	bpe->segment_header.ExtendedPixelBitDepthFlag = GET_BOOL_FROM_UINT32(word, 2); /* NOTE bpp has been changed */
	bpe->segment_header.SignedPixels = GET_BOOL_FROM_UINT32(word, 3);
	bpe->segment_header.PixelBitDepth = GET_BOOL_FROM_UINT32(word, 4); /* NOTE bpp has been changed */
	bpe->segment_header.ImageWidth = GET_UINT_FROM_UINT32(word, 8, M20); /* NOTE width has been changed ==> reallocate frame->data[] */
	bpe->segment_header.TransposeImg = GET_BOOL_FROM_UINT32(word, 28);
	bpe->segment_header.CodeWordLength = GET_UINT_FROM_UINT32(word, 29, M3);

	err = bio_read_bits(bpe->bio, &word, 32);

	if (err) {
		return err;
	}

	bpe->segment_header.CustomWtFlag = GET_BOOL_FROM_UINT32(word, 0);
	if (bpe->segment_header.CustomWtFlag) {
		bpe->segment_header.weight[ 3] = 1 + (int)GET_UINT_FROM_UINT32(word,  1, M2); /* CustomWtHH1 */
		bpe->segment_header.weight[ 1] = 1 + (int)GET_UINT_FROM_UINT32(word,  3, M2); /* CustomWtHL1 */
		bpe->segment_header.weight[ 2] = 1 + (int)GET_UINT_FROM_UINT32(word,  5, M2); /* CustomWtLH1 */
		bpe->segment_header.weight[ 7] = 1 + (int)GET_UINT_FROM_UINT32(word,  7, M2); /* CustomWtHH2 */
		bpe->segment_header.weight[ 5] = 1 + (int)GET_UINT_FROM_UINT32(word,  9, M2); /* CustomWtHL2 */
		bpe->segment_header.weight[ 6] = 1 + (int)GET_UINT_FROM_UINT32(word, 11, M2); /* CustomWtLH2 */
		bpe->segment_header.weight[11] = 1 + (int)GET_UINT_FROM_UINT32(word, 13, M2); /* CustomWtHH3 */
		bpe->segment_header.weight[ 9] = 1 + (int)GET_UINT_FROM_UINT32(word, 15, M2); /* CustomWtHL3 */
		bpe->segment_header.weight[10] = 1 + (int)GET_UINT_FROM_UINT32(word, 17, M2); /* CustomWtLH3 */
		bpe->segment_header.weight[ 8] = 1 + (int)GET_UINT_FROM_UINT32(word, 19, M2); /* CustomWtLL3 */
	}
	/* +21 Reserved : 11 */

	return RET_SUCCESS;
}

int bpe_write_segment_header(struct bpe *bpe)
{
	size_t S;
	int err;

	assert(bpe);

	S = bpe->S;

	bpe->segment_header.StartImgFlag = (bpe->block_index == S);
	bpe->segment_header.SegmentCount = ( (bpe->block_index - 1) / S ) & M8;
	/* TODO EndImgFlag */
	/* BitDepthDC */
	/* BitDepthAC */
	/* Part 2: */
	/* SegByteLimit */

	if (bpe->segment_header.StartImgFlag) {
		bpe->segment_header.Part2Flag = 1;
		bpe->segment_header.Part3Flag = 1;
		bpe->segment_header.Part4Flag = 1;
	}

	/* Segment Header Part 1A (mandatory) */
	bpe_write_segment_header_part1a(bpe);

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

	dprint (("BPE :: Segment Header has been written\n"));

	return RET_SUCCESS;
}

int bpe_read_segment_header(struct bpe *bpe)
{
	/* Segment Header Part 1A (mandatory) */
	bpe_read_segment_header_part1a(bpe);

	/* Segment Header Part 1B */
	if (bpe->segment_header.EndImgFlag != 0) {
		bpe_read_segment_header_part1b(bpe);
	}

	/* Segment Header Part 2 */
	if (bpe->segment_header.Part2Flag) {
		bpe_read_segment_header_part2(bpe);
	}

	/* Segment Header Part 3 */
	if (bpe->segment_header.Part3Flag) {
		bpe_read_segment_header_part3(bpe);
		/* NOTE S has been changed */
	}

	/* Segment Header Part 4 */
	if (bpe->segment_header.Part4Flag) {
		bpe_read_segment_header_part4(bpe);
		/* NOTE bpp & width have been changed */
	}

	return 0;
}

int bpe_encode_segment(struct bpe *bpe, size_t total_no_blocks)
{
	size_t S;
	size_t s;
	size_t blk;

	assert(bpe);

	S = bpe->S;
	s = bpe->block_index % S;

	if (s == 0) {
		s = S;
	}

	/* next block is not valid block (behind the image) */
	if (bpe->block_index >= total_no_blocks) {
		dprint (("BPE: encoding segment %lu (%lu blocks), next block starts at %lu <-- the last segment\n", ((bpe->block_index-1) / S), s, bpe->block_index));
		bpe->segment_header.EndImgFlag = 1;
		if (s != S) {
			bpe->segment_header.Part3Flag = 1; /* signal new "s" as "S" */
			bpe->segment_header.S = (UINT32) s;
		}
	} else {
		dprint (("BPE: encoding segment %lu (%lu blocks), next block starts at %lu\n", ((bpe->block_index - 1) / S), s, bpe->block_index));
	}

	bpe_write_segment_header(bpe);

	for (blk = 0; blk < s; ++blk) {
		/* encode the block */
		bpe_encode_block(bpe->segment + blk*8*8, 8, bpe->bio);
	}

	/* after writing of the first segment, set some flags to zero */
	bpe->segment_header.StartImgFlag = 0;
	bpe->segment_header.Part2Flag = 0;
	bpe->segment_header.Part3Flag = 0;
	bpe->segment_header.Part4Flag = 0;

	return RET_SUCCESS;
}

int bpe_decode_block(INT32 *data, size_t stride, struct bio *bio)
{
	size_t y, x;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			bio_read_int(bio, (UINT32 *) &data[y*stride + x]); /* FIXME UINT32 -> INT32 */
		}
	}

	return RET_SUCCESS;
}

int bpe_decode_segment(struct bpe *bpe, size_t total_no_blocks)
{
	size_t S;
	size_t s;
	size_t blk;

	assert(bpe);

	S = bpe->S;

	s = 0;
	if (S) {
		s = bpe->block_index % S;
	}

	if (s == 0) {
		s = S;
	}

	/* correct the number of block in the last segment */
	if (bpe->block_index + S > total_no_blocks) {
		dprint (("shortened segment (%lu blocks)\n", total_no_blocks - bpe->block_index));
		s = total_no_blocks - bpe->block_index;
	}

	if (S) {
		dprint (("BPE: decoding segment %lu (%lu blocks)\n", (bpe->block_index / S), s));
	} else {
		dprint (("BPE: decoding segment zero (%lu blocks)\n", s));
	}

	bpe_read_segment_header(bpe);

	dprint (("BPE :: Segment Header :: StartImgFlag = %i\n", bpe->segment_header.StartImgFlag));
	dprint (("BPE :: Segment Header :: EndImgFlag   = %i\n", bpe->segment_header.EndImgFlag));
	dprint (("BPE :: Segment Header :: Part3Flag    = %i\n", bpe->segment_header.Part3Flag));
	dprint (("BPE :: Segment Header :: Part4Flag    = %i\n", bpe->segment_header.Part4Flag));
	dprint (("BPE :: Segment Header :: S            = %lu\n", bpe->segment_header.S));

	if (bpe->segment_header.Part3Flag /*&& bpe->S != bpe->segment_header.S*/) {
		dprint (("BPE:: S changed from %lu to %lu ==> reallocate\n", bpe->S, bpe->segment_header.S));
		bpe_realloc_segment(bpe);
		S = bpe->S;
		s = 0;
		if (S) {
			s = bpe->block_index % S;
		}
		if (s == 0) {
			s = S;
		}
	}

	if (bpe->segment_header.Part4Flag && bpe->frame->width != bpe->segment_header.ImageWidth) {
		dprint (("BPE: bpp changed\n"));
		dprint (("BPE: width changed ==> reallocate\n"));
		bpe_realloc_frame_width(bpe);
		abort();
	}

	for (blk = 0; blk < s; ++blk) {
		/* decode the block */
		bpe_decode_block(bpe->segment + blk*8*8, 8, bpe->bio);
	}

	return RET_SUCCESS;
}

int bpe_push_block(struct bpe *bpe, INT32 *data, size_t stride, size_t total_no_blocks)
{
	size_t S;
	size_t s;
	INT32 *local;
	size_t y, x;

	assert(bpe);

	/* push block into bpe->segment[] at the index (block_index%S) */

	S = bpe->S;
	s = bpe->block_index % S;
	local = bpe->segment + s*8*8;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			local[y*8 + x] = data[y*stride + x];
		}
	}

	/* next block will be... */
	bpe->block_index ++;

	s = bpe->block_index % S;

	/* if this is the last block in the segment, serialize the segment into bpe->bio */
	if (s == 0) {
		/* encode this segment */
		bpe_encode_segment(bpe, total_no_blocks);
	}

	return RET_SUCCESS;
}

int bpe_pop_block(struct bpe *bpe, INT32 *data, size_t stride, size_t total_no_blocks)
{
	size_t S;
	size_t s;
	INT32 *local;
	size_t y, x;

	S = bpe->S;

	s = 0;
	if (S) {
		s = bpe->block_index % S;
	}

	/* if this is the first block in the segment, deserialize it */
	if (s == 0) {
		bpe_decode_segment(bpe, total_no_blocks);
	}

	/* pop the block from bpe->segment[] */
	local = bpe->segment + s*8*8;

	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 8; ++x) {
			data[y*stride + x] = local[y*8 + x];
		}
	}

	bpe->block_index ++;

	return RET_SUCCESS;
}

int bpe_flush(struct bpe *bpe, size_t total_no_blocks)
{
	size_t S;
	size_t s;

	S = bpe->S;
	s = bpe->block_index % S;

	if (s > 0) {
		/* encode the last (incomplete) segment */
		bpe_encode_segment(bpe, total_no_blocks);
	}

	return RET_SUCCESS;
}

size_t get_total_no_blocks(struct frame *frame)
{
	size_t height, width;

	assert(frame);

	height = ceil_multiple8(frame->height);
	width  = ceil_multiple8(frame->width);

	return height / 8 * width / 8;
}

struct block {
	int *data;
	size_t stride;
};

int block_by_index(struct block *block, struct frame *frame, size_t block_index)
{
	size_t width;
	size_t y, x;
	int *data;

	assert(frame);

	width  = ceil_multiple8(frame->width);

	y = block_index / (width / 8) * 8;
	x = block_index % (width / 8) * 8;

	data = frame->data;

	assert(block);

	block->data = data + y*width + x;
	block->stride = width;

	return RET_SUCCESS;
}

int bpe_encode_block_by_index(struct frame *frame, struct bio *bio, size_t block_index)
{
	struct block block;

	block_by_index(&block, frame, block_index);

	return bpe_encode_block(block.data, block.stride, bio);
}

int bpe_decode_block_by_index(struct frame *frame, struct bio *bio, size_t block_index)
{
	struct block block;

	block_by_index(&block, frame, block_index);

	return bpe_decode_block(block.data, block.stride, bio);
}

int bpe_encode(struct frame *frame, const struct parameters *parameters, struct bio *bio)
{
	size_t block_index;
	size_t total_no_blocks;
	struct bpe bpe;

	assert(frame);

	/* HACK
	 * until the library is fully implemented, write the image
	 * dimensions at the beginning of the bitstream
	 */
	bio_write_int(bio, (UINT32) frame->width);
	bio_write_int(bio, (UINT32) frame->height);
	bio_write_int(bio, (UINT32) frame->bpp);

	total_no_blocks = get_total_no_blocks(frame);

	bpe_init(&bpe, parameters, bio, frame);

	/* push all blocks into the BPE engine */
	for (block_index = 0; block_index < total_no_blocks; ++block_index) {
		struct block block;

		block_by_index(&block, frame, block_index);

		bpe_push_block(&bpe, block.data, block.stride, total_no_blocks);
	}

	bpe_flush(&bpe, total_no_blocks);

	bpe_destroy(&bpe);

	return RET_SUCCESS;
}

int bpe_decode(struct frame *frame, const struct parameters *parameters, struct bio *bio)
{
	size_t block_index;
	size_t total_no_blocks;
	struct bpe bpe;

	assert(frame);

	/* HACK
	 * until the library is fully implemented, read the image
	 * dimensions from the beginning of the bitstream
	 */
	bio_read_int(bio, (UINT32 *) &frame->width);
	bio_read_int(bio, (UINT32 *) &frame->height);
	bio_read_int(bio, (UINT32 *) &frame->bpp);

	total_no_blocks = get_total_no_blocks(frame);

	bpe_init(&bpe, parameters, bio, frame);

	/* initially allocate bpe->segment[] according to initial S */
	bpe_realloc_segment(&bpe);

	dprint (("main decoding loop...\n"));

	/* push all blocks into the BPE engine */
	for (block_index = 0; block_index < total_no_blocks; ++block_index) {
		struct block block;

		block_by_index(&block, frame, block_index);

		bpe_pop_block(&bpe, block.data, block.stride, total_no_blocks);
	}

	bpe_destroy(&bpe);

	return RET_SUCCESS;
}

size_t get_maximum_stream_size(struct frame *frame)
{
	size_t width, height;

	assert(frame);

	width = ceil_multiple8(frame->width);
	height = ceil_multiple8(frame->height);

	return height * width * sizeof(int) + 20 * get_total_no_blocks(frame) + 4096;
}
