/*
 * predict.h - reversible spatial predictor transforms for RGB565 buffers.
 *
 * Every transform below is a bijection on a width*height RGB565 buffer: the
 * forward ("fwd") direction always writes exactly width*height*2 bytes and
 * the inverse ("inv") direction reconstructs the original pixels exactly,
 * using well-defined modulo (wrapping) arithmetic so there is never any
 * signed-integer overflow or undefined behavior. All transforms reset their
 * prediction state at the start of every scanline (never predict across a
 * row boundary), so they remain correct for arbitrary widths and heights,
 * including 1xN / Nx1 / odd dimensions.
 */
#ifndef FBCODEC_PREDICT_H
#define FBCODEC_PREDICT_H

#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*predict_fwd_fn)(const uint16_t *src, uint32_t w, uint32_t h,
				uint8_t *dst);
typedef void (*predict_inv_fn)(const uint8_t *src, uint32_t w, uint32_t h,
				uint16_t *dst);

typedef struct {
	const char *name;
	predict_fwd_fn fwd;
	predict_inv_fn inv;
} rgb565_transform;

/* B1: byte-wise PNG-style "Sub" filter, two-byte (RGB565) pixel stride.
 * filtered[i] = original[i] - original[i-2] (mod 256) within each scanline;
 * the first pixel (first 2 bytes) of every row is preserved verbatim.
 */
void predict_sub_byte_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			   uint8_t *dst);
void predict_sub_byte_inv(const uint8_t *src, uint32_t w, uint32_t h,
			   uint16_t *dst);

/* B2: uint16 left-subtraction. residual[0] = pixel[0];
 * residual[x] = pixel[x] - pixel[x-1] (mod 65536), per scanline.
 */
void predict_u16_sub_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			  uint8_t *dst);
void predict_u16_sub_inv(const uint8_t *src, uint32_t w, uint32_t h,
			  uint16_t *dst);

/* B3: uint16 left-XOR. encoded[0] = pixel[0];
 * encoded[x] = pixel[x] XOR pixel[x-1], per scanline.
 */
void predict_u16_xor_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			  uint8_t *dst);
void predict_u16_xor_inv(const uint8_t *src, uint32_t w, uint32_t h,
			  uint16_t *dst);

/* B4a: byte-wise PNG Paeth predictor with bpp=2 (RGB565 pixel stride). */
void predict_paeth_byte_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			     uint8_t *dst);
void predict_paeth_byte_inv(const uint8_t *src, uint32_t w, uint32_t h,
			     uint16_t *dst);

/* B4b: RGB565-pixel Paeth variant, predicting each 16-bit pixel value as a
 * scalar (residual wrapped mod 65536). Cheaper than the per-channel variant
 * and still exactly reversible.
 */
void predict_paeth_pixel_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			      uint8_t *dst);
void predict_paeth_pixel_inv(const uint8_t *src, uint32_t w, uint32_t h,
			      uint16_t *dst);

/* B5: Median-Edge-Detector (JPEG-LS style) predictor, operated per channel
 * (R5/G6/B5 separated), residual wrapped modulo the channel's value range
 * and repacked back into RGB565 pixel shape.
 */
void predict_med_fwd(const uint16_t *src, uint32_t w, uint32_t h,
		      uint8_t *dst);
void predict_med_inv(const uint8_t *src, uint32_t w, uint32_t h,
		      uint16_t *dst);

/* Channel-aware left-subtraction: extract R5/G6/B5, left-subtract each
 * channel independently (mod its own range), repack into RGB565 shape.
 */
void predict_channel_sub_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			      uint8_t *dst);
void predict_channel_sub_inv(const uint8_t *src, uint32_t w, uint32_t h,
			      uint16_t *dst);

/* Channel-aware left-XOR: extract R5/G6/B5, left-XOR each channel
 * independently, repack into RGB565 shape.
 */
void predict_channel_xor_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			      uint8_t *dst);
void predict_channel_xor_inv(const uint8_t *src, uint32_t w, uint32_t h,
			      uint16_t *dst);

/* S1: RGB565 byte shuffle. Instead of lo0 hi0 lo1 hi1 ..., produce
 * lo0 lo1 lo2 ... hi0 hi1 hi2 ... across the *whole* buffer (not per
 * scanline -- this is a pure reordering, not a prediction).
 */
void predict_shuffle_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			  uint8_t *dst);
void predict_shuffle_inv(const uint8_t *src, uint32_t w, uint32_t h,
			  uint16_t *dst);

/* Looks up any of the above transforms by stable name, for composing
 * quantization + predictor combinations generically.
 */
const rgb565_transform *predict_find(const char *name);

/* Iterates the full transform table (used by unit tests). */
size_t predict_all(const rgb565_transform **out, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_PREDICT_H */
