/*
 * op6_sender_bench.c - real OnePlus-6-side (or any aarch64 target) sender
 * benchmark for the final codec/transport comparison.
 *
 * This is a standalone, self-contained tool (statically linked against
 * musl + vendored LZ4 1.10.0 + vendored libjpeg-turbo) built specifically
 * because the project's OnePlus 6 Ubuntu Touch userspace has no working
 * C compiler (verified: `gcc`/`cc`/`clang` are all absent). It is cross
 * built on the workstation with `musl-gcc -static` (produces a fully
 * static aarch64 ELF binary with no libc dependency on the target) and
 * then copied to the phone and executed there directly over SSH, so all
 * timings reported by this tool are REAL wall-clock measurements taken
 * on the actual target CPU, not modeled/extrapolated from the
 * workstation.
 *
 * Candidates (matching PROJECT SPEC section 5-7, section 13 timing
 * boundaries):
 *   - RAW RGB565            (source -> RGB565 conversion only)
 *   - RGB565 + LZ4          (current production-equivalent codec)
 *   - R4G4B4 + LZ4          (moderate quantization, FUSED single pass,
 *                            no redundant XRGB->RGB565->requant double
 *                            pass -- PROJECT SPEC section 5C)
 *   - RGB332 + LZ4          (aggressive quantization, fused single pass)
 *   - JPEG Q95 4:4:4 / Q90 4:4:4 / Q85 4:2:0 (baseline sequential,
 *     libjpeg-turbo, real NEON-accelerated encode, directly from the
 *     RGB888 view of the RGBA8888 source -- PROJECT SPEC section 7)
 *
 * Input: a raw RGBA8888LE frame file (width*height*4 bytes, tightly
 * packed, no stride padding) -- the same representation this project's
 * mirscreencast capture pipeline produces.
 *
 * Output: one JSON object per candidate with mean/p50/p90/p95/p99/max
 * timings (ns), MPixels/s, effective GiB/s and bytes/frame, matching
 * PROJECT SPEC section 13.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <jpeglib.h>
#include <setjmp.h>

#include "../../vendor/lz4.h"

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

struct stats {
	double mean_ns;
	uint64_t p50_ns, p90_ns, p95_ns, p99_ns, max_ns, min_ns;
};

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return (x > y) - (x < y);
}

static struct stats compute_stats(uint64_t *samples, int n)
{
	struct stats s;
	double sum = 0;
	int i;
	qsort(samples, n, sizeof(uint64_t), cmp_u64);
	for (i = 0; i < n; i++)
		sum += (double)samples[i];
	s.mean_ns = sum / n;
	s.p50_ns = samples[(int)(0.50 * (n - 1))];
	s.p90_ns = samples[(int)(0.90 * (n - 1))];
	s.p95_ns = samples[(int)(0.95 * (n - 1))];
	s.p99_ns = samples[(int)(0.99 * (n - 1))];
	s.max_ns = samples[n - 1];
	s.min_ns = samples[0];
	return s;
}

static void print_stats_json(FILE *out, const char *name, struct stats s,
			      size_t bytes_out, uint32_t w, uint32_t h,
			      int iterations, int is_last)
{
	double mpixels_s = ((double)w * h / 1e6) / (s.mean_ns / 1e9);
	double gib_s = ((double)w * h * 4 / (1024.0 * 1024 * 1024)) /
		       (s.mean_ns / 1e9);
	fprintf(out, "  {\n");
	fprintf(out, "    \"candidate\": \"%s\",\n", name);
	fprintf(out, "    \"width\": %u, \"height\": %u,\n", w, h);
	fprintf(out, "    \"iterations\": %d,\n", iterations);
	fprintf(out, "    \"bytes_out\": %zu,\n", bytes_out);
	fprintf(out, "    \"mean_ns\": %.1f,\n", s.mean_ns);
	fprintf(out, "    \"p50_ns\": %llu,\n", (unsigned long long)s.p50_ns);
	fprintf(out, "    \"p90_ns\": %llu,\n", (unsigned long long)s.p90_ns);
	fprintf(out, "    \"p95_ns\": %llu,\n", (unsigned long long)s.p95_ns);
	fprintf(out, "    \"p99_ns\": %llu,\n", (unsigned long long)s.p99_ns);
	fprintf(out, "    \"max_ns\": %llu,\n", (unsigned long long)s.max_ns);
	fprintf(out, "    \"min_ns\": %llu,\n", (unsigned long long)s.min_ns);
	fprintf(out, "    \"mpixels_s\": %.3f,\n", mpixels_s);
	fprintf(out, "    \"input_gib_s\": %.3f,\n", gib_s);
	fprintf(out, "    \"label\": \"MEASURED_OP6_SENDER\"\n");
	fprintf(out, "  }%s\n", is_last ? "" : ",");
}

/* ---- fused RGBA8888 -> RGB565 (truncating, matches project ingest) --- */
static inline uint16_t rgba_to_565(const uint8_t *p)
{
	uint16_t r = p[0] >> 3, g = p[1] >> 2, b = p[2] >> 3;
	return (uint16_t)((r << 11) | (g << 5) | b);
}

