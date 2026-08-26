/*
 * rle.h - simple PackBits-style run-length preprocessing (PROJECT SPEC
 * section 10). Applied to the raw RGB565 byte stream *before* LZ4, to
 * measure whether explicit long-run handling adds anything LZ4 does not
 * already capture on its own (flat UI backgrounds are the target case).
 *
 * Encoding is classic PackBits over the byte stream:
 *   control byte c in [0,127]   -> literal run of (c+1) bytes follows
 *   control byte c in [129,255] -> repeat the next single byte (257-c) times
 *   control byte 128            -> unused (never emitted)
 */
#ifndef FBCODEC_RLE_H
#define FBCODEC_RLE_H

#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t rle_bound(size_t input_len);
size_t rle_encode(const uint8_t *src, size_t src_len, uint8_t *dst,
		   size_t dst_cap);
int rle_decode(const uint8_t *src, size_t src_len, uint8_t *dst,
		size_t dst_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_RLE_H */
