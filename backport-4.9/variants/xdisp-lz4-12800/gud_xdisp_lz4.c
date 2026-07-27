/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * XDISP rectangle planning around the private upstream LZ4 embedding.
 *
 * gud_xdisp_lz4_upstream.c embeds lz4 1.10.0's block compressor in gud.ko.
 * Keep the policy here: protocol framing, full-row validation, and the
 * 12,800-byte transport invariant are GUD responsibilities, not LZ4's.
 */

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#else
#include <errno.h>
#include <limits.h>
#endif

#include "gud_xdisp_lz4.h"

size_t gud_xdisp_lz4_compress_bound(size_t source_length)
{
	if (source_length > (size_t)-1 - source_length / 255 - 16)
		return 0;
	return source_length + source_length / 255 + 16;
}

size_t gud_xdisp_lz4_compress(const u8 *source, size_t source_length,
			      u8 *destination, size_t destination_capacity,
			      void *workmem)
{
	size_t consumed = source_length;
	size_t compressed_length;

	/*
	 * destSize() is also the normal full-rectangle compressor here.  It is
	 * accepted only when it consumed exactly the caller's complete-row
	 * rectangle.  A partial LZ4 prefix is never exposed as a GUD payload.
	 */
	compressed_length = gud_xdisp_lz4_compress_dest_size(
		source, &consumed, destination, destination_capacity, workmem);
	if (!compressed_length || consumed != source_length)
		return 0;

	return compressed_length;
}

static int gud_xdisp_plan_raw_chunk(u32 remaining_rows,
				    size_t bytes_per_line, u32 max_rows,
				    size_t payload_limit,
				    struct gud_xdisp_chunk *chunk)
{
	u32 rows;

	if (!remaining_rows || !bytes_per_line || !max_rows || !payload_limit ||
	    !chunk)
		return -EINVAL;
	if (bytes_per_line > payload_limit)
		return -E2BIG;

	rows = remaining_rows;
	if (rows > max_rows)
		rows = max_rows;
	if (rows > payload_limit / bytes_per_line)
		rows = payload_limit / bytes_per_line;
	if (!rows)
		return -E2BIG;

	chunk->rows = rows;
	chunk->source_length = (size_t)rows * bytes_per_line;
	chunk->payload_length = chunk->source_length;
	chunk->compressed = false;
	chunk->compression_attempts = 0;
	chunk->rejected_compression_attempts = 0;
	chunk->compression_source_bytes = 0;
	return 0;
}

int gud_xdisp_plan_chunk(const u8 *source, u32 remaining_rows,
			 size_t bytes_per_line, u32 max_rows,
			 size_t payload_limit, void *workmem,
			 u8 *scratch, size_t scratch_capacity,
			 struct gud_xdisp_chunk *chunk)
{
	u32 raw_fallback_rows;
	u32 compression_attempts = 0;
	u32 rejected_compression_attempts = 0;
	u64 compression_source_bytes = 0;
	u32 rows;

	if (!source || !remaining_rows || !bytes_per_line || !max_rows ||
	    !payload_limit || !workmem || !scratch || !chunk)
		return -EINVAL;
	if (bytes_per_line > payload_limit)
		return -E2BIG;

	raw_fallback_rows = payload_limit / bytes_per_line;
	if (!raw_fallback_rows)
		return -E2BIG;
	rows = remaining_rows < max_rows ? remaining_rows : max_rows;

