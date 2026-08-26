/*
 * pi_lz4_decode_bench.c - real Raspberry Pi Zero 2 W LZ4 decode timing for
 * the "current LZ4" RGB565 candidate (PROJECT SPEC section 14).
 *
 * Built natively on the Pi (gcc is available there; unlike the OnePlus 6),
 * against the same vendored LZ4 1.10.0 source used by gud.ko and the OP6
 * sender benchmark, so encode (OP6) and decode (Pi) use byte-identical
 * codec logic.
 *
 * Usage: pi_lz4_decode_bench <compressed_bin> <decompressed_len> \
 *            <iterations> <out_json>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lz4.h"

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
	if (argc < 5) {
		fprintf(stderr,
			"usage: %s <compressed_bin> <decompressed_len> <iterations> <out_json>\n",
			argv[0]);
		return 2;
	}
	const char *comp_path = argv[1];
	int dec_len = atoi(argv[2]);
	int iterations = atoi(argv[3]);
	const char *out_json = argv[4];

	FILE *f = fopen(comp_path, "rb");
	if (!f) {
		perror("fopen");
		return 1;
	}
	fseek(f, 0, SEEK_END);
	long comp_len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *comp_buf = malloc(comp_len);
	fread(comp_buf, 1, comp_len, f);
	fclose(f);

	char *dec_buf = malloc(dec_len);
	uint64_t *samples = malloc(sizeof(uint64_t) * iterations);
	int warmup = iterations / 5 > 2 ? iterations / 5 : 2;

	for (int i = 0; i < warmup; i++)
		LZ4_decompress_safe(comp_buf, dec_buf, (int)comp_len, dec_len);

	int last_result = 0;
	for (int i = 0; i < iterations; i++) {
		uint64_t t0 = now_ns();
		last_result = LZ4_decompress_safe(comp_buf, dec_buf,
						   (int)comp_len, dec_len);
		samples[i] = now_ns() - t0;
	}

	qsort(samples, iterations, sizeof(uint64_t), cmp_u64);
	double sum = 0;
	for (int i = 0; i < iterations; i++)
		sum += (double)samples[i];
	double mean_ns = sum / iterations;

	FILE *jout = fopen(out_json, "w");
	fprintf(jout, "{\n");
	fprintf(jout, "  \"compressed_bytes\": %ld,\n", comp_len);
	fprintf(jout, "  \"decompressed_bytes\": %d,\n", dec_len);
	fprintf(jout, "  \"decompress_result\": %d,\n", last_result);
	fprintf(jout, "  \"iterations\": %d,\n", iterations);
	fprintf(jout, "  \"mean_ns\": %.1f,\n", mean_ns);
	fprintf(jout, "  \"p50_ns\": %llu,\n",
		(unsigned long long)samples[(int)(0.50 * (iterations - 1))]);
	fprintf(jout, "  \"p90_ns\": %llu,\n",
		(unsigned long long)samples[(int)(0.90 * (iterations - 1))]);
	fprintf(jout, "  \"p95_ns\": %llu,\n",
		(unsigned long long)samples[(int)(0.95 * (iterations - 1))]);
	fprintf(jout, "  \"p99_ns\": %llu,\n",
		(unsigned long long)samples[(int)(0.99 * (iterations - 1))]);
	fprintf(jout, "  \"max_ns\": %llu,\n",
		(unsigned long long)samples[iterations - 1]);
	fprintf(jout, "  \"min_ns\": %llu,\n", (unsigned long long)samples[0]);
	fprintf(jout, "  \"mpixels_s\": %.3f,\n",
		((double)(dec_len / 2) / 1e6) / (mean_ns / 1e9));
	fprintf(jout, "  \"label\": \"MEASURED_PI_LZ4_DECODE\"\n");
	fprintf(jout, "}\n");
	fclose(jout);
	fprintf(stderr, "decode_result=%d mean_ns=%.1f p50_ns=%llu\n",
		last_result, mean_ns,
		(unsigned long long)samples[(int)(0.50 * (iterations - 1))]);
	return 0;
}
