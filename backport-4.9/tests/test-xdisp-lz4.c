#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../variants/xdisp-lz4-12800/gud_xdisp_lz4.h"

int LZ4_decompress_safe(const char *source, char *destination,
			int compressed_size, int destination_capacity);

#define GUARD_SIZE 64U

enum pattern {
	PATTERN_ZERO,
	PATTERN_BARS,
	PATTERN_GRADIENT,
	PATTERN_RANDOM,
	PATTERN_MIXED,
};

static int failures;

typedef int (*xdisp_planner_fn)(const u8 *source, u32 remaining_rows,
				size_t bytes_per_line, u32 max_rows,
				size_t payload_limit, void *workmem,
				u8 *scratch, size_t scratch_capacity,
				struct gud_xdisp_chunk *chunk);

static void fail(const char *test, const char *message)
{
	fprintf(stderr, "FAIL [%s]: %s\n", test, message);
	failures++;
}

static uint32_t next_random(uint32_t *state)
{
	uint32_t value = *state;

	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	*state = value;
	return value;
}

static void fill_pattern(uint8_t *buffer, size_t length, size_t line_bytes,
			 enum pattern pattern)
{
	uint32_t random_state = 0x6d2b79f5U;
	size_t index;

	for (index = 0; index < length; index++) {
		size_t row = line_bytes ? index / line_bytes : 0;
		size_t column = line_bytes ? index % line_bytes : 0;

		switch (pattern) {
		case PATTERN_ZERO:
			buffer[index] = 0;
			break;
		case PATTERN_BARS:
			buffer[index] = (column / 320U) * 29U;
			break;
		case PATTERN_GRADIENT:
			buffer[index] = (uint8_t)(row + column / 2U);
			break;
		case PATTERN_RANDOM:
			buffer[index] = (uint8_t)next_random(&random_state);
			break;
		case PATTERN_MIXED:
			if (row % 37U < 29U)
				buffer[index] = (uint8_t)(row / 8U);
			else
				buffer[index] =
					(uint8_t)next_random(&random_state);
			break;
		}
	}
}

static int guards_intact(const uint8_t *allocation, size_t capacity)
{
	size_t index;

	for (index = 0; index < GUARD_SIZE; index++) {
		if (allocation[index] != 0xa5)
			return 0;
		if (allocation[GUARD_SIZE + capacity + index] != 0x5a)
			return 0;
	}
	return 1;
}

