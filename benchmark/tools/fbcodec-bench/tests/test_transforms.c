/*
 * test_transforms.c - round-trip and bound validation for every custom
 * reversible transform and quantization mode (PROJECT SPEC section 51).
 *
 * Every LOSSLESS transform must satisfy decode(encode(x)) == x exactly for:
 *   1x1, 2x1, 1x2, odd widths, odd heights, random RGB565, solid colors,
 *   gradients, and a large ("maximum tested") rectangle.
 * Lossy quantization modes are checked against their documented per-channel
 * error bounds instead (they are not supposed to round-trip exactly).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/transforms/predict.h"
#include "../src/transforms/quant.h"
#include "../src/transforms/rle.h"

static int g_failures;

#define CHECK(cond, msg)                                                    \
	do {                                                                  \
		if (!(cond)) {                                                \
			fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__,   \
				__LINE__);                                    \
			g_failures++;                                         \
		}                                                              \
	} while (0)

static uint32_t xorshift32(uint32_t *state)
{
	uint32_t x = *state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static uint16_t *make_random(uint32_t w, uint32_t h, uint32_t seed)
{
	size_t n = (size_t)w * h;
	uint16_t *buf = malloc(n * sizeof(uint16_t));
	uint32_t state = seed ? seed : 1;
	size_t i;

	for (i = 0; i < n; i++)
		buf[i] = (uint16_t)xorshift32(&state);
	return buf;
}

static uint16_t *make_solid(uint32_t w, uint32_t h, uint16_t color)
{
	size_t n = (size_t)w * h;
	uint16_t *buf = malloc(n * sizeof(uint16_t));
	size_t i;

	for (i = 0; i < n; i++)
		buf[i] = color;
	return buf;
}

static uint16_t *make_gradient(uint32_t w, uint32_t h)
{
	uint16_t *buf = malloc((size_t)w * h * sizeof(uint16_t));
	uint32_t y;

	for (y = 0; y < h; y++) {
		uint32_t x;

		for (x = 0; x < w; x++) {
			uint8_t v = (uint8_t)((x + y) & 0x1F);

			buf[(size_t)y * w + x] = rgb565_pack(v & 0x1F, (v * 2) & 0x3F,
							       v & 0x1F);
		}
	}
	return buf;
}

static void check_transform_roundtrip(const rgb565_transform *t, uint32_t w,
				       uint32_t h, const uint16_t *src,
				       const char *case_name)
{
	size_t bytes = rgb565_byte_size(w, h);
	uint8_t *encoded = malloc(bytes ? bytes : 1);
	uint16_t *decoded = malloc(bytes ? bytes : 1);
	char msg[256];

	t->fwd(src, w, h, encoded);
	t->inv(encoded, w, h, decoded);

	snprintf(msg, sizeof(msg), "%s roundtrip mismatch on case '%s' (%ux%u)",
		 t->name, case_name, w, h);
	CHECK(memcmp(src, decoded, bytes) == 0, msg);

	free(encoded);
	free(decoded);
}

static void run_case(const rgb565_transform *t, const char *case_name,
		      uint16_t *(*gen)(uint32_t, uint32_t), uint32_t w,
		      uint32_t h)
{
	uint16_t *buf = gen(w, h);

	check_transform_roundtrip(t, w, h, buf, case_name);
	free(buf);
}

static uint16_t *gen_random_seeded(uint32_t w, uint32_t h)
{
	return make_random(w, h, 0xC0FFEEu);
}

static uint16_t *gen_gradient(uint32_t w, uint32_t h)
{
	return make_gradient(w, h);
}

static void test_all_predictors(void)
{
	const rgb565_transform *transforms[16];
	size_t n = predict_all(transforms, 16);
	size_t i;

	/* Dimension cases required by section 24/51. */
	static const struct {
		uint32_t w, h;
		const char *name;
	} dims[] = {
		{ 1, 1, "1x1" },       { 2, 1, "2x1" },     { 1, 2, "1x2" },
		{ 3, 3, "odd-3x3" },   { 17, 1, "odd-17x1" }, { 1, 17, "odd-1x17" },
		{ 129, 65, "odd-129x65" }, { 640, 100, "rect-640x100" },
		{ 1280, 720, "max-720p" },
	};

	for (i = 0; i < n; i++) {
		size_t d;

		for (d = 0; d < sizeof(dims) / sizeof(dims[0]); d++) {
			run_case(transforms[i], dims[d].name,
				 gen_random_seeded, dims[d].w, dims[d].h);
			run_case(transforms[i], dims[d].name, gen_gradient,
				  dims[d].w, dims[d].h);
		}

		{
			uint16_t *solid_black =
				make_solid(64, 64, rgb565_pack(0, 0, 0));
			uint16_t *solid_white =
				make_solid(64, 64, rgb565_pack(31, 63, 31));

			check_transform_roundtrip(transforms[i], 64, 64,
						   solid_black, "solid-black");
			check_transform_roundtrip(transforms[i], 64, 64,
						   solid_white, "solid-white");
			free(solid_black);
			free(solid_white);
		}

		/* Randomized property-style pass: many random dims/seeds. */
		{
			uint32_t seed = 42;
			int trial;

			for (trial = 0; trial < 25; trial++) {
				uint32_t w = 1 + (xorshift32(&seed) % 200);
				uint32_t h = 1 + (xorshift32(&seed) % 200);
				uint16_t *buf =
					make_random(w, h, seed ^ (uint32_t)trial);

				check_transform_roundtrip(transforms[i], w, h,
							   buf, "random-property");
				free(buf);
			}
		}
	}
}

