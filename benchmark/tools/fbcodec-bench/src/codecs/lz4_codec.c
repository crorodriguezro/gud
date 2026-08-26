/* lz4_codec.c - A2 baseline: RGB565 + LZ4, the current production candidate. */
#include "../codec.h"
#include "generic_lz4_codec.h"

static size_t codec_bound(uint32_t w, uint32_t h)
{
	return generic_lz4_bound(w, h);
}

static size_t codec_encode(void *ctx, const uint16_t *src, uint32_t w,
			    uint32_t h, uint8_t *dst, size_t dst_cap)
{
	(void)ctx;
	return generic_0step_encode(src, w, h, dst, dst_cap);
}

static int codec_decode(void *ctx, const uint8_t *src, size_t src_len,
			 uint16_t *dst, uint32_t w, uint32_t h)
{
	(void)ctx;
	return generic_0step_decode(src, src_len, dst, w, h);
}

const fbcodec_desc fbcodec_rgb565_lz4 = {
	.name = "rgb565-lz4",
	.label = "RGB565 + LZ4 (baseline)",
	.category = "baseline",
	.is_lossy = false,
	.is_stateful = false,
	.native_rgb565 = true,
	.complexity = "low",
	.create = NULL,
	.destroy = NULL,
	.reset = NULL,
	.bound = codec_bound,
	.encode = codec_encode,
	.decode = codec_decode,
};