	for (;;) {
		size_t source_length;
		size_t compressed_length;
		size_t bound;
		u64 estimate;
		u32 next_rows;

		if ((size_t)rows > (size_t)-1 / bytes_per_line)
			return -EOVERFLOW;
		source_length = (size_t)rows * bytes_per_line;
		bound = gud_xdisp_lz4_compress_bound(source_length);
		if (!bound || bound > scratch_capacity)
			return -ENOSPC;

		compression_attempts++;
		compression_source_bytes += source_length;
		compressed_length = gud_xdisp_lz4_compress(
			source, source_length, scratch, bound, workmem);
		if (!compressed_length)
			return -EIO;

		if (compressed_length < source_length &&
		    compressed_length <= payload_limit) {
			chunk->rows = rows;
			chunk->source_length = source_length;
			chunk->payload_length = compressed_length;
			chunk->compressed = true;
			chunk->compression_attempts = compression_attempts;
			chunk->rejected_compression_attempts =
				rejected_compression_attempts;
			chunk->compression_source_bytes =
				compression_source_bytes;
			return 0;
		}

		rejected_compression_attempts++;
		if (source_length <= payload_limit) {
			chunk->rows = rows;
			chunk->source_length = source_length;
			chunk->payload_length = source_length;
			chunk->compressed = false;
			chunk->compression_attempts = compression_attempts;
			chunk->rejected_compression_attempts =
				rejected_compression_attempts;
			chunk->compression_source_bytes =
				compression_source_bytes;
			return 0;
		}

		/*
		 * Use the measured ratio to choose the next complete-row
		 * rectangle.  The loop always verifies the new result and
		 * strictly decreases rows, so the payload cap does not depend
		 * on compression-ratio monotonicity.
		 */
		estimate = (u64)rows * payload_limit;
		estimate /= compressed_length;
		next_rows = estimate > (u64)~0U ? ~0U : (u32)estimate;
		if (next_rows < raw_fallback_rows)
			next_rows = raw_fallback_rows;
		if (next_rows >= rows)
			next_rows = rows - 1;
		if (next_rows < raw_fallback_rows)
			next_rows = raw_fallback_rows;
		rows = next_rows;
	}
}

int gud_xdisp_plan_chunk_bounded(const u8 *source, u32 remaining_rows,
				 size_t bytes_per_line, u32 max_rows,
				 size_t payload_limit, void *workmem,
				 u8 *scratch, size_t scratch_capacity,
				 struct gud_xdisp_chunk *chunk)
{
	size_t source_length;
	size_t discovered_source_length;
	size_t discovered_payload_length;
	size_t target_payload;
	size_t aligned_source_length;
	size_t compressed_length;
	size_t bound;
	u32 raw_fallback_rows;
	u32 raw_rows;
	u32 rows;
	int ret;

	if (!source || !remaining_rows || !bytes_per_line || !max_rows ||
	    !payload_limit || !workmem || !scratch || !chunk)
		return -EINVAL;
	if (bytes_per_line > payload_limit)
		return -E2BIG;

	raw_fallback_rows = payload_limit / bytes_per_line;
	if (!raw_fallback_rows)
		return -E2BIG;
	rows = remaining_rows < max_rows ? remaining_rows : max_rows;
	raw_rows = rows < raw_fallback_rows ? rows : raw_fallback_rows;
	if ((size_t)rows > (size_t)-1 / bytes_per_line)
		return -EOVERFLOW;
	source_length = (size_t)rows * bytes_per_line;
	bound = gud_xdisp_lz4_compress_bound(source_length);
	if (!bound || bound > scratch_capacity)
		return -ENOSPC;

	/*
	 * Reserve 5% of the proven cap. It keeps a headroom margin while the
	 * exact full-row validation below retains the real 12,800-byte invariant.
	 */
	target_payload = payload_limit * GUD_XDISP_TARGET_PAYLOAD_PERCENT / 100U;
	if (!target_payload || target_payload > payload_limit)
		target_payload = payload_limit;
	discovered_source_length = source_length;
	discovered_payload_length = gud_xdisp_lz4_compress_dest_size(
		source, &discovered_source_length, scratch, target_payload,
		workmem);
	if (!discovered_payload_length ||
	    discovered_payload_length > target_payload ||
	    !discovered_source_length || discovered_source_length > source_length)
		return -EIO;