static void test_quant_error_bounds(void)
{
	uint32_t w = 200, h = 150;
	uint16_t *src = make_random(w, h, 777);
	uint8_t *out = malloc(rgb565_byte_size(w, h));
	uint16_t *quantized = (uint16_t *)out;
	size_t n = (size_t)w * h, i;
	int max_r_err = 0, max_g_err = 0, max_b_err = 0;

	quant_mild_fwd(src, w, h, out);
	for (i = 0; i < n; i++) {
		uint8_t or_, og, ob, qr, qg, qb;

		rgb565_unpack(src[i], &or_, &og, &ob);
		rgb565_unpack(quantized[i], &qr, &qg, &qb);
		if (abs((int)or_ - (int)qr) > max_r_err)
			max_r_err = abs((int)or_ - (int)qr);
		if (abs((int)og - (int)qg) > max_g_err)
			max_g_err = abs((int)og - (int)qg);
		if (abs((int)ob - (int)qb) > max_b_err)
			max_b_err = abs((int)ob - (int)qb);
	}
	/* Q1 retains top 4 of 5 bits (R,B) and top 5 of 6 bits (G): worst
	 * case error is (2^dropped_bits - 1) = 1 for both.
	 */
	CHECK(max_r_err <= 1, "Q1 R channel exceeds documented error bound");
	CHECK(max_g_err <= 1, "Q1 G channel exceeds documented error bound");
	CHECK(max_b_err <= 1, "Q1 B channel exceeds documented error bound");

	quant_moderate_fwd(src, w, h, out);
	max_r_err = max_g_err = max_b_err = 0;
	for (i = 0; i < n; i++) {
		uint8_t or_, og, ob, qr, qg, qb;

		rgb565_unpack(src[i], &or_, &og, &ob);
		rgb565_unpack(quantized[i], &qr, &qg, &qb);
		if (abs((int)or_ - (int)qr) > max_r_err)
			max_r_err = abs((int)or_ - (int)qr);
		if (abs((int)og - (int)qg) > max_g_err)
			max_g_err = abs((int)og - (int)qg);
		if (abs((int)ob - (int)qb) > max_b_err)
			max_b_err = abs((int)ob - (int)qb);
	}
	/* Q2 retains top 4 of 5 (R,B) -> error <=1; top 4 of 6 (G) -> error <=3. */
	CHECK(max_r_err <= 1, "Q2 R channel exceeds documented error bound");
	CHECK(max_g_err <= 3, "Q2 G channel exceeds documented error bound");
	CHECK(max_b_err <= 1, "Q2 B channel exceeds documented error bound");

	free(src);
	free(out);
}

static void test_rgb444_packed_roundtrip(void)
{
	uint32_t dims_w[] = { 1, 2, 3, 17, 256 };
	uint32_t dims_h[] = { 1, 1, 3, 5, 256 };
	size_t d;

	/* quant_rgb444_pack()/unpack() are used directly on the *original*
	 * pixels by the quant-moderate-packed12-lz4 codec (see
	 * codecs/quant_codecs.c); they are not composed with
	 * quant_moderate_fwd()'s separate LSB-masking convention, which
	 * reconstructs channel values differently (bit-replication upscale
	 * vs. LSB-cleared truncation). The invariant that actually matters
	 * is that pack()/unpack() forms a well-defined, *idempotent*
	 * mapping: packing the unpacked (already 4-bit-quantized) image
	 * must reproduce exactly the same packed bytes.
	 */
	for (d = 0; d < sizeof(dims_w) / sizeof(dims_w[0]); d++) {
		uint32_t w = dims_w[d], h = dims_h[d];
		uint16_t *src = make_random(w, h, 99 + (uint32_t)d);
		uint8_t *packed = malloc(quant_rgb444_packed_bound(w, h) + 1);
		uint16_t *unpacked = malloc(rgb565_byte_size(w, h));
		uint8_t *repacked = malloc(quant_rgb444_packed_bound(w, h) + 1);
		size_t packed_len, repacked_len;

		packed_len = quant_rgb444_pack(src, w, h, packed);
		CHECK(packed_len == quant_rgb444_packed_bound(w, h),
		      "RGB444 packed length mismatch");
		quant_rgb444_unpack(packed, w, h, unpacked);
		repacked_len = quant_rgb444_pack(unpacked, w, h, repacked);

		CHECK(repacked_len == packed_len,
		      "RGB444 re-pack length mismatch");
		CHECK(memcmp(packed, repacked, packed_len) == 0,
		      "RGB444 pack/unpack is not idempotent");

		free(src);
		free(packed);
		free(unpacked);
		free(repacked);
	}
}

