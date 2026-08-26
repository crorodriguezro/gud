/*
 * generic_lz4_codec.h - shared "apply 0/1/2 reversible transforms, then
 * LZ4" plumbing used by most spatial-predictor, quantization, shuffle and
 * combination candidates. See predict.h and quant.h for the individual
 * transforms; this file only owns the LZ4 composition glue so it is not
 * duplicated ~20 times.
 */
#ifndef FBCODEC_GENERIC_LZ4_CODEC_H
#define FBCODEC_GENERIC_LZ4_CODEC_H

#include "../transforms/predict.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t generic_lz4_bound(uint32_t w, uint32_t h);

size_t generic_0step_encode(const uint16_t *src, uint32_t w, uint32_t h,
			     uint8_t *dst, size_t dst_cap);
int generic_0step_decode(const uint8_t *src, size_t src_len, uint16_t *dst,
			  uint32_t w, uint32_t h);

size_t generic_1step_encode(const rgb565_transform *t, const uint16_t *src,
			     uint32_t w, uint32_t h, uint8_t *dst,
			     size_t dst_cap);
int generic_1step_decode(const rgb565_transform *t, const uint8_t *src,
			  size_t src_len, uint16_t *dst, uint32_t w,
			  uint32_t h);

size_t generic_2step_encode(const rgb565_transform *t1,
			     const rgb565_transform *t2, const uint16_t *src,
			     uint32_t w, uint32_t h, uint8_t *dst,
			     size_t dst_cap);
int generic_2step_decode(const rgb565_transform *t1,
			  const rgb565_transform *t2, const uint8_t *src,
			  size_t src_len, uint16_t *dst, uint32_t w,
			  uint32_t h);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_GENERIC_LZ4_CODEC_H */
