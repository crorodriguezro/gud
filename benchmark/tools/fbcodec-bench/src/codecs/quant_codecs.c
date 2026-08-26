/*
 * quant_codecs.c - Q1/Q2/Q3 quantization candidates (PROJECT SPEC section 6)
 * and the quantization + predictor combinations required by section 7.
 *
 * Q1 (mild) and Q2-16bit (moderate) keep 16-bit RGB565 storage and reuse the
 * generic same-size transform + LZ4 plumbing. Q2-packed (12 bpp) and Q3
 * (RGB332, 8 bpp) actually shrink the stored representation, so packing and
 * unpacking cost is measured as part of encode()/decode() below, per the
 * spec's explicit requirement.
 */
#include <stdlib.h>
#include <string.h>

#include "../codec.h"
#include "../lz4_util.h"
#include "../transforms/quant.h"
#include "generic_lz4_codec.h"

static const rgb565_transform quant_mild_transform = {
	"quant-mild", quant_mild_fwd, quant_identity_inv
};
static const rgb565_transform quant_moderate_transform = {
	"quant-moderate", quant_moderate_fwd, quant_identity_inv
};

/* ---- Q1 mild: raw (no LZ4), demonstrates unchanged bytes/pixel. ---- */

static size_t q1_raw_bound(uint32_t w, uint32_t h)
{
	return rgb565_byte_size(w, h);
}

static size_t q1_raw_encode(void *ctx, const uint16_t *src, uint32_t w,
			     uint32_t h, uint8_t *dst, size_t cap)
{
	size_t bytes = rgb565_byte_size(w, h);

	(void)ctx;
	if (cap < bytes)
		return (size_t)-1;
	quant_mild_fwd(src, w, h, dst);
	return bytes;
}

static int q1_raw_decode(void *ctx, const uint8_t *src, size_t len,
			  uint16_t *dst, uint32_t w, uint32_t h)
{
	size_t bytes = rgb565_byte_size(w, h);

	(void)ctx;
	if (len != bytes)
		return -1;
	memcpy(dst, src, bytes);
	return 0;
}

const fbcodec_desc fbcodec_quant_mild_raw = {
	.name = "quant-mild-raw",
	.label = "Q1 mild quantization, raw 16-bit storage (no LZ4)",
	.category = "quant",
	.is_lossy = true,
	.is_stateful = false,
	.native_rgb565 = true,
	.complexity = "low",
	.bound = q1_raw_bound,
	.encode = q1_raw_encode,
	.decode = q1_raw_decode,
};

/* ---- Q1 mild + LZ4 ---- */

static size_t q1_lz4_encode(void *ctx, const uint16_t *src, uint32_t w,
			     uint32_t h, uint8_t *dst, size_t cap)
{
	(void)ctx;
	return generic_1step_encode(&quant_mild_transform, src, w, h, dst,
				     cap);
}

static int q1_lz4_decode(void *ctx, const uint8_t *src, size_t len,
			  uint16_t *dst, uint32_t w, uint32_t h)
{
	(void)ctx;
	return generic_1step_decode(&quant_mild_transform, src, len, dst, w,
				     h);
}

const fbcodec_desc fbcodec_quant_mild_lz4 = {
	.name = "quant-mild-lz4",
	.label = "Q1 mild quantization + LZ4",
	.category = "quant",
	.is_lossy = true,
	.is_stateful = false,
	.native_rgb565 = true,
	.complexity = "low",
	.bound = generic_lz4_bound,
	.encode = q1_lz4_encode,
	.decode = q1_lz4_decode,
};

/* ---- Q2 moderate, 16-bit storage + LZ4 ---- */

static size_t q2_16_encode(void *ctx, const uint16_t *src, uint32_t w,
			    uint32_t h, uint8_t *dst, size_t cap)
{
	(void)ctx;
	return generic_1step_encode(&quant_moderate_transform, src, w, h,
				     dst, cap);
}

static int q2_16_decode(void *ctx, const uint8_t *src, size_t len,
			 uint16_t *dst, uint32_t w, uint32_t h)
{
	(void)ctx;
	return generic_1step_decode(&quant_moderate_transform, src, len, dst,
				     w, h);
}

