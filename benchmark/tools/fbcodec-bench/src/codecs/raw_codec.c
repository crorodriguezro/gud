/* raw_codec.c - A1 baseline: no compression at all. */
#include <string.h>

#include "../codec.h"

static size_t raw_bound(uint32_t w, uint32_t h)
{
	return rgb565_byte_size(w, h);
}

static size_t raw_encode(void *ctx, const uint16_t *src, uint32_t w,
			  uint32_t h, uint8_t *dst, size_t dst_cap)
{
	size_t bytes = rgb565_byte_size(w, h);

	(void)ctx;
	if (dst_cap < bytes)
		return (size_t)-1;
	memcpy(dst, src, bytes);
	return bytes;
}

static int raw_decode(void *ctx, const uint8_t *src, size_t src_len,
		       uint16_t *dst, uint32_t w, uint32_t h)
{
	size_t bytes = rgb565_byte_size(w, h);

	(void)ctx;
	if (src_len != bytes)
		return -1;
	memcpy(dst, src, bytes);
	return 0;
}

const fbcodec_desc fbcodec_raw = {
	.name = "raw",
	.label = "Raw RGB565 (no compression)",
	.category = "baseline",
	.is_lossy = false,
	.is_stateful = false,
	.native_rgb565 = true,
	.complexity = "low",
	.create = NULL,
	.destroy = NULL,
	.reset = NULL,
	.bound = raw_bound,
	.encode = raw_encode,
	.decode = raw_decode,
};
