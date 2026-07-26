/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Small, private LZ4 block compressor for the XDISP diagnostic module.
 *
 * The target kernel has CONFIG_LZ4_COMPRESS disabled, so gud.ko cannot use
 * the kernel's non-exported implementation. This is adapted from
 * lib/lz4/lz4_compress.c and lib/lz4/lz4defs.h at the exact target kernel
 * commit 6b190d86bd895acf891617e26840b1a85df6602c:
 *
 * Copyright (C) 2011-2012, Yann Collet.
 * Changed for kernel use by Chanho Min <chanho.min@lge.com>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#endif

#include "gud_xdisp_lz4.h"
#include "gud_xdisp_lz4defs.h"

static int gud_lz4_compress_general(void *context, const u8 *source,
				    u8 *destination, int source_size,
				    int destination_capacity)
{
	GUD_LZ4_HASH_TYPE *hash_table = context;
	const u8 *input = source;
	const u8 *const base = input;
	const u8 *anchor = input;
	const u8 *const input_end = input + source_size;
	const u8 *const match_limit = input_end - GUD_LZ4_MF_LIMIT;
	const u8 *const copy_limit = input_end - GUD_LZ4_LAST_LITERALS;
	u8 *output = destination;
	u8 *const output_end = output + destination_capacity;
	u32 forward_hash;
	int last_run;

	if (source_size < GUD_LZ4_MIN_LENGTH)
		goto last_literals;

	memset(hash_table, 0, GUD_XDISP_LZ4_WORKMEM_SIZE);
	hash_table[GUD_LZ4_HASH_VALUE(input)] = input - base;
	input++;
	forward_hash = GUD_LZ4_HASH_VALUE(input);

	for (;;) {
		int attempts = (1U << GUD_LZ4_SKIP_STRENGTH) + 3;
		const u8 *forward_input = input;
		const u8 *reference;
		u8 *token;
		int length;

		do {
			u32 hash = forward_hash;
			int step = attempts++ >> GUD_LZ4_SKIP_STRENGTH;

			input = forward_input;
			forward_input = input + step;
			if (unlikely(forward_input > match_limit))
				goto last_literals;

			forward_hash = GUD_LZ4_HASH_VALUE(forward_input);
			reference = base + hash_table[hash];
			hash_table[hash] = input - base;
		} while (reference < input - GUD_LZ4_MAX_DISTANCE ||
			 GUD_LZ4_A32(reference) != GUD_LZ4_A32(input));

		while (input > anchor && reference > source &&
		       unlikely(input[-1] == reference[-1])) {
			input--;
			reference--;
		}

		length = input - anchor;
		token = output++;
		if (unlikely(output + length +
			     (2 + 1 + GUD_LZ4_LAST_LITERALS) +
			     (length >> 8) > output_end))
			return 0;

		if (length >= GUD_LZ4_RUN_MASK) {
			int literal_length = length - GUD_LZ4_RUN_MASK;

			*token = GUD_LZ4_RUN_MASK << GUD_LZ4_ML_BITS;
			for (; literal_length > 254; literal_length -= 255)
				*output++ = 255;
			*output++ = (u8)literal_length;
		} else {
			*token = length << GUD_LZ4_ML_BITS;
		}

		GUD_LZ4_BLIND_COPY(anchor, output, length);

next_match:
		GUD_LZ4_WRITE_LE16(output, input - reference);
		input += GUD_LZ4_MIN_MATCH;
		reference += GUD_LZ4_MIN_MATCH;
		anchor = input;

		while (likely(input < copy_limit - (GUD_LZ4_STEP_SIZE - 1))) {
			u64 difference = GUD_LZ4_A64(reference) ^
					 GUD_LZ4_A64(input);

			if (!difference) {
				input += GUD_LZ4_STEP_SIZE;
				reference += GUD_LZ4_STEP_SIZE;
				continue;
			}
			input += GUD_LZ4_COMMON_BYTES(difference);
			goto end_count;
		}
		if (input < copy_limit - 3 &&
		    GUD_LZ4_A32(reference) == GUD_LZ4_A32(input)) {
			input += 4;
			reference += 4;
		}
		if (input < copy_limit - 1 &&
		    GUD_LZ4_A16(reference) == GUD_LZ4_A16(input)) {
			input += 2;
			reference += 2;
		}
		if (input < copy_limit && *reference == *input)
			input++;

end_count:
		length = input - anchor;
		if (unlikely(output + (1 + GUD_LZ4_LAST_LITERALS) +
			     (length >> 8) > output_end))
			return 0;

		if (length >= GUD_LZ4_ML_MASK) {
			*token += GUD_LZ4_ML_MASK;
			length -= GUD_LZ4_ML_MASK;
			for (; length > 509; length -= 510) {
				*output++ = 255;
				*output++ = 255;
			}
			if (length > 254) {
				length -= 255;
				*output++ = 255;
			}
			*output++ = (u8)length;
		} else {
			*token += length;
		}

		if (input > match_limit) {
			anchor = input;
			break;
		}

		hash_table[GUD_LZ4_HASH_VALUE(input - 2)] = input - 2 - base;
		reference = base + hash_table[GUD_LZ4_HASH_VALUE(input)];
		hash_table[GUD_LZ4_HASH_VALUE(input)] = input - base;
		if (reference > input - (GUD_LZ4_MAX_DISTANCE + 1) &&
		    GUD_LZ4_A32(reference) == GUD_LZ4_A32(input)) {
			token = output++;
			*token = 0;
			goto next_match;
		}

		anchor = input++;
		forward_hash = GUD_LZ4_HASH_VALUE(input);
	}

last_literals:
	last_run = input_end - anchor;
	if ((size_t)(output - destination) + last_run + 1 +
	    ((last_run + 255 - GUD_LZ4_RUN_MASK) / 255) >
	    (size_t)destination_capacity)
		return 0;

	if (last_run >= GUD_LZ4_RUN_MASK) {
		*output++ = GUD_LZ4_RUN_MASK << GUD_LZ4_ML_BITS;
		last_run -= GUD_LZ4_RUN_MASK;
		for (; last_run > 254; last_run -= 255)
			*output++ = 255;
		*output++ = (u8)last_run;
	} else {
		*output++ = last_run << GUD_LZ4_ML_BITS;
	}
	memcpy(output, anchor, input_end - anchor);
	output += input_end - anchor;

	return output - destination;
}

