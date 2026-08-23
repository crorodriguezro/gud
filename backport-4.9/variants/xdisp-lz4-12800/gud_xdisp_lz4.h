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
#define GUD_XDISP_DEFAULT_PAYLOAD_LIMIT 12800U
#define GUD_XDISP_MAX_PAYLOAD_LIMIT (4U * 1024U * 1024U)
#define GUD_XDISP_PAYLOAD_LIMIT GUD_XDISP_DEFAULT_PAYLOAD_LIMIT
#define GUD_XDISP_TARGET_PAYLOAD_PERCENT 95U
#define GUD_XDISP_DISCOVERY_TARGET_PAYLOAD \
	(GUD_XDISP_DEFAULT_PAYLOAD_LIMIT * GUD_XDISP_TARGET_PAYLOAD_PERCENT / 100U)

#ifdef __KERNEL__
extern unsigned int gud_xdisp_payload_limit;
#endif

struct gud_xdisp_chunk {
	u32 rows;
	size_t source_length;
	size_t payload_length;
	bool compressed;
	u32 compression_attempts;
	u32 rejected_compression_attempts;
	u64 compression_source_bytes;
	bool predictive_hit;
	bool predictive_fallback;
};

/*
 * Per-frame state for the bounded-output policy. It is intentionally local
 * to one atomic update: raw fallback avoids more wide discovery only until
 * the next frame gets a new chance to compress.
 */
struct gud_xdisp_bounded_frame {
	bool raw_backoff;
	/* A miss disables predictive probing for this frame only. */
	bool predictive_cooldown;
};

size_t gud_xdisp_lz4_compress_bound(size_t source_length);

u32 gud_xdisp_raw_rows(u32 remaining_rows, size_t bytes_per_line,
			       u32 max_rows, size_t payload_limit);

/*
 * Modern upstream LZ4 bounded-output compressor, embedded privately in
 * gud.ko.  It consumes no more than *source_length bytes and returns the
 * exact consumed prefix through that argument.  Callers must round that
 * prefix down to a complete source row before a GUD SET_BUFFER submission.
 */
size_t gud_xdisp_lz4_upstream_workmem_size(void);
size_t gud_xdisp_lz4_compress_dest_size(const u8 *source,
					 size_t *source_length,
					 u8 *destination,
					 size_t destination_capacity,
					 void *workmem);

/*
 * Returns a raw LZ4 block length, or zero when the source cannot be encoded
 * within destination_capacity.  The destination is never written past that
 * capacity.
 */
size_t gud_xdisp_lz4_compress(const u8 *source, size_t source_length,
			      u8 *destination, size_t destination_capacity,
			      void *workmem);

/*
 * Compress one exact source rectangle with a strict output limit. Unlike the
 * bounded-prefix discovery helper, success means the complete input was
 * encoded. It is used by the test-only predictive fast path; callers still
 * validate the returned payload before USB submission.
 */
size_t gud_xdisp_lz4_compress_limited(const u8 *source,
			      size_t source_length, u8 *destination,
			      size_t destination_capacity, void *workmem);

/*
 * Plan one complete-row rectangle.  scratch_capacity must accommodate the
 * compression bound for max_rows * bytes_per_line.  On success, scratch holds
 * the compressed payload when chunk->compressed is true.  A raw chunk is read
 * directly from source by the caller.  The compression counters include every
 * candidate processed while choosing the returned rectangle, including
 * candidates rejected for exceeding payload_limit or not reducing the source.
 */
int gud_xdisp_plan_chunk(const u8 *source, u32 remaining_rows,
			 size_t bytes_per_line, u32 max_rows,
			 size_t payload_limit, void *workmem,
			 u8 *scratch, size_t scratch_capacity,
			 struct gud_xdisp_chunk *chunk);

/*
 * One-pass bounded discovery. It asks upstream LZ4 for the largest source
 * prefix fitting a target below the transport cap, rounds it down to complete
 * rows, then validates that exact rectangle against payload_limit. For
 * incompressible content it chooses the largest raw complete-row rectangle.
 */
int gud_xdisp_plan_chunk_bounded(const u8 *source, u32 remaining_rows,
				 size_t bytes_per_line, u32 max_rows,
				 size_t payload_limit, void *workmem,
				 u8 *scratch, size_t scratch_capacity,
				 struct gud_xdisp_chunk *chunk);

/*
 * Bounded discovery with frame-local incompressible backoff. The first raw
 * fallback records frame->raw_backoff; remaining chunks in that frame are
 * direct cap-safe raw rows with zero compression work. The caller must create
 * a fresh frame state for every new framebuffer update.
 */
int gud_xdisp_plan_chunk_bounded_frame(
				 const u8 *source, u32 remaining_rows,
				 size_t bytes_per_line, u32 max_rows,
				 size_t payload_limit, void *workmem,
				 u8 *scratch, size_t scratch_capacity,
				 struct gud_xdisp_bounded_frame *frame,
				 struct gud_xdisp_chunk *chunk);

/*
 * Test-only hybrid policy. It first tries predicted_rows as one complete
 * bounded LZ4 block. A miss is never submitted: the normal bounded discovery
 * path chooses the chunk instead, and the frame enters a prediction cooldown.
 */
int gud_xdisp_plan_chunk_predictive_frame(
				    const u8 *source, u32 remaining_rows,
				    size_t bytes_per_line, u32 max_rows,
				    u32 predicted_rows, size_t payload_limit,
				    void *workmem, u8 *scratch,
				    size_t scratch_capacity,
				    struct gud_xdisp_bounded_frame *frame,
				    struct gud_xdisp_chunk *chunk);

u32 gud_xdisp_next_row_hint(u32 selected_rows, u32 absolute_max_rows);

/* Predict a next complete-row candidate from the most recent measured LZ4
 * ratio. The caller still validates every result against the hard payload cap.
 */
u32 gud_xdisp_next_row_hint_target(const struct gud_xdisp_chunk *chunk,
				   size_t bytes_per_line,
				   u32 absolute_max_rows,
				   size_t target_payload);

#endif
