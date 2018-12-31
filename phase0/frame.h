/**
 * \file frame.h
 * \brief Reading and writing image files to/from framebuffer
 */
#ifndef FRAME_H_
#define FRAME_H_

#include <stddef.h>

/**
 * \brief Framebuffer holding either image or wavelet coefficients
 *
 * The \c width and \c height are exact image dimensions, i.e. not rounded to next multiple of eight.
 * However the \c data is a buffer having dimensions to be multiples of eight.
 */
struct frame_t {
	size_t width;  /**< \brief number of columns, range [17; 1<<20] */
	size_t height; /**< \brief number of rows, range [17; infty) */
	size_t bpp;    /**< \brief pixel bit depth (valid in image domain, not in transform domain) */

	int *data;     /**< \brief framebuffer */
};

/**
 * \brief Save an image in PGM format
 *
 * The function writes the image in \p frame to the file specified by \c path.
 * Currently, only binary PGM (P5) is supported.
 * If \p path is \c "-", the output is written to the \c stdout.
 */
int frame_save_pgm(const struct frame_t *frame, const char *path);

/**
 * \brief Load image from PGM file
 *
 * The function loads an image from the file specified by \c path.
 * Currently, only binary PGM (P5) is supported.
 * If \p path is \c "-", the input is read from the \c stdin.
 */
int frame_load_pgm(struct frame_t *frame, const char *path);

/**
 * \brief Debugging dump
 *
 * Writes the content of \p frame into PGM format.
 */
int frame_dump(const struct frame_t *frame, const char *path, int factor);

#endif /* FRAME_H_ */
