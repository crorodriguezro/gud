/*
 * predictor_codecs.c - B1..B5, channel-aware and shuffle candidates, all
 * expressed as "apply one reversible transform, then LZ4" via the shared
 * generic_lz4_codec helpers. See transforms/predict.h for the transform
 * definitions and border-handling documentation.
 */
#include "../codec.h"
#include "generic_lz4_codec.h"

/* One macro instantiation per single-transform "<predictor>+LZ4" codec.
 * `cname` must be a valid C identifier; `pred_key` is the transform name
 * looked up via predict_find(); `codec_id`/`codec_label` are the
 * machine/human readable identifiers used in reports.
 */
#define DEFINE_1STEP_CODEC(cname, pred_key, codec_id, codec_label, cplx) \
	static size_t cname##_encode(void *ctx, const uint16_t *src,     \
				      uint32_t w, uint32_t h, uint8_t *dst, \
				      size_t cap)                          \
	{                                                                  \
		(void)ctx;                                                \
		return generic_1step_encode(predict_find(pred_key), src,  \
					     w, h, dst, cap);              \
	}                                                                  \
	static int cname##_decode(void *ctx, const uint8_t *src,          \
				   size_t len, uint16_t *dst, uint32_t w,  \
				   uint32_t h)                             \
	{                                                                  \
		(void)ctx;                                                \
		return generic_1step_decode(predict_find(pred_key), src,  \
					     len, dst, w, h);              \
	}                                                                  \
	const fbcodec_desc fbcodec_##cname = {                            \
		.name = codec_id,                                         \
		.label = codec_label,                                     \
		.category = "spatial",                                    \
		.is_lossy = false,                                        \
		.is_stateful = false,                                      \
		.native_rgb565 = true,                                     \
		.complexity = cplx,                                        \
		.create = NULL,                                            \
		.destroy = NULL,                                           \
		.reset = NULL,                                             \
		.bound = generic_lz4_bound,                                \
		.encode = cname##_encode,                                  \
		.decode = cname##_decode,                                  \
	}

DEFINE_1STEP_CODEC(sub_byte_lz4, "sub-byte", "sub-byte-lz4",
		    "PNG byte Sub + LZ4 (B1)", "low");
DEFINE_1STEP_CODEC(u16_sub_lz4, "u16-sub", "u16-sub-lz4",
		    "uint16 left-subtract + LZ4 (B2)", "low");
DEFINE_1STEP_CODEC(u16_xor_lz4, "u16-xor", "u16-xor-lz4",
		    "uint16 left-XOR + LZ4 (B3)", "low");
DEFINE_1STEP_CODEC(paeth_byte_lz4, "paeth-byte", "paeth-byte-lz4",
		    "PNG-style byte Paeth + LZ4 (B4a)", "medium");
DEFINE_1STEP_CODEC(paeth_pixel_lz4, "paeth-pixel", "paeth-pixel-lz4",
		    "RGB565-pixel Paeth + LZ4 (B4b)", "medium");
DEFINE_1STEP_CODEC(med_lz4, "med", "med-lz4",
		    "MED / JPEG-LS style predictor + LZ4 (B5)", "medium");
DEFINE_1STEP_CODEC(channel_sub_lz4, "channel-sub", "channel-sub-lz4",
		    "Channel-aware left-subtract + LZ4", "medium");
DEFINE_1STEP_CODEC(channel_xor_lz4, "channel-xor", "channel-xor-lz4",
		    "Channel-aware left-XOR + LZ4", "medium");
DEFINE_1STEP_CODEC(shuffle_lz4, "shuffle", "shuffle-lz4",
		    "RGB565 byte shuffle (S1) + LZ4", "low");

#undef DEFINE_1STEP_CODEC