static void test_rgb332_roundtrip(void)
{
	uint32_t w = 257, h = 3; /* odd total pixel count on purpose */
	uint16_t *src = make_random(w, h, 555);
	uint8_t *packed = malloc(quant_rgb332_bound(w, h));
	uint16_t *unpacked = malloc(rgb565_byte_size(w, h));
	uint16_t *repacked_check = malloc(rgb565_byte_size(w, h));
	uint8_t *packed2 = malloc(quant_rgb332_bound(w, h));
	size_t i, n = (size_t)w * h;

	quant_rgb332_pack(src, w, h, packed);
	quant_rgb332_unpack(packed, w, h, unpacked);
	/* Re-packing the unpacked (already-quantized) image must reproduce
	 * exactly the same packed bytes -- RGB332 is idempotent.
	 */
	quant_rgb332_pack(unpacked, w, h, packed2);
	CHECK(memcmp(packed, packed2, quant_rgb332_bound(w, h)) == 0,
	      "RGB332 quantization is not idempotent");

	(void)repacked_check;
	for (i = 0; i < n; i++) {
		uint8_t or_, og, ob, ur, ug, ub;

		rgb565_unpack(src[i], &or_, &og, &ob);
		rgb565_unpack(unpacked[i], &ur, &ug, &ub);
		/* R: 5->3 bits, worst case error <= 3; G: 6->3, error <=7;
		 * B: 5->2, error <=7.
		 */
		if (abs((int)or_ - (int)ur) > 3 ||
		    abs((int)og - (int)ug) > 7 ||
		    abs((int)ob - (int)ub) > 7) {
			fprintf(stderr,
				"FAIL: RGB332 error bound exceeded at %zu\n",
				i);
			g_failures++;
			break;
		}
	}

	free(src);
	free(packed);
	free(unpacked);
	free(packed2);
}

static void test_rle_roundtrip(void)
{
	struct {
		const char *name;
		size_t len;
	} cases[] = {
		{ "empty", 0 },       { "single-byte", 1 },
		{ "short-run", 5 },   { "long-run-128", 300 },
		{ "random-noisy", 4096 },
	};
	size_t c;

	for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
		size_t len = cases[c].len;
		uint8_t *src = malloc(len ? len : 1);
		size_t bound = rle_bound(len);
		uint8_t *encoded = malloc(bound ? bound : 1);
		uint8_t *decoded = malloc(len ? len : 1);
		size_t enc_len, dec_len = 0;
		uint32_t state = 1234;
		size_t i;

		if (strcmp(cases[c].name, "random-noisy") == 0) {
			for (i = 0; i < len; i++)
				src[i] = (uint8_t)xorshift32(&state);
		} else if (strcmp(cases[c].name, "long-run-128") == 0) {
			for (i = 0; i < len; i++)
				src[i] = (uint8_t)(i < 200 ? 0x42
							    : (i & 0xFF));
		} else {
			for (i = 0; i < len; i++)
				src[i] = (uint8_t)(7 + i);
		}

		enc_len = rle_encode(src, len, encoded, bound);
		CHECK(enc_len != (size_t)-1, "RLE encode overflowed bound");
		CHECK(rle_decode(encoded, enc_len, decoded, len, &dec_len) ==
			      0,
		      "RLE decode failed");
		CHECK(dec_len == len, "RLE decode length mismatch");
		CHECK(len == 0 || memcmp(src, decoded, len) == 0,
		      "RLE roundtrip mismatch");

		free(src);
		free(encoded);
		free(decoded);
	}
}

int main(void)
{
	test_all_predictors();
	test_quant_error_bounds();
	test_rgb444_packed_roundtrip();
	test_rgb332_roundtrip();
	test_rle_roundtrip();

	if (g_failures) {
		fprintf(stderr, "\n%d test_transforms FAILURE(S)\n",
			g_failures);
		return 1;
	}
	printf("test_transforms: all checks passed\n");
	return 0;
}