static void conv_rgba_to_rgb565(const uint8_t *rgba, uint32_t w, uint32_t h,
				 uint16_t *out)
{
	size_t n = (size_t)w * h, i;
	for (i = 0; i < n; i++)
		out[i] = rgba_to_565(&rgba[i * 4]);
}

/* ---- fused RGBA8888 -> R4G4B4-in-RGB565-word, single pass ---- */
static void conv_rgba_to_r4g4b4(const uint8_t *rgba, uint32_t w, uint32_t h,
				 uint16_t *out)
{
	size_t n = (size_t)w * h, i;
	for (i = 0; i < n; i++) {
		const uint8_t *p = &rgba[i * 4];
		uint16_t r5 = (uint16_t)(p[0] >> 3) & 0x1Eu; /* keep top4/5 */
		uint16_t g6 = (uint16_t)(p[1] >> 2) & 0x3Cu; /* keep top4/6 */
		uint16_t b5 = (uint16_t)(p[2] >> 3) & 0x1Eu; /* keep top4/5 */
		out[i] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
	}
}

/* ---- fused RGBA8888 -> RGB332 (1 byte/pixel), single pass ---- */
static void conv_rgba_to_rgb332(const uint8_t *rgba, uint32_t w, uint32_t h,
				 uint8_t *out)
{
	size_t n = (size_t)w * h, i;
	for (i = 0; i < n; i++) {
		const uint8_t *p = &rgba[i * 4];
		uint8_t r3 = (uint8_t)(p[0] >> 5);
		uint8_t g3 = (uint8_t)(p[1] >> 5);
		uint8_t b2 = (uint8_t)(p[2] >> 6);
		out[i] = (uint8_t)((r3 << 5) | (g3 << 2) | b2);
	}
}

/* ---- RGBA8888 -> RGB888 (drop alpha), for JPEG input ---- */
static void conv_rgba_to_rgb888(const uint8_t *rgba, uint32_t w, uint32_t h,
				 uint8_t *out)
{
	size_t n = (size_t)w * h, i;
	for (i = 0; i < n; i++) {
		out[i * 3 + 0] = rgba[i * 4 + 0];
		out[i * 3 + 1] = rgba[i * 4 + 1];
		out[i * 3 + 2] = rgba[i * 4 + 2];
	}
}

struct jpeg_mem_dest_mgr {
	struct jpeg_destination_mgr pub;
	uint8_t *buf;
	size_t cap;
	size_t used;
};

