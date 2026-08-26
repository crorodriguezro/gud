/*
 * temporal_codecs.c - T1 (previous-frame XOR), T2 (previous-frame
 * subtraction) and T3 (quantized temporal, closed-loop) candidates. See
 * transforms/temporal.h for the drift-free closed-loop design rationale.
 *
 * Default keyframe interval: every 120 frames (~2s at 60fps / ~4s at
 * 30fps), plus explicit reset() support used by the temporal validation
 * tests (dropped/reordered updates, forced resets, reconnects).
 */
#include "../codec.h"
#include "../transforms/temporal.h"

#define DEFAULT_KEYFRAME_INTERVAL 120

static void *xor_create(uint32_t w, uint32_t h)
{
	return temporal_create(w, h, TEMPORAL_MODE_XOR,
				DEFAULT_KEYFRAME_INTERVAL);
}

static void *sub_create(uint32_t w, uint32_t h)
{
	return temporal_create(w, h, TEMPORAL_MODE_SUB,
				DEFAULT_KEYFRAME_INTERVAL);
}

static void *quant_xor_create(uint32_t w, uint32_t h)
{
	return temporal_create(w, h, TEMPORAL_MODE_QUANT_XOR,
				DEFAULT_KEYFRAME_INTERVAL);
}

static void ctx_destroy(void *ctx)
{
	temporal_destroy((temporal_ctx *)ctx);
}

static void ctx_reset(void *ctx)
{
	temporal_reset((temporal_ctx *)ctx);
}

static size_t t_bound(uint32_t w, uint32_t h)
{
	return temporal_bound(w, h);
}

static size_t t_encode(void *ctx, const uint16_t *src, uint32_t w,
			uint32_t h, uint8_t *dst, size_t cap)
{
	return temporal_encode((temporal_ctx *)ctx, src, w, h, dst, cap);
}

static int t_decode(void *ctx, const uint8_t *src, size_t len, uint16_t *dst,
		     uint32_t w, uint32_t h)
{
	return temporal_decode((temporal_ctx *)ctx, src, len, dst, w, h);
}

const fbcodec_desc fbcodec_prev_xor_lz4 = {
	.name = "prev-xor-lz4",
	.label = "Previous-frame XOR + LZ4 (T1)",
	.category = "temporal",
	.is_lossy = false,
	.is_stateful = true,
	.native_rgb565 = true,
	.complexity = "high",
	.create = xor_create,
	.destroy = ctx_destroy,
	.reset = ctx_reset,
	.bound = t_bound,
	.encode = t_encode,
	.decode = t_decode,
};

const fbcodec_desc fbcodec_prev_sub_lz4 = {
	.name = "prev-sub-lz4",
	.label = "Previous-frame subtraction + LZ4 (T2)",
	.category = "temporal",
	.is_lossy = false,
	.is_stateful = true,
	.native_rgb565 = true,
	.complexity = "high",
	.create = sub_create,
	.destroy = ctx_destroy,
	.reset = ctx_reset,
	.bound = t_bound,
	.encode = t_encode,
	.decode = t_decode,
};

const fbcodec_desc fbcodec_prev_quant_xor_lz4 = {
	.name = "prev-quant-xor-lz4",
	.label = "Quantized temporal, closed-loop XOR + LZ4 (T3)",
	.category = "temporal",
	.is_lossy = true,
	.is_stateful = true,
	.native_rgb565 = true,
	.complexity = "high",
	.create = quant_xor_create,
	.destroy = ctx_destroy,
	.reset = ctx_reset,
	.bound = t_bound,
	.encode = t_encode,
	.decode = t_decode,
};