static void run_frame_case(const char *name, u32 width, u32 height,
			   enum pattern pattern, size_t payload_limit,
			   xdisp_planner_fn planner, int expect_retry)
{
	size_t line_bytes = (size_t)width * 2U;
	size_t frame_length = line_bytes * height;
	size_t scratch_capacity =
		gud_xdisp_lz4_compress_bound(frame_length);
	uint8_t *scratch_allocation;
	uint8_t *decompressed;
	uint8_t *workmem;
	uint8_t *frame;
	size_t offset = 0;
	u64 compression_attempts = 0;
	u64 rejected_compression_attempts = 0;
	u64 compression_source_bytes = 0;
	u32 rows_done = 0;
	u32 chunks = 0;
	u32 row_hint = height;
	size_t workmem_size = gud_xdisp_lz4_upstream_workmem_size();

	frame = malloc(frame_length);
	workmem = malloc(workmem_size);
	scratch_allocation = malloc(scratch_capacity + 2U * GUARD_SIZE);
	decompressed = malloc(frame_length);
	if (!frame || !workmem || !scratch_allocation || !decompressed) {
		fail(name, "allocation failed");
		goto out;
	}
	fill_pattern(frame, frame_length, line_bytes, pattern);
	memset(scratch_allocation, 0xcc,
	       scratch_capacity + 2U * GUARD_SIZE);
	memset(scratch_allocation, 0xa5, GUARD_SIZE);
	memset(scratch_allocation + GUARD_SIZE + scratch_capacity, 0x5a,
	       GUARD_SIZE);

	while (rows_done < height) {
		struct gud_xdisp_chunk chunk;
		u32 remaining = height - rows_done;
		int ret;

		ret = planner(
			frame + offset, remaining, line_bytes, row_hint,
			payload_limit, workmem,
			scratch_allocation + GUARD_SIZE, scratch_capacity,
			&chunk);
		if (ret) {
			fail(name, "planner rejected a valid frame");
			goto out;
		}
		if (!chunk.rows || chunk.rows > remaining) {
			fail(name, "planner did not make bounded row progress");
			goto out;
		}
		if (chunk.source_length !=
		    (size_t)chunk.rows * line_bytes) {
			fail(name, "source length does not match rectangle");
			goto out;
		}
		if (!chunk.payload_length ||
		    chunk.payload_length > payload_limit) {
			fail(name, "payload escaped the requested cap");
			goto out;
		}
		if (!guards_intact(scratch_allocation,
				   scratch_capacity)) {
			fail(name, "compressor wrote outside scratch");
			goto out;
		}
		if (!chunk.compression_attempts ||
		    chunk.rejected_compression_attempts >
			    chunk.compression_attempts ||
		    chunk.compression_source_bytes <
			    chunk.source_length) {
			fail(name, "planner returned invalid compression counters");
			goto out;
		}

		if (chunk.compressed) {
			int decoded = LZ4_decompress_safe(
				(char *)scratch_allocation + GUARD_SIZE,
				(char *)decompressed,
				(int)chunk.payload_length,
				(int)chunk.source_length);

			if (decoded != (int)chunk.source_length ||
			    memcmp(decompressed, frame + offset,
				   chunk.source_length)) {
				fail(name,
				     "standard liblz4 did not reproduce source");
				goto out;
			}
			if (chunk.payload_length >= chunk.source_length) {
				fail(name, "non-beneficial block marked compressed");
				goto out;
			}
		} else if (chunk.payload_length != chunk.source_length) {
			fail(name, "raw payload length mismatch");
			goto out;
		}

		offset += chunk.source_length;
		rows_done += chunk.rows;
		chunks++;
		compression_attempts += chunk.compression_attempts;
		rejected_compression_attempts +=
			chunk.rejected_compression_attempts;
		compression_source_bytes +=
			chunk.compression_source_bytes;
		row_hint = gud_xdisp_next_row_hint(chunk.rows, height);
		if (chunks > height) {
			fail(name, "planner failed to terminate");
			goto out;
		}
	}

	if (rows_done != height || offset != frame_length)
		fail(name, "rows were dropped or duplicated");
	if (compression_attempts < chunks ||
	    rejected_compression_attempts > compression_attempts ||
	    compression_source_bytes < frame_length)
		fail(name, "frame compression counters are inconsistent");
	if (pattern == PATTERN_ZERO && chunks != 1)
		fail(name, "compressible full frame was unnecessarily split");
	if (pattern == PATTERN_ZERO &&
	    (compression_attempts != 1 ||
	     rejected_compression_attempts != 0 ||
	     compression_source_bytes != frame_length))
		fail(name, "single-attempt frame counters are incorrect");
	if (expect_retry && pattern == PATTERN_RANDOM &&
	    frame_length > payload_limit &&
	    (!rejected_compression_attempts ||
	     compression_source_bytes <= frame_length))
		fail(name, "retry overhead was not reflected in counters");

out:
	free(decompressed);
	free(scratch_allocation);
	free(workmem);
	free(frame);
}

