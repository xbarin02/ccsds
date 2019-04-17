/**
 * \file
 * \brief Global parameters
 */

/*
 * int can be either 16-bit or 32-bit quantity
 */
#define CONFIG_HAS_INT32 1

/*
 * long can be either 32-bit or 64-bit quantity
 */
#define CONFIG_HAS_LONG64 1

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
#define CONFIG_PERFTEST_TYPE 2

/*
 * number of measurement points
 */
#define CONFIG_PERFTEST_NUM 64
