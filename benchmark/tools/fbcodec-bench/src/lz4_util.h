/*
 * lz4_util.h - thin wrapper around the vendored upstream LZ4 block
 * compressor (vendor/lz4/lz4.c, pinned to the same 1.10.0 revision already
 * embedded in the gud.ko kernel driver, see
 * backport-4.9/variants/xdisp-lz4-12800/UPSTREAM.md for provenance).
 *
 * This is used both as the A2 baseline codec and as the second stage of
 * every "<transform> + LZ4" candidate.
 */
#ifndef FBCODEC_LZ4_UTIL_H
#define FBCODEC_LZ4_UTIL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t lz4u_compress_bound(size_t input_size);

/* Returns compressed size, or (size_t)-1 on failure. */
size_t lz4u_compress(const void *src, size_t src_len, void *dst,
		      size_t dst_cap);

/* Returns 0 on success (and writes *out_len), nonzero on failure. */
int lz4u_decompress(const void *src, size_t src_len, void *dst,
		     size_t dst_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_LZ4_UTIL_H */
