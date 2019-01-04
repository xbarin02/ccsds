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

/**
 * \brief Release resources
 */
void frame_destroy(struct frame_t *frame);

/**
 * \brief Convert DWT from chunked layout to semiplanar layout
 * \sa \ref memoryLayouts
 */
int frame_convert_chunked_to_semiplanar(struct frame_t *frame);

/*! \page memoryLayouts Memory layouts
 *
 * Considering the discrete wavelet transform, there are several ways how
 * the wavelet coefficients can lie in memory. This also applies to
 * multiscale transform. The simplest choice is to store each subband at
 * each resolution (level) into a special memory region. This is called the
 * planar layout. The drawback of this solution is the need to hold a lot of
 * pointers in some suitable data structure. The second option is called the
 * semiplanar layout. This is the layout you see everywhere someone is
 * dealing with the DWT. The subbands at all resolutions are placed into
 * a single memory region. The four subbands occupy four image quadrands.
 * Usually, the LL subband is replaced with subbands on the coarser resolution
 * (next decomposition level). This is how the multiscale transform is
 * placed. The advantage is that one only needs one pointer. In both planar
 * and semiplanar layouts, the subband coefficients are adjacent in memory.
 * In other words, the subband coefficients lies close together even on
 * coarse resolutions. The disadvantage is that the transform cannot be
 * computed in place in a single loop. This is huge performance bottleneck.
 * Thus, the third layout leaves the coefficients in place of their original
 * spatial location. The four subbands become interleaved in memory.
 * This is called the chunked (interleaved) memory layout. Using this layout,
 * the transform can be computed in place in a single loop (single pass over
 * the image data). The drawback is that the coefficients at coarse levels
 * lie far apart. Fortunately, this is not a problem for the CCSDS standard
 * since it only uses three decomposition levels. Another drawback is that
 * the transform cannot be easily visuallized. To visuallize it, one needs to
 * convert the layout into the semiplanar layout first. Only then the
 * transform is meaningful for human observers.
 */

/**
 * \brief Duplicate the frame buffer
 */
int frame_clone(const struct frame_t *frame, struct frame_t *cloned_frame);

/**
 * \brief Debugging dump for frames in chunked memory layout
 *
 * Writes the content of \p frame into PGM file.
 *
 * \sa \ref memoryLayouts
 */
int frame_dump_chunked_as_semiplanar(const struct frame_t *frame, const char *path, int factor);

/**
 * \brief Compute and dump the mean squared error (MSE)
 */
int frame_dump_mse(const struct frame_t *frameA, const struct frame_t *frameB);

#endif /* FRAME_H_ */
