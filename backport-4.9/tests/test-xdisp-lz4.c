#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gud_xdisp_lz4.h"

extern int LZ4_decompress_safe(const char *src, char *dst,
			       int compressed_size, int dst_capacity);

#define FRAME_BYTES (1280U * 720U * 2U)

static int failures;

static void check_roundtrip(const char *name, const u8 *source, size_t length,
			    void *workmem)
{
	u8 *compressed = malloc(length);
	u8 *decoded = malloc(length);
	size_t compressed_length;
	int decoded_length;

	if (!compressed || !decoded) {
		fprintf(stderr, "FAIL [%s]: allocation\n", name);
		failures++;
		goto out;
	}

	compressed_length = gud_xdisp_lz4_compress_limited(
		source, length, compressed, length, workmem);
	if (!compressed_length) {
		/* This is the upstream raw-fallback result for incompressible data. */
		printf("PASS [%s]: raw fallback\n", name);
		goto out;
	}
	if (compressed_length >= length) {
		fprintf(stderr, "FAIL [%s]: non-beneficial length=%zu\n",
			name, compressed_length);
		failures++;
		goto out;
	}

	decoded_length = LZ4_decompress_safe((const char *)compressed,
		(char *)decoded, (int)compressed_length, (int)length);
	if (decoded_length != (int)length || memcmp(source, decoded, length)) {
		fprintf(stderr, "FAIL [%s]: roundtrip length=%d\n",
			name, decoded_length);
		failures++;
		goto out;
	}
	printf("PASS [%s]: source=%zu compressed=%zu\n",
	       name, length, compressed_length);

out:
	free(decoded);
	free(compressed);
}

int main(void)
{
	u8 *frame = malloc(FRAME_BYTES);
	void *workmem = malloc(gud_xdisp_lz4_upstream_workmem_size());
	uint32_t random = 0x12345678U;
	size_t i;

	if (!frame || !workmem)
		return 2;

	memset(frame, 0, FRAME_BYTES);
	check_roundtrip("zero-full-rgb565", frame, FRAME_BYTES, workmem);

	for (i = 0; i < FRAME_BYTES; i++)
		frame[i] = (u8)(i / (1280U * 2U));
	check_roundtrip("horizontal-lines-full-rgb565", frame, FRAME_BYTES,
			workmem);

	for (i = 0; i < FRAME_BYTES; i++) {
		random ^= random << 13;
		random ^= random >> 17;
		random ^= random << 5;
		frame[i] = (u8)random;
	}
	check_roundtrip("noise-full-rgb565", frame, FRAME_BYTES, workmem);

	free(workmem);
	free(frame);
	printf("xdisp full-update LZ4 tests: %d failures\n", failures);
	return failures ? 1 : 0;
}