static void run_bounded_dest_size_case(void)
{
	const char *name = "bounded-dest-size-row-alignment";
	const size_t bytes_per_line = 1280U * 2U;
	const size_t source_length = bytes_per_line * 720U;
	const size_t target = GUD_XDISP_DISCOVERY_TARGET_PAYLOAD;
	uint8_t *source = malloc(source_length);
	uint8_t *decoded = malloc(source_length);
	uint8_t *discovery_allocation = malloc(target + 2U * GUARD_SIZE);
	uint8_t *final_allocation =
		malloc(GUD_XDISP_PAYLOAD_LIMIT + 2U * GUARD_SIZE);
	uint8_t *workmem = malloc(gud_xdisp_lz4_upstream_workmem_size());
	size_t consumed = source_length;
	size_t compressed;
	size_t aligned;
	int decoded_length;

	if (!source || !decoded || !discovery_allocation || !final_allocation ||
	    !workmem) {
		fail(name, "allocation failed");
		goto out;
	}
	fill_pattern(source, source_length, bytes_per_line, PATTERN_MIXED);
	memset(discovery_allocation, 0xcc, target + 2U * GUARD_SIZE);
	memset(discovery_allocation, 0xa5, GUARD_SIZE);
	memset(discovery_allocation + GUARD_SIZE + target, 0x5a,
	       GUARD_SIZE);
	compressed = gud_xdisp_lz4_compress_dest_size(
		source, &consumed, discovery_allocation + GUARD_SIZE, target,
		workmem);
	if (!compressed || compressed > target || !consumed ||
	    consumed > source_length ||
	    !guards_intact(discovery_allocation, target)) {
		fail(name, "bounded discovery violated its destination contract");
		goto out;
	}
	decoded_length = LZ4_decompress_safe(
		(char *)discovery_allocation + GUARD_SIZE, (char *)decoded,
		(int)compressed, (int)source_length);
	if (decoded_length != (int)consumed ||
	    memcmp(decoded, source, consumed)) {
		fail(name, "bounded discovery did not reproduce its consumed prefix");
		goto out;
	}

	aligned = consumed - consumed % bytes_per_line;
	if (!aligned) {
		fail(name, "bounded discovery did not reach a complete row");
		goto out;
	}
	memset(final_allocation, 0xcc,
	       GUD_XDISP_PAYLOAD_LIMIT + 2U * GUARD_SIZE);
	memset(final_allocation, 0xa5, GUARD_SIZE);
	memset(final_allocation + GUARD_SIZE + GUD_XDISP_PAYLOAD_LIMIT,
	       0x5a, GUARD_SIZE);
	compressed = gud_xdisp_lz4_compress(
		source, aligned, final_allocation + GUARD_SIZE,
		GUD_XDISP_PAYLOAD_LIMIT, workmem);
	if (!compressed || compressed > GUD_XDISP_PAYLOAD_LIMIT ||
	    !guards_intact(final_allocation, GUD_XDISP_PAYLOAD_LIMIT)) {
		fail(name, "aligned validation escaped the hard transfer cap");
		goto out;
	}
	decoded_length = LZ4_decompress_safe(
		(char *)final_allocation + GUARD_SIZE, (char *)decoded,
		(int)compressed, (int)aligned);
	if (decoded_length != (int)aligned || memcmp(decoded, source, aligned))
		fail(name, "aligned bounded rectangle did not round trip");

out:
	free(workmem);
	free(final_allocation);
	free(discovery_allocation);
	free(decoded);
	free(source);
}

static void run_direct_compressor_case(void)
{
	const char *name = "direct-lz4-bound-and-guard";
	size_t source_length = 64000;
	size_t bound = gud_xdisp_lz4_compress_bound(source_length);
	uint8_t *allocation = malloc(bound + 2U * GUARD_SIZE);
	uint8_t *decoded = malloc(source_length);
	uint8_t *source = malloc(source_length);
	uint8_t *workmem = malloc(gud_xdisp_lz4_upstream_workmem_size());
	size_t compressed;
	int decoded_length;

	if (!allocation || !decoded || !source || !workmem) {
		fail(name, "allocation failed");
		goto out;
	}
	fill_pattern(source, source_length, 2560, PATTERN_RANDOM);
	memset(allocation, 0xcc, bound + 2U * GUARD_SIZE);
	memset(allocation, 0xa5, GUARD_SIZE);
	memset(allocation + GUARD_SIZE + bound, 0x5a, GUARD_SIZE);

	compressed = gud_xdisp_lz4_compress(
		source, source_length, allocation + GUARD_SIZE, bound,
		workmem);
	if (!compressed || compressed > bound) {
		fail(name, "compression did not fit its advertised bound");
		goto out;
	}
	if (!guards_intact(allocation, bound)) {
		fail(name, "compression crossed its destination bound");
		goto out;
	}
	decoded_length = LZ4_decompress_safe(
		(char *)allocation + GUARD_SIZE, (char *)decoded,
		(int)compressed, (int)source_length);
	if (decoded_length != (int)source_length ||
	    memcmp(decoded, source, source_length))
		fail(name, "standard liblz4 round trip failed");

	memset(allocation, 0xcc, bound + 2U * GUARD_SIZE);
	memset(allocation, 0xa5, GUARD_SIZE);
	memset(allocation + GUARD_SIZE + 12800, 0x5a, GUARD_SIZE);
	compressed = gud_xdisp_lz4_compress(
		source, source_length, allocation + GUARD_SIZE, 12800,
		workmem);
	if (compressed)
		fail(name, "incompressible block unexpectedly fit 12800");
	if (!guards_intact(allocation, 12800))
		fail(name, "failed compression crossed the 12800 cap");

out:
	free(workmem);
	free(source);
	free(decoded);
	free(allocation);
}

