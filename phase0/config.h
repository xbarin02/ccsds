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
 * 0 for convolution, 1 for multi-loop lifting, 2 for single-loop lifting
 */
#define CONFIG_DWT1_MODE 0

/*
 * 0 for separable mode, 1 for line-based mode, 2 for single-loop mode
 */
#define CONFIG_DWT2_MODE 0
