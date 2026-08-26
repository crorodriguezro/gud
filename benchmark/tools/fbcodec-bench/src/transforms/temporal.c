#include <stdlib.h>
#include <string.h>

#include "temporal.h"
#include "quant.h"
#include "../lz4_util.h"

temporal_ctx *temporal_create(uint32_t width, uint32_t height,
			       enum temporal_mode mode,
			       uint32_t keyframe_interval)
{
	temporal_ctx *ctx = calloc(1, sizeof(*ctx));

	if (!ctx)
		return NULL;
	ctx->width = width;
	ctx->height = height;
	ctx->mode = mode;
	ctx->keyframe_interval = keyframe_interval;
	ctx->frame_index = 0;
	ctx->have_reference = false;
	ctx->reference = calloc(rgb565_pixel_count(width, height),
				 sizeof(uint16_t));
	if (!ctx->reference) {
		free(ctx);
		return NULL;
	}
	return ctx;
}

void temporal_destroy(temporal_ctx *ctx)
{
	if (!ctx)
		return;
	free(ctx->reference);
	free(ctx);
}

void temporal_reset(temporal_ctx *ctx)
{
	ctx->have_reference = false;
	ctx->frame_index = 0;
}

size_t temporal_bound(uint32_t width, uint32_t height)
{
	return 1 + lz4u_compress_bound(rgb565_byte_size(width, height));
}

static bool is_keyframe_due(const temporal_ctx *ctx)
{
	if (!ctx->have_reference)
		return true;
	if (ctx->keyframe_interval == 0)
		return false;
	return (ctx->frame_index % ctx->keyframe_interval) == 0;
}

size_t temporal_encode(temporal_ctx *ctx, const uint16_t *src, uint32_t w,
			uint32_t h, uint8_t *dst, size_t dst_cap)
{
	size_t n = rgb565_pixel_count(w, h);
	uint16_t *target = malloc(n * sizeof(uint16_t)); /* quantized/raw target */
	uint16_t *delta = malloc(n * sizeof(uint16_t));
	size_t compressed;
	uint8_t keyframe;
	const uint16_t *payload;

	if (!target || !delta) {
		free(target);
		free(delta);
		return (size_t)-1;
	}

	if (ctx->mode == TEMPORAL_MODE_QUANT_XOR)
		quant_mild_fwd(src, w, h, (uint8_t *)target);
	else
		memcpy(target, src, n * sizeof(uint16_t));

	keyframe = is_keyframe_due(ctx) ? 1 : 0;
	if (keyframe) {
		payload = target;
	} else {
		size_t i;

		if (ctx->mode == TEMPORAL_MODE_SUB) {
			for (i = 0; i < n; i++)
				delta[i] = (uint16_t)(target[i] -
						       ctx->reference[i]);
		} else {
			for (i = 0; i < n; i++)
				delta[i] = (uint16_t)(target[i] ^
						       ctx->reference[i]);
		}
		payload = delta;
	}

	if (dst_cap < 1) {
		free(target);
		free(delta);
		return (size_t)-1;
	}
	dst[0] = keyframe;
	compressed = lz4u_compress(payload, n * sizeof(uint16_t), dst + 1,
				    dst_cap - 1);
	if (compressed == (size_t)-1) {
		free(target);
		free(delta);
		return (size_t)-1;
	}

	/* Closed loop: update the encoder-side reference with exactly what
	 * the decoder will reconstruct (see file header comment).
	 */
	memcpy(ctx->reference, target, n * sizeof(uint16_t));
	ctx->have_reference = true;
	ctx->frame_index++;

	free(target);
	free(delta);
	return compressed + 1;
}

int temporal_decode(temporal_ctx *ctx, const uint8_t *src, size_t src_len,
		     uint16_t *dst, uint32_t w, uint32_t h)
{
	size_t n = rgb565_pixel_count(w, h);
	uint16_t *payload = malloc(n * sizeof(uint16_t));
	size_t out_len = 0;
	uint8_t keyframe;

	if (!payload)
		return -1;
	if (src_len < 1) {
		free(payload);
		return -1;
	}
	keyframe = src[0];
	if (lz4u_decompress(src + 1, src_len - 1, payload,
			     n * sizeof(uint16_t), &out_len) != 0 ||
	    out_len != n * sizeof(uint16_t)) {
		free(payload);
		return -1;
	}

	if (keyframe) {
		memcpy(dst, payload, n * sizeof(uint16_t));
	} else {
		size_t i;

		if (ctx->mode == TEMPORAL_MODE_SUB) {
			for (i = 0; i < n; i++)
				dst[i] = (uint16_t)(payload[i] +
						     ctx->reference[i]);
		} else {
			for (i = 0; i < n; i++)
				dst[i] = (uint16_t)(payload[i] ^
						     ctx->reference[i]);
		}
	}

	memcpy(ctx->reference, dst, n * sizeof(uint16_t));
	ctx->have_reference = true;
	ctx->frame_index++;

	free(payload);
	return 0;
}
