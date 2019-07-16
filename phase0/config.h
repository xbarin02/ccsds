/**
 * \file
 * \brief Global parameters
 */

/**
 * \brief Indicates the layout of multi-byte integers
 *
 * The macro should be set to 1 on little endian architectures.
 */
#define CONFIG_SWAP_BYTE_ORDER 1

/*
 * 0 for single-loop convolution, 1 for multi-loop lifting, 2 for single-loop lifting
 */
#define CONFIG_DWT1_MODE 2

/*
 * 0 for separable mode, 1 for line-based mode, 2 for single-loop mode
 */
#define CONFIG_DWT2_MODE 2

/*
 * 0 for processing transform levels sequentially, 1 for interleaving the individual levels in strips, 2 for interleaving the individual levels in blocks
 */
#define CONFIG_DWT_MS_MODE 2

/*
 * 0 for forward transform, 1 for inverse transform
 */
#define CONFIG_PERFTEST_DIR 0

/*
 * 0 for 4:3 aspect ratio, 1 for 1024 pixels wide strip, 2 for 16:9 ratio
 */
#define CONFIG_PERFTEST_TYPE 0

/*
 * number of measurement points
 */
#define CONFIG_PERFTEST_NUM 64

/*
 * 0 for Float DWT, 1 for Integer DWT
 */
#define CONFIG_PERFTEST_DWTTYPE 1