static void jmem_init_dest(j_compress_ptr cinfo)
{
	struct jpeg_mem_dest_mgr *d = (struct jpeg_mem_dest_mgr *)cinfo->dest;
	d->pub.next_output_byte = d->buf;
	d->pub.free_in_buffer = d->cap;
	d->used = 0;
}

static boolean jmem_empty_output(j_compress_ptr cinfo)
{
	(void)cinfo;
	fprintf(stderr, "FATAL: JPEG output buffer too small\n");
	exit(1);
	return TRUE;
}

static void jmem_term_dest(j_compress_ptr cinfo)
{
	struct jpeg_mem_dest_mgr *d = (struct jpeg_mem_dest_mgr *)cinfo->dest;
	d->used = d->cap - d->pub.free_in_buffer;
}

static size_t jpeg_encode_rgb888(struct jpeg_compress_struct *cinfo,
				  const uint8_t *rgb888, uint32_t w,
				  uint32_t h, int quality, int h2v2_subsample,
				  uint8_t *out, size_t out_cap)
{
	struct jpeg_mem_dest_mgr dest;
	struct jpeg_error_mgr jerr;
	cinfo->err = jpeg_std_error(&jerr);
	jpeg_create_compress(cinfo);
	dest.pub.init_destination = jmem_init_dest;
	dest.pub.empty_output_buffer = jmem_empty_output;
	dest.pub.term_destination = jmem_term_dest;
	dest.buf = out;
	dest.cap = out_cap;
	cinfo->dest = &dest.pub;
	cinfo->image_width = w;
	cinfo->image_height = h;
	cinfo->input_components = 3;
	cinfo->in_color_space = JCS_RGB;
	jpeg_set_defaults(cinfo);
	jpeg_set_quality(cinfo, quality, TRUE);
	cinfo->dct_method = JDCT_ISLOW; /* baseline sequential */
	if (!h2v2_subsample) {
		cinfo->comp_info[0].h_samp_factor = 1;
		cinfo->comp_info[0].v_samp_factor = 1;
	} else {
		cinfo->comp_info[0].h_samp_factor = 2;
		cinfo->comp_info[0].v_samp_factor = 2;
	}
	cinfo->comp_info[1].h_samp_factor = 1;
	cinfo->comp_info[1].v_samp_factor = 1;
	cinfo->comp_info[2].h_samp_factor = 1;
	cinfo->comp_info[2].v_samp_factor = 1;
	jpeg_start_compress(cinfo, TRUE);
	{
		JSAMPROW row_pointer[1];
		int row_stride = w * 3;
		while (cinfo->next_scanline < cinfo->image_height) {
			row_pointer[0] = (JSAMPROW)&rgb888[cinfo->next_scanline * row_stride];
			jpeg_write_scanlines(cinfo, row_pointer, 1);
		}
	}
	jpeg_finish_compress(cinfo);
	jpeg_destroy_compress(cinfo);
	return dest.used;
}

static void dump_bytes(const char *prefix, const char *suffix,
			const void *data, size_t n)
{
	if (!prefix)
		return;
	char path[1024];
	snprintf(path, sizeof(path), "%s.%s", prefix, suffix);
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	fwrite(data, 1, n, f);
	fclose(f);
}