static void run_randomized_compressor_cases(void)
{
	const char *name = "randomized-lz4-roundtrip";
	const size_t maximum_length = 2U * 1024U * 1024U;
	size_t maximum_bound =
		gud_xdisp_lz4_compress_bound(maximum_length);
	uint8_t *allocation =
		malloc(maximum_bound + 2U * GUARD_SIZE);
	uint8_t *decoded = malloc(maximum_length);
	uint8_t *source = malloc(maximum_length);
	uint8_t *workmem = malloc(gud_xdisp_lz4_upstream_workmem_size());
	uint32_t random_state = 0x91e10da5U;
	unsigned int test_index;

	if (!allocation || !decoded || !source || !workmem) {
		fail(name, "allocation failed");
		goto out;
	}

	for (test_index = 0; test_index < 4160; test_index++) {
		size_t length_limit =
			test_index < 4096 ? 128U * 1024U : maximum_length;
		size_t length = 1U +
			next_random(&random_state) % length_limit;
		size_t bound = gud_xdisp_lz4_compress_bound(length);
		size_t compressed;
		size_t capacity;
		int decoded_length;

		fill_pattern(source, length, 2560,
			     (enum pattern)(test_index % 5U));
		memset(allocation, 0xcc, bound + 2U * GUARD_SIZE);
		memset(allocation, 0xa5, GUARD_SIZE);
		memset(allocation + GUARD_SIZE + bound, 0x5a,
		       GUARD_SIZE);
		compressed = gud_xdisp_lz4_compress(
			source, length, allocation + GUARD_SIZE, bound,
			workmem);
		if (!compressed || compressed > bound ||
		    !guards_intact(allocation, bound)) {
			fail(name, "full-bound compression violated contract");
			goto out;
		}
		decoded_length = LZ4_decompress_safe(
			(char *)allocation + GUARD_SIZE, (char *)decoded,
			(int)compressed, (int)length);
		if (decoded_length != (int)length ||
		    memcmp(decoded, source, length)) {
			fail(name, "full-bound randomized round trip failed");
			goto out;
		}

		capacity = 1U + next_random(&random_state) % bound;
		memset(allocation, 0xcc, capacity + 2U * GUARD_SIZE);
		memset(allocation, 0xa5, GUARD_SIZE);
		memset(allocation + GUARD_SIZE + capacity, 0x5a,
		       GUARD_SIZE);
		compressed = gud_xdisp_lz4_compress(
			source, length, allocation + GUARD_SIZE, capacity,
			workmem);
		if (!guards_intact(allocation, capacity) ||
		    compressed > capacity) {
			fail(name, "bounded compression crossed randomized cap");
			goto out;
		}
		if (compressed) {
			decoded_length = LZ4_decompress_safe(
				(char *)allocation + GUARD_SIZE,
				(char *)decoded, (int)compressed,
				(int)length);
			if (decoded_length != (int)length ||
			    memcmp(decoded, source, length)) {
				fail(name,
				     "bounded randomized round trip failed");
				goto out;
			}
		}
	}

out:
	free(workmem);
	free(source);
	free(decoded);
	free(allocation);
}

