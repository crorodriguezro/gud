/*
 * temporal.h - previous-frame (temporal) prediction transforms.
 *
 * These implement PROJECT SPEC section 8: T1 (previous-frame XOR), T2
 * (previous-frame subtraction) and T3 (quantized temporal, closed-loop).
 *
 * All three maintain a persistent per-stream reference frame both at the
 * "encoder" and "decoder" side. To guarantee the encoder and decoder never
 * drift apart (section 8's closed-loop requirement), the value that is
 * XORed/subtracted against the reference is *always* something fully
 * reconstructible from the bitstream alone:
 *   - T1/T2 are lossless, so the reconstructed frame equals the original
 *     source frame exactly; there is no quantization step to cause drift.
 *   - T3 first applies the *pixel* quantizer (quant_mild) to the source
 *     frame -- a function of the original pixel alone, not of the
 *     reference -- and then encodes the (already fully-known) quantized
 *     target against the reference with a lossless XOR + LZ4 step. Section
 *     27 explicitly allows prioritizing pixel quantization over residual
 *     quantization to avoid drift-prone closed-loop residual coding; that
 *     choice is documented here and in summary.md.
 *
 * Every stream begins with (and can be forced back to, via reset()) a
 * "keyframe": the reconstructed target is sent through unmodified (no XOR
 * against a - possibly absent or stale - reference).
 */
#ifndef FBCODEC_TEMPORAL_H
#define FBCODEC_TEMPORAL_H

#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum temporal_mode {
	TEMPORAL_MODE_XOR = 0,   /* T1 */
	TEMPORAL_MODE_SUB = 1,   /* T2 */
	TEMPORAL_MODE_QUANT_XOR = 2, /* T3 */
};

typedef struct {
	uint32_t width;
	uint32_t height;
	enum temporal_mode mode;
	uint32_t keyframe_interval; /* 0 = only the very first frame */
	uint32_t frame_index;
	bool have_reference;
	uint16_t *reference; /* last reconstructed frame */
} temporal_ctx;

temporal_ctx *temporal_create(uint32_t width, uint32_t height,
			       enum temporal_mode mode,
			       uint32_t keyframe_interval);
void temporal_destroy(temporal_ctx *ctx);
/* Forces the next encode()/decode() call to emit/expect a keyframe. */
void temporal_reset(temporal_ctx *ctx);

size_t temporal_bound(uint32_t width, uint32_t height);

/* header byte (0=delta,1=keyframe) + payload. dst_cap must be >= bound(). */
size_t temporal_encode(temporal_ctx *ctx, const uint16_t *src, uint32_t w,
			uint32_t h, uint8_t *dst, size_t dst_cap);
int temporal_decode(temporal_ctx *ctx, const uint8_t *src, size_t src_len,
		     uint16_t *dst, uint32_t w, uint32_t h);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_TEMPORAL_H */