	/* If the complete candidate fits, the discovery result is final. */
	if (discovered_source_length == source_length) {
		if (discovered_payload_length < source_length &&
		    discovered_payload_length <= payload_limit) {
			chunk->rows = rows;
			chunk->source_length = source_length;
			chunk->payload_length = discovered_payload_length;
			chunk->compressed = true;
			chunk->compression_attempts = 1;
			chunk->rejected_compression_attempts = 0;
			chunk->compression_source_bytes = source_length;
			return 0;
		}
		if (source_length <= payload_limit)
			goto raw_fallback;
		return -EIO;
	}

	/* A GUD rectangle must contain only full scanlines. */
	aligned_source_length = discovered_source_length;
	aligned_source_length -= aligned_source_length % bytes_per_line;
	if (aligned_source_length < (size_t)raw_rows * bytes_per_line ||
	    discovered_payload_length >= discovered_source_length)
		goto raw_fallback;

	/*
	 * destSize() may end in the middle of a row. Recompress the aligned prefix
	 * with the hard cap before it can reach SET_BUFFER. This is the sole
	 * validation attempt; a failure falls back to a cap-safe raw rectangle.
	 */
	compressed_length = gud_xdisp_lz4_compress(
		source, aligned_source_length, scratch, payload_limit, workmem);
	if (compressed_length && compressed_length < aligned_source_length &&
	    compressed_length <= payload_limit) {
		chunk->rows = aligned_source_length / bytes_per_line;
		chunk->source_length = aligned_source_length;
		chunk->payload_length = compressed_length;
		chunk->compressed = true;
		chunk->compression_attempts = 2;
		chunk->rejected_compression_attempts = 0;
		chunk->compression_source_bytes = source_length +
			aligned_source_length;
		return 0;
	}

raw_fallback:
	ret = gud_xdisp_plan_raw_chunk(remaining_rows, bytes_per_line,
					 raw_rows, payload_limit, chunk);
	if (ret)
		return ret;
	chunk->compression_attempts = 1;
	chunk->rejected_compression_attempts = 0;
	chunk->compression_source_bytes = source_length;
	return 0;
}

int gud_xdisp_plan_chunk_bounded_frame(
				 const u8 *source, u32 remaining_rows,
				 size_t bytes_per_line, u32 max_rows,
				 size_t payload_limit, void *workmem,
				 u8 *scratch, size_t scratch_capacity,
				 struct gud_xdisp_bounded_frame *frame,
				 struct gud_xdisp_chunk *chunk)
{
	int ret;

	if (!frame)
		return -EINVAL;
	if (frame->raw_backoff)
		return gud_xdisp_plan_raw_chunk(remaining_rows, bytes_per_line,
						max_rows, payload_limit, chunk);

	ret = gud_xdisp_plan_chunk_bounded(source, remaining_rows,
					  bytes_per_line, max_rows, payload_limit,
					  workmem, scratch, scratch_capacity, chunk);
	if (!ret && !chunk->compressed)
		frame->raw_backoff = true;
	return ret;
}

u32 gud_xdisp_next_row_hint(u32 selected_rows, u32 absolute_max_rows)
{
	if (!selected_rows || !absolute_max_rows)
		return 0;
	if (selected_rows >= absolute_max_rows ||
	    selected_rows > absolute_max_rows / 2)
		return absolute_max_rows;
	return selected_rows * 2;
}

u32 gud_xdisp_next_row_hint_target(const struct gud_xdisp_chunk *chunk,
				   size_t bytes_per_line,
				   u32 absolute_max_rows,
				   size_t target_payload)
{
	u64 rows;

	if (!chunk || !chunk->source_length || !chunk->payload_length ||
	    !bytes_per_line || !absolute_max_rows || !target_payload)
		return 0;

	rows = (u64)target_payload * chunk->source_length;
	rows /= chunk->payload_length;
	rows /= bytes_per_line;
	if (!rows)
		rows = 1;
	if (rows > absolute_max_rows)
		return absolute_max_rows;
	return (u32)rows;
}