static int gud_lz4_compress_64k(void *context, const u8 *source,
				u8 *destination, int source_size,
				int destination_capacity)
{
	u16 *hash_table = context;
	const u8 *input = source;
	const u8 *anchor = input;
	const u8 *const base = input;
	const u8 *const input_end = input + source_size;
	const u8 *const match_limit = input_end - GUD_LZ4_MF_LIMIT;
	const u8 *const copy_limit = input_end - GUD_LZ4_LAST_LITERALS;
	u8 *output = destination;
	u8 *const output_end = output + destination_capacity;
	u32 forward_hash;
	int last_run;

	if (source_size < GUD_LZ4_MIN_LENGTH)
		goto last_literals;

	memset(hash_table, 0, GUD_XDISP_LZ4_WORKMEM_SIZE);
	input++;
	forward_hash = GUD_LZ4_HASH64K_VALUE(input);

	for (;;) {
		int attempts = (1U << GUD_LZ4_SKIP_STRENGTH) + 3;
		const u8 *forward_input = input;
		const u8 *reference;
		u8 *token;
		int length;

		do {
			u32 hash = forward_hash;
			int step = attempts++ >> GUD_LZ4_SKIP_STRENGTH;

			input = forward_input;
			forward_input = input + step;
			if (forward_input > match_limit)
				goto last_literals;

			forward_hash = GUD_LZ4_HASH64K_VALUE(forward_input);
			reference = base + hash_table[hash];
			hash_table[hash] = (u16)(input - base);
		} while (GUD_LZ4_A32(reference) != GUD_LZ4_A32(input));

		while (input > anchor && reference > source &&
		       input[-1] == reference[-1]) {
			input--;
			reference--;
		}

		length = input - anchor;
		token = output++;
		if (unlikely(output + length +
			     (2 + 1 + GUD_LZ4_LAST_LITERALS) +
			     (length >> 8) > output_end))
			return 0;

		if (length >= GUD_LZ4_RUN_MASK) {
			int literal_length = length - GUD_LZ4_RUN_MASK;

			*token = GUD_LZ4_RUN_MASK << GUD_LZ4_ML_BITS;
			for (; literal_length > 254; literal_length -= 255)
				*output++ = 255;
			*output++ = (u8)literal_length;
		} else {
			*token = length << GUD_LZ4_ML_BITS;
		}

		GUD_LZ4_BLIND_COPY(anchor, output, length);

next_match:
		GUD_LZ4_WRITE_LE16(output, input - reference);
		input += GUD_LZ4_MIN_MATCH;
		reference += GUD_LZ4_MIN_MATCH;
		anchor = input;

		while (input < copy_limit - (GUD_LZ4_STEP_SIZE - 1)) {
			u64 difference = GUD_LZ4_A64(reference) ^
					 GUD_LZ4_A64(input);

			if (!difference) {
				input += GUD_LZ4_STEP_SIZE;
				reference += GUD_LZ4_STEP_SIZE;
				continue;
			}
			input += GUD_LZ4_COMMON_BYTES(difference);
			goto end_count;
		}
		if (input < copy_limit - 3 &&
		    GUD_LZ4_A32(reference) == GUD_LZ4_A32(input)) {
			input += 4;
			reference += 4;
		}
		if (input < copy_limit - 1 &&
		    GUD_LZ4_A16(reference) == GUD_LZ4_A16(input)) {
			input += 2;
			reference += 2;
		}
		if (input < copy_limit && *reference == *input)
			input++;

end_count:
		length = input - anchor;
		if (unlikely(output + (1 + GUD_LZ4_LAST_LITERALS) +
			     (length >> 8) > output_end))
			return 0;

		if (length >= GUD_LZ4_ML_MASK) {
			*token += GUD_LZ4_ML_MASK;
			length -= GUD_LZ4_ML_MASK;
			for (; length > 509; length -= 510) {
				*output++ = 255;
				*output++ = 255;
			}
			if (length > 254) {
				length -= 255;
				*output++ = 255;
			}
			*output++ = (u8)length;
		} else {
			*token += length;
		}

		if (input > match_limit) {
			anchor = input;
			break;
		}

		hash_table[GUD_LZ4_HASH64K_VALUE(input - 2)] =
			(u16)(input - 2 - base);
		reference = base + hash_table[GUD_LZ4_HASH64K_VALUE(input)];
		hash_table[GUD_LZ4_HASH64K_VALUE(input)] =
			(u16)(input - base);
		if (GUD_LZ4_A32(reference) == GUD_LZ4_A32(input)) {
			token = output++;
			*token = 0;
			goto next_match;
		}

		anchor = input++;
		forward_hash = GUD_LZ4_HASH64K_VALUE(input);
	}

last_literals:
	last_run = input_end - anchor;
	if ((size_t)(output - destination) + last_run + 1 +
	    ((last_run + 255 - GUD_LZ4_RUN_MASK) / 255) >
	    (size_t)destination_capacity)
		return 0;

	if (last_run >= GUD_LZ4_RUN_MASK) {
		*output++ = GUD_LZ4_RUN_MASK << GUD_LZ4_ML_BITS;
		last_run -= GUD_LZ4_RUN_MASK;
		for (; last_run > 254; last_run -= 255)
			*output++ = 255;
		*output++ = (u8)last_run;
	} else {
		*output++ = last_run << GUD_LZ4_ML_BITS;
	}
	memcpy(output, anchor, input_end - anchor);
	output += input_end - anchor;

	return output - destination;
}

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
	int result;

	if (!source || !source_length || !destination || !destination_capacity ||
	    !workmem || source_length > INT_MAX ||
	    destination_capacity > INT_MAX)
		return 0;

	if (source_length < GUD_LZ4_64K_LIMIT)
		result = gud_lz4_compress_64k(workmem, source, destination,
					     source_length,
					     destination_capacity);
	else
		result = gud_lz4_compress_general(workmem, source, destination,
						 source_length,
						 destination_capacity);

	return result > 0 ? (size_t)result : 0;
}

int gud_xdisp_plan_chunk(const u8 *source, u32 remaining_rows,
			 size_t bytes_per_line, u32 max_rows,
			 size_t payload_limit, void *workmem,
			 u8 *scratch, size_t scratch_capacity,
			 struct gud_xdisp_chunk *chunk)
{
	u32 raw_fallback_rows;
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
			return 0;
		}

		if (source_length <= payload_limit) {
			chunk->rows = rows;
			chunk->source_length = source_length;
			chunk->payload_length = source_length;
			chunk->compressed = false;
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

u32 gud_xdisp_next_row_hint(u32 selected_rows, u32 absolute_max_rows)
{
	if (!selected_rows || !absolute_max_rows)
		return 0;
	if (selected_rows >= absolute_max_rows ||
	    selected_rows > absolute_max_rows / 2)
		return absolute_max_rows;
	return selected_rows * 2;
}