const fbcodec_desc fbcodec_quant_moderate_16_lz4 = {
	.name = "quant-moderate-16bit-lz4",
	.label = "Q2 moderate (RGB444-equiv) 16-bit storage + LZ4",
	.category = "quant",
	.is_lossy = true,
	.is_stateful = false,
	.native_rgb565 = true,
	.complexity = "low",
	.bound = generic_lz4_bound,
	.encode = q2_16_encode,
	.decode = q2_16_decode,
};

/* ---- Q2 moderate, packed 12 bpp + LZ4 (packing cost included) ---- */

static size_t q2_packed_bound(uint32_t w, uint32_t h)
{
	return lz4u_compress_bound(quant_rgb444_packed_bound(w, h));
}

static size_t q2_packed_encode(void *ctx, const uint16_t *src, uint32_t w,
				uint32_t h, uint8_t *dst, size_t cap)
{
	size_t packed_bound = quant_rgb444_packed_bound(w, h);
	uint8_t *packed = malloc(packed_bound ? packed_bound : 1);
	size_t packed_len, n;

	(void)ctx;
	if (!packed)
		return (size_t)-1;
	packed_len = quant_rgb444_pack(src, w, h, packed);
	n = lz4u_compress(packed, packed_len, dst, cap);
	free(packed);
	return n;
}

static int q2_packed_decode(void *ctx, const uint8_t *src, size_t len,
			     uint16_t *dst, uint32_t w, uint32_t h)
{
	size_t packed_bound = quant_rgb444_packed_bound(w, h);
	uint8_t *packed = malloc(packed_bound ? packed_bound : 1);
	size_t out_len = 0;
	int rc = 0;

	(void)ctx;
	if (!packed)
		return -1;
	if (lz4u_decompress(src, len, packed, packed_bound, &out_len) != 0 ||
	    out_len != packed_bound) {
		rc = -1;
		goto out;
	}
	quant_rgb444_unpack(packed, w, h, dst);
out:
	free(packed);
	return rc;
}

const fbcodec_desc fbcodec_quant_moderate_packed_lz4 = {
	.name = "quant-moderate-packed12-lz4",
	.label = "Q2 moderate, packed 12 bpp + LZ4",
	.category = "quant",
	.is_lossy = true,
	.is_stateful = false,
	.native_rgb565 = false,
	.complexity = "medium",
	.bound = q2_packed_bound,
	.encode = q2_packed_encode,
	.decode = q2_packed_decode,
};

/* ---- Q3 aggressive: RGB332, 1 byte/pixel, raw (no LZ4) ---- */

static size_t q3_raw_bound(uint32_t w, uint32_t h)
{
	return quant_rgb332_bound(w, h);
}

static size_t q3_raw_encode(void *ctx, const uint16_t *src, uint32_t w,
			     uint32_t h, uint8_t *dst, size_t cap)
{
	(void)ctx;
	if (cap < quant_rgb332_bound(w, h))
		return (size_t)-1;
	return quant_rgb332_pack(src, w, h, dst);
}

static int q3_raw_decode(void *ctx, const uint8_t *src, size_t len,
			  uint16_t *dst, uint32_t w, uint32_t h)
{
	(void)ctx;
	if (len != quant_rgb332_bound(w, h))
		return -1;
	quant_rgb332_unpack(src, w, h, dst);
	return 0;
}

const fbcodec_desc fbcodec_quant_aggressive_raw = {
	.name = "quant-aggressive-raw",
	.label = "Q3 RGB332, raw 1 byte/pixel (no LZ4)",
	.category = "quant",
	.is_lossy = true,
	.is_stateful = false,
	.native_rgb565 = false,
	.complexity = "low",
	.bound = q3_raw_bound,
	.encode = q3_raw_encode,
	.decode = q3_raw_decode,
};

/* ---- Q3 aggressive + LZ4 ---- */

static size_t q3_lz4_bound(uint32_t w, uint32_t h)
{
	return lz4u_compress_bound(quant_rgb332_bound(w, h));
}

