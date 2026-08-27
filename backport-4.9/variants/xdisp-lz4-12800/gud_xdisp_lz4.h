/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __GUD_XDISP_LZ4_H__
#define __GUD_XDISP_LZ4_H__

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
#endif

/* Match upstream GUD's defensive descriptor allocation ceiling. */
#define GUD_XDISP_MAX_PAYLOAD_LIMIT (64U * 1024U * 1024U)

size_t gud_xdisp_lz4_upstream_workmem_size(void);

/*
 * Upstream-style exact-rectangle compression. A zero return means the whole
 * source did not fit and the caller must transmit that rectangle raw.
 */
size_t gud_xdisp_lz4_compress_limited(const u8 *source,
				      size_t source_length, u8 *destination,
				      size_t destination_capacity, void *workmem);

#endif
