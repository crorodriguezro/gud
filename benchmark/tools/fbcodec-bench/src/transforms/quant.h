/*
 * quant.h - deterministic RGB565 channel-aware quantization.
 *
 * All masks operate on the actual RGB565 bit fields (RRRRR GGGGGG BBBBB),
 * never on arbitrary low bits of the raw uint16_t. See PROJECT SPEC
 * section 26. Three quality levels are provided:
 *
 *   Q1 mild      -> retains R4 G5 B4 (~1 bit/channel dropped), 8192 colors
 *   Q2 moderate  -> retains R4 G4 B4 ("RGB444"), 4096 colors
 *   Q3 aggressive-> retains R3 G3 B2 ("RGB332"), 256 colors, 1 byte/pixel
 *
 * Q1/Q2 keep 16-bit RGB565 storage (quantize-in-place, same buffer size);
 * Q2 additionally offers a packed 12-bits-per-pixel representation and Q3
 * is inherently an 8-bit-per-pixel packed representation.
 */
#ifndef FBCODEC_QUANT_H
#define FBCODEC_QUANT_H

#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	const char *name;
	int r_bits;
	int g_bits;
	int b_bits;
	uint32_t color_count;
	double effective_bpp; /* bits per pixel if packed tightly */
	double stored_bpp;    /* bits per pixel actually stored by this mode */
} quant_mode_info;

extern const quant_mode_info quant_mild_info;     /* Q1 */
extern const quant_mode_info quant_moderate_info; /* Q2, 16-bit storage */
extern const quant_mode_info quant_moderate_packed_info; /* Q2 packed 12bpp */
extern const quant_mode_info quant_aggressive_info;      /* Q3, 8bpp packed */

/* Q1 mild: clear the low bit of R and B (5-bit fields) and the low bit of G
 * (6-bit field). Same-size transform (16-bit storage preserved); "inv" is a
 * no-op copy because the quantized value is already the reconstructed
 * pixel -- this deliberately demonstrates that raw bytes/pixel do not
 * decrease unless something further (LZ4, packing) is applied.
 */
void quant_mild_fwd(const uint16_t *src, uint32_t w, uint32_t h,
		     uint8_t *dst);
void quant_identity_inv(const uint8_t *src, uint32_t w, uint32_t h,
			 uint16_t *dst);

/* Q2 moderate ("RGB444"): retain R4 G4 B4, 16-bit storage. */
void quant_moderate_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			 uint8_t *dst);

/* Q2 packed: pack two R4G4B4 pixels into 3 bytes (12 bits/pixel). Handles
 * odd pixel counts by padding the final unpaired pixel with zero.
 * dst capacity must be quant_rgb444_packed_bound(w,h).
 */
size_t quant_rgb444_packed_bound(uint32_t w, uint32_t h);
size_t quant_rgb444_pack(const uint16_t *src, uint32_t w, uint32_t h,
			  uint8_t *dst);
void quant_rgb444_unpack(const uint8_t *src, uint32_t w, uint32_t h,
			  uint16_t *dst);

/* Q3 aggressive ("RGB332"): 1 byte/pixel, R3 G3 B2 packed into a single
 * byte: bits [7:5]=R3 [4:2]=G3 [1:0]=B2.
 */
size_t quant_rgb332_bound(uint32_t w, uint32_t h);
size_t quant_rgb332_pack(const uint16_t *src, uint32_t w, uint32_t h,
			   uint8_t *dst);
void quant_rgb332_unpack(const uint8_t *src, uint32_t w, uint32_t h,
			   uint16_t *dst);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_QUANT_H */
