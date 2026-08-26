/*
 * rle_codec.c - simple PackBits-style RLE preprocessing + LZ4 (PROJECT SPEC
 * section 10). Operates on the raw RGB565 byte stream.
 */
#include <stdlib.h>

#include "../codec.h"
#include "../lz4_util.h"
#include "../transforms/rle.h"

static size_t rle_lz4_bound(uint32_t w, uint32_t h)
{
	return lz4u_compress_bound(rle_bound(rgb565_byte_size(w, h)));
}

static size_t rle_lz4_encode(void *ctx, const uint16_t *src, uint32_t w,
			      uint32_t h, uint8_t *dst, size_t cap)
{
	size_t bytes = rgb565_byte_size(w, h);
	size_t bound = rle_bound(bytes);
	uint8_t *scratch = malloc(bound ? bound : 1);
	size_t rle_len, n;

	(void)ctx;
	if (!scratch)
		return (size_t)-1;
	rle_len = rle_encode((const uint8_t *)src, bytes, scratch, bound);
	if (rle_len == (size_t)-1) {
		free(scratch);
		return (size_t)-1;
	}
	n = lz4u_compress(scratch, rle_len, dst, cap);
	free(scratch);
	return n;
}

static int rle_lz4_decode(void *ctx, const uint8_t *src, size_t len,
			   uint16_t *dst, uint32_t w, uint32_t h)
{
	size_t bytes = rgb565_byte_size(w, h);
	size_t bound = rle_bound(bytes);
	uint8_t *scratch = malloc(bound ? bound : 1);
	size_t lz4_out_len = 0;
	size_t final_len = 0;
	int rc = 0;

	(void)ctx;
	if (!scratch)
		return -1;
	if (lz4u_decompress(src, len, scratch, bound, &lz4_out_len) != 0) {
		rc = -1;
		goto out;
	}
	if (rle_decode(scratch, lz4_out_len, (uint8_t *)dst, bytes,
		       &final_len) != 0 ||
	    final_len != bytes) {
		rc = -1;
		goto out;
	}
out:
	free(scratch);
	return rc;
}

const fbcodec_desc fbcodec_rle_lz4 = {
	.name = "rle-lz4",
	.label = "PackBits-style RLE + LZ4",
	.category = "rle",
	.is_lossy = false,
	.is_stateful = false,
	.native_rgb565 = true,
	.complexity = "low",
	.bound = rle_lz4_bound,
	.encode = rle_lz4_encode,
	.decode = rle_lz4_decode,
};
