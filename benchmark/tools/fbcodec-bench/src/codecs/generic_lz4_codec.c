#include <stdlib.h>
#include <string.h>

#include "generic_lz4_codec.h"
#include "../lz4_util.h"

size_t generic_lz4_bound(uint32_t w, uint32_t h)
{
	return lz4u_compress_bound(rgb565_byte_size(w, h));
}

size_t generic_0step_encode(const uint16_t *src, uint32_t w, uint32_t h,
			     uint8_t *dst, size_t dst_cap)
{
	return lz4u_compress(src, rgb565_byte_size(w, h), dst, dst_cap);
}

int generic_0step_decode(const uint8_t *src, size_t src_len, uint16_t *dst,
			  uint32_t w, uint32_t h)
{
	size_t bytes = rgb565_byte_size(w, h);
	size_t out_len = 0;

	if (lz4u_decompress(src, src_len, dst, bytes, &out_len) != 0 ||
	    out_len != bytes)
		return -1;
	return 0;
}

size_t generic_1step_encode(const rgb565_transform *t, const uint16_t *src,
			     uint32_t w, uint32_t h, uint8_t *dst,
			     size_t dst_cap)
{
	size_t bytes = rgb565_byte_size(w, h);
	uint8_t *scratch = malloc(bytes ? bytes : 1);
	size_t n;

	if (!scratch)
		return (size_t)-1;
	t->fwd(src, w, h, scratch);
	n = lz4u_compress(scratch, bytes, dst, dst_cap);
	free(scratch);
	return n;
}

int generic_1step_decode(const rgb565_transform *t, const uint8_t *src,
			  size_t src_len, uint16_t *dst, uint32_t w,
			  uint32_t h)
{
	size_t bytes = rgb565_byte_size(w, h);
	uint8_t *scratch = malloc(bytes ? bytes : 1);
	size_t out_len = 0;

	if (!scratch)
		return -1;
	if (lz4u_decompress(src, src_len, scratch, bytes, &out_len) != 0 ||
	    out_len != bytes) {
		free(scratch);
		return -1;
	}
	t->inv(scratch, w, h, dst);
	free(scratch);
	return 0;
}

size_t generic_2step_encode(const rgb565_transform *t1,
			     const rgb565_transform *t2, const uint16_t *src,
			     uint32_t w, uint32_t h, uint8_t *dst,
			     size_t dst_cap)
{
	size_t bytes = rgb565_byte_size(w, h);
	uint8_t *a = malloc(bytes ? bytes : 1);
	uint8_t *b = malloc(bytes ? bytes : 1);
	size_t n;

	if (!a || !b) {
		free(a);
		free(b);
		return (size_t)-1;
	}
	t1->fwd(src, w, h, a);
	t2->fwd((const uint16_t *)a, w, h, b);
	n = lz4u_compress(b, bytes, dst, dst_cap);
	free(a);
	free(b);
	return n;
}

int generic_2step_decode(const rgb565_transform *t1,
			  const rgb565_transform *t2, const uint8_t *src,
			  size_t src_len, uint16_t *dst, uint32_t w,
			  uint32_t h)
{
	size_t bytes = rgb565_byte_size(w, h);
	uint8_t *a = malloc(bytes ? bytes : 1);
	uint8_t *b = malloc(bytes ? bytes : 1);
	size_t out_len = 0;
	int rc = 0;

	if (!a || !b) {
		free(a);
		free(b);
		return -1;
	}
	if (lz4u_decompress(src, src_len, b, bytes, &out_len) != 0 ||
	    out_len != bytes) {
		rc = -1;
		goto out;
	}
	t2->inv(b, w, h, (uint16_t *)a);
	t1->inv(a, w, h, dst);
out:
	free(a);
	free(b);
	return rc;
}