static size_t q3_lz4_encode(void *ctx, const uint16_t *src, uint32_t w,
			     uint32_t h, uint8_t *dst, size_t cap)
{
	size_t raw_len = quant_rgb332_bound(w, h);
	uint8_t *raw = malloc(raw_len ? raw_len : 1);
	size_t n;

	(void)ctx;
	if (!raw)
		return (size_t)-1;
	quant_rgb332_pack(src, w, h, raw);
	n = lz4u_compress(raw, raw_len, dst, cap);
	free(raw);
	return n;
}

static int q3_lz4_decode(void *ctx, const uint8_t *src, size_t len,
			  uint16_t *dst, uint32_t w, uint32_t h)
{
	size_t raw_len = quant_rgb332_bound(w, h);
	uint8_t *raw = malloc(raw_len ? raw_len : 1);
	size_t out_len = 0;
	int rc = 0;

	(void)ctx;
	if (!raw)
		return -1;
	if (lz4u_decompress(src, len, raw, raw_len, &out_len) != 0 ||
	    out_len != raw_len) {
		rc = -1;
		goto out;
	}
	quant_rgb332_unpack(raw, w, h, dst);
out:
	free(raw);
	return rc;
}

const fbcodec_desc fbcodec_quant_aggressive_lz4 = {
	.name = "quant-aggressive-lz4",
	.label = "Q3 RGB332 + LZ4",
	.category = "quant",
	.is_lossy = true,
	.is_stateful = false,
	.native_rgb565 = false,
	.complexity = "low",
	.bound = q3_lz4_bound,
	.encode = q3_lz4_encode,
	.decode = q3_lz4_decode,
};

/* ---- Combinations (section 7): quant + predictor + LZ4 ---- */

#define DEFINE_COMBO_CODEC(cname, step1, pred_key, codec_id, codec_label)  \
	static size_t cname##_encode(void *ctx, const uint16_t *src,       \
				      uint32_t w, uint32_t h, uint8_t *dst,  \
				      size_t cap)                           \
	{                                                                   \
		(void)ctx;                                                 \
		return generic_2step_encode(&step1, predict_find(pred_key), \
					     src, w, h, dst, cap);          \
	}                                                                   \
	static int cname##_decode(void *ctx, const uint8_t *src,           \
				   size_t len, uint16_t *dst, uint32_t w,   \
				   uint32_t h)                              \
	{                                                                   \
		(void)ctx;                                                 \
		return generic_2step_decode(&step1, predict_find(pred_key), \
					     src, len, dst, w, h);          \
	}                                                                   \
	const fbcodec_desc fbcodec_##cname = {                             \
		.name = codec_id,                                          \
		.label = codec_label,                                      \
		.category = "combo",                                       \
		.is_lossy = true,                                          \
		.is_stateful = false,                                       \
		.native_rgb565 = true,                                      \
		.complexity = "medium",                                     \
		.bound = generic_lz4_bound,                                 \
		.encode = cname##_encode,                                   \
		.decode = cname##_decode,                                   \
	}

DEFINE_COMBO_CODEC(quant_mild_u16sub_lz4, quant_mild_transform, "u16-sub",
		    "quant-mild-u16sub-lz4",
		    "Q1 mild quant + uint16 left-sub + LZ4");
DEFINE_COMBO_CODEC(quant_mild_u16xor_lz4, quant_mild_transform, "u16-xor",
		    "quant-mild-u16xor-lz4",
		    "Q1 mild quant + uint16 left-XOR + LZ4");
DEFINE_COMBO_CODEC(quant_moderate_bestpred_lz4, quant_moderate_transform,
		    "u16-xor", "quant-moderate-bestpred-lz4",
		    "Q2 moderate quant + best predictor (u16-XOR) + LZ4");

/* ---- S2: quantization + byte shuffle + LZ4 ---- */
DEFINE_COMBO_CODEC(quant_shuffle_lz4, quant_mild_transform, "shuffle",
		    "quant-shuffle-lz4",
		    "Q1 mild quant + byte shuffle (S2) + LZ4");

#undef DEFINE_COMBO_CODEC