int main(int argc, char **argv)
{
	if (argc < 6) {
		fprintf(stderr,
			"usage: %s <rgba8888_file> <width> <height> <iterations> <out_json>\n",
			argv[0]);
		return 2;
	}
	const char *path = argv[1];
	uint32_t w = (uint32_t)atoi(argv[2]);
	uint32_t h = (uint32_t)atoi(argv[3]);
	int iterations = atoi(argv[4]);
	const char *out_path = argv[5];
	const char *dump_prefix = argc > 6 ? argv[6] : NULL;

	size_t frame_bytes = (size_t)w * h * 4;
	FILE *f = fopen(path, "rb");
	if (!f) {
		perror("fopen");
		return 1;
	}
	uint8_t *rgba = malloc(frame_bytes);
	if (fread(rgba, 1, frame_bytes, f) != frame_bytes) {
		fprintf(stderr, "short read\n");
		return 1;
	}
	fclose(f);

	size_t npix = (size_t)w * h;
	uint16_t *rgb565_buf = malloc(npix * sizeof(uint16_t));
	uint8_t *rgb332_buf = malloc(npix);
	uint8_t *rgb888_buf = malloc(npix * 3);
	size_t lz4_cap = (size_t)LZ4_compressBound((int)(npix * sizeof(uint16_t)));
	uint8_t *lz4_buf = malloc(lz4_cap);
	size_t lz4_332_cap = (size_t)LZ4_compressBound((int)npix);
	uint8_t *lz4_332_buf = malloc(lz4_332_cap);
	size_t jpeg_cap = frame_bytes; /* generous upper bound for any quality */
	uint8_t *jpeg_buf = malloc(jpeg_cap);

	uint64_t *samples = malloc(sizeof(uint64_t) * (size_t)iterations);
	int warmup = iterations / 5 > 2 ? iterations / 5 : 2;

	FILE *jout = fopen(out_path, "w");
	fprintf(jout, "[\n");

	/* ---- RAW RGB565 (conversion only, no compression) ---- */
	{
		for (int i = 0; i < warmup; i++)
			conv_rgba_to_rgb565(rgba, w, h, rgb565_buf);
		for (int i = 0; i < iterations; i++) {
			uint64_t t0 = now_ns();
			conv_rgba_to_rgb565(rgba, w, h, rgb565_buf);
			samples[i] = now_ns() - t0;
		}
		struct stats s = compute_stats(samples, iterations);
		print_stats_json(jout, "raw-rgb565-conversion", s,
				  npix * 2, w, h, iterations, 0);
	}

	/* ---- RGB565 + LZ4 (current production-equivalent) ---- */
	{
		size_t last_bytes = 0;
		conv_rgba_to_rgb565(rgba, w, h, rgb565_buf);
		for (int i = 0; i < warmup; i++)
			LZ4_compress_default((const char *)rgb565_buf,
					      (char *)lz4_buf,
					      (int)(npix * 2), (int)lz4_cap);
		for (int i = 0; i < iterations; i++) {
			uint64_t t0 = now_ns();
			conv_rgba_to_rgb565(rgba, w, h, rgb565_buf);
			int n = LZ4_compress_default(
				(const char *)rgb565_buf, (char *)lz4_buf,
				(int)(npix * 2), (int)lz4_cap);
			samples[i] = now_ns() - t0;
			last_bytes = (size_t)n;
		}
		struct stats s = compute_stats(samples, iterations);
		print_stats_json(jout, "rgb565-lz4-current", s, last_bytes,
				  w, h, iterations, 0);
		dump_bytes(dump_prefix, "rgb565-lz4.bin", lz4_buf, last_bytes);
	}

	/* ---- R4G4B4 + LZ4 (fused quantization) ---- */
	{
		size_t last_bytes = 0;
		for (int i = 0; i < warmup; i++) {
			conv_rgba_to_r4g4b4(rgba, w, h, rgb565_buf);
			LZ4_compress_default((const char *)rgb565_buf,
					      (char *)lz4_buf,
					      (int)(npix * 2), (int)lz4_cap);
		}
		for (int i = 0; i < iterations; i++) {
			uint64_t t0 = now_ns();
			conv_rgba_to_r4g4b4(rgba, w, h, rgb565_buf);
			int n = LZ4_compress_default(
				(const char *)rgb565_buf, (char *)lz4_buf,
				(int)(npix * 2), (int)lz4_cap);
			samples[i] = now_ns() - t0;
			last_bytes = (size_t)n;
		}
		struct stats s = compute_stats(samples, iterations);
		print_stats_json(jout, "r4g4b4-lz4-fused", s, last_bytes, w,
				  h, iterations, 0);
		dump_bytes(dump_prefix, "r4g4b4-lz4.bin", lz4_buf, last_bytes);
	}

	/* ---- RGB332 + LZ4 (fused quantization) ---- */
	{
		size_t last_bytes = 0;
		for (int i = 0; i < warmup; i++) {
			conv_rgba_to_rgb332(rgba, w, h, rgb332_buf);
			LZ4_compress_default((const char *)rgb332_buf,
					      (char *)lz4_332_buf, (int)npix,
					      (int)lz4_332_cap);
		}
		for (int i = 0; i < iterations; i++) {
			uint64_t t0 = now_ns();
			conv_rgba_to_rgb332(rgba, w, h, rgb332_buf);
			int n = LZ4_compress_default(
				(const char *)rgb332_buf,
				(char *)lz4_332_buf, (int)npix,
				(int)lz4_332_cap);
			samples[i] = now_ns() - t0;
			last_bytes = (size_t)n;
		}
		struct stats s = compute_stats(samples, iterations);
		print_stats_json(jout, "rgb332-lz4-fused", s, last_bytes, w,
				  h, iterations, 0);
		dump_bytes(dump_prefix, "rgb332-lz4.bin", lz4_332_buf, last_bytes);
	}

	/* ---- JPEG Q95 4:4:4, Q90 4:4:4, Q85 4:2:0 ---- */
	{
		struct { const char *name; int quality; int subsample; } jcfgs[] = {
			{ "jpeg-q95-444", 95, 0 },
			{ "jpeg-q90-444", 90, 0 },
			{ "jpeg-q85-420", 85, 1 },
		};
		int nj = (int)(sizeof(jcfgs) / sizeof(jcfgs[0]));
		conv_rgba_to_rgb888(rgba, w, h, rgb888_buf);
		for (int j = 0; j < nj; j++) {
			struct jpeg_compress_struct cinfo;
			size_t last_bytes = 0;
			for (int i = 0; i < warmup; i++)
				jpeg_encode_rgb888(&cinfo, rgb888_buf, w, h,
						    jcfgs[j].quality,
						    jcfgs[j].subsample,
						    jpeg_buf, jpeg_cap);
			for (int i = 0; i < iterations; i++) {
				uint64_t t0 = now_ns();
				size_t n = jpeg_encode_rgb888(
					&cinfo, rgb888_buf, w, h,
					jcfgs[j].quality, jcfgs[j].subsample,
					jpeg_buf, jpeg_cap);
				samples[i] = now_ns() - t0;
				last_bytes = n;
			}
			struct stats s = compute_stats(samples, iterations);
			print_stats_json(jout, jcfgs[j].name, s, last_bytes,
					  w, h, iterations, 0);
			{
				char suffix[64];
				snprintf(suffix, sizeof(suffix), "%s.jpg",
					 jcfgs[j].name);
				dump_bytes(dump_prefix, suffix, jpeg_buf,
					   last_bytes);
			}
		}
	}

	/* ---- RGBA->RGB888 conversion cost alone (for JPEG sender-path
	 * accounting per PROJECT SPEC section 7) ---- */
	{
		for (int i = 0; i < warmup; i++)
			conv_rgba_to_rgb888(rgba, w, h, rgb888_buf);
		for (int i = 0; i < iterations; i++) {
			uint64_t t0 = now_ns();
			conv_rgba_to_rgb888(rgba, w, h, rgb888_buf);
			samples[i] = now_ns() - t0;
		}
		struct stats s = compute_stats(samples, iterations);
		print_stats_json(jout, "rgba-to-rgb888-conversion", s,
				  npix * 3, w, h, iterations, 1);
	}

	fprintf(jout, "]\n");
	fclose(jout);
	fprintf(stderr, "done: %s (%ux%u, %d iterations)\n", out_path, w, h,
		iterations);
	return 0;
}