static void run_invalid_cases(void)
{
	const char *name = "invalid-and-boundary-inputs";
	uint8_t source[25600] = { 0 };
	uint8_t scratch[25800];
	uint8_t *workmem = malloc(gud_xdisp_lz4_upstream_workmem_size());
	struct gud_xdisp_chunk chunk;
	int ret;

	if (!workmem) {
		fail(name, "workmem allocation failed");
		return;
	}

	ret = gud_xdisp_plan_chunk(source, 1, 12801, 1, 12800,
				   workmem, scratch, sizeof(scratch), &chunk);
	if (ret != -E2BIG)
		fail(name, "one row larger than cap was not rejected");

	ret = gud_xdisp_plan_chunk(source, 0, 2560, 1, 12800,
				   workmem, scratch, sizeof(scratch), &chunk);
	if (ret != -EINVAL)
		fail(name, "zero remaining rows was not rejected");

	ret = gud_xdisp_plan_chunk(source, 10, 2560, 10, 12800,
				   workmem, scratch, 100, &chunk);
	if (ret != -ENOSPC)
		fail(name, "undersized scratch was not rejected");
	if (gud_xdisp_next_row_hint(5, 720) != 10 ||
	    gud_xdisp_next_row_hint(400, 720) != 720 ||
	    gud_xdisp_next_row_hint(0, 720) != 0)
		fail(name, "row-hint growth is not bounded");
	chunk.source_length = 160U * 2560U;
	chunk.payload_length = 12000U;
	if (gud_xdisp_next_row_hint_target(&chunk, 2560, 720, 12160) !=
	    162)
		fail(name, "recent-ratio row prediction is incorrect");

	run_frame_case("exact-12800-random", 1280, 5, PATTERN_RANDOM,
		       12800, gud_xdisp_plan_chunk, 1);
	run_frame_case("limit-12799", 1280, 9, PATTERN_MIXED, 12799,
		       gud_xdisp_plan_chunk, 1);
	run_frame_case("limit-12801", 1280, 9, PATTERN_MIXED, 12801,
		       gud_xdisp_plan_chunk, 1);
	free(workmem);
}

int main(void)
{
	run_direct_compressor_case();
	run_randomized_compressor_cases();
	run_bounded_dest_size_case();
	run_frame_case("solid-1280x720", 1280, 720, PATTERN_ZERO, 12800,
		       gud_xdisp_plan_chunk, 1);
	run_frame_case("bars-1280x720", 1280, 720, PATTERN_BARS, 12800,
		       gud_xdisp_plan_chunk, 1);
	run_frame_case("gradient-1280x720", 1280, 720,
		       PATTERN_GRADIENT, 12800, gud_xdisp_plan_chunk, 1);
	run_frame_case("mixed-1280x719", 1280, 719, PATTERN_MIXED, 12800,
		       gud_xdisp_plan_chunk, 1);
	run_frame_case("random-1280x720", 1280, 720, PATTERN_RANDOM, 12800,
		       gud_xdisp_plan_chunk, 1);
	run_frame_case("random-640x41", 640, 41, PATTERN_RANDOM, 12800,
		       gud_xdisp_plan_chunk, 1);
	run_frame_case("mixed-1920x53", 1920, 53, PATTERN_MIXED, 12800,
		       gud_xdisp_plan_chunk, 1);
	run_frame_case("bounded-solid-1280x720", 1280, 720, PATTERN_ZERO,
		       12800, gud_xdisp_plan_chunk_bounded, 0);
	run_frame_case("bounded-mixed-1280x719", 1280, 719, PATTERN_MIXED,
		       12800, gud_xdisp_plan_chunk_bounded, 0);
	run_frame_case("bounded-random-1280x720", 1280, 720, PATTERN_RANDOM,
		       12800, gud_xdisp_plan_chunk_bounded, 0);
	run_invalid_cases();

	printf("xdisp-lz4 tests: %d failures\n", failures);
	return failures ? 1 : 0;
}
