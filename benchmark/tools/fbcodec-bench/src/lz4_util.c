#include "lz4_util.h"
#include "../vendor/lz4/lz4.h"

size_t lz4u_compress_bound(size_t input_size)
{
	int bound = LZ4_compressBound((int)input_size);

	if (bound <= 0)
		return 0;
	return (size_t)bound;
}

size_t lz4u_compress(const void *src, size_t src_len, void *dst,
		      size_t dst_cap)
{
	int n;

	if (src_len == 0)
		return 0;
	n = LZ4_compress_default((const char *)src, (char *)dst,
				  (int)src_len, (int)dst_cap);
	if (n <= 0)
		return (size_t)-1;
	return (size_t)n;
}

int lz4u_decompress(const void *src, size_t src_len, void *dst,
		     size_t dst_cap, size_t *out_len)
{
	int n;

	if (src_len == 0) {
		*out_len = 0;
		return 0;
	}
	n = LZ4_decompress_safe((const char *)src, (char *)dst, (int)src_len,
				 (int)dst_cap);
	if (n < 0)
		return -1;
	*out_len = (size_t)n;
	return 0;
}
