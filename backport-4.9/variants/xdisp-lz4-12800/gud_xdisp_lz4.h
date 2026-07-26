/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __GUD_XDISP_LZ4_H__
#define __GUD_XDISP_LZ4_H__

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

/*
 * Gate E qualified complete 12,800-byte host transfers on the Pi.  This
 * diagnostic must never rely on a shorter FunctionFS read to split a larger
 * host request: every submitted bulk URB is bounded here instead.
 */
#define GUD_XDISP_PAYLOAD_LIMIT 12800U
#define GUD_XDISP_LZ4_WORKMEM_SIZE 16384U

struct gud_xdisp_chunk {
	u32 rows;
	size_t source_length;
	size_t payload_length;
	bool compressed;
};

size_t gud_xdisp_lz4_compress_bound(size_t source_length);

/*
 * Returns a raw LZ4 block length, or zero when the source cannot be encoded
 * within destination_capacity.  The destination is never written past that
 * capacity.
 */
size_t gud_xdisp_lz4_compress(const u8 *source, size_t source_length,
			      u8 *destination, size_t destination_capacity,
			      void *workmem);

/*
 * Plan one complete-row rectangle.  scratch_capacity must accommodate the
 * compression bound for max_rows * bytes_per_line.  On success, scratch holds
 * the compressed payload when chunk->compressed is true.  A raw chunk is read
 * directly from source by the caller.
 */
int gud_xdisp_plan_chunk(const u8 *source, u32 remaining_rows,
			 size_t bytes_per_line, u32 max_rows,
			 size_t payload_limit, void *workmem,
			 u8 *scratch, size_t scratch_capacity,
			 struct gud_xdisp_chunk *chunk);

u32 gud_xdisp_next_row_hint(u32 selected_rows, u32 absolute_max_rows);

#endif
