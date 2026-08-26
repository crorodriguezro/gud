/*
 * charls_codec.c - C3: JPEG-LS via CharLS (system libcharls.so.2, headers
 * vendored from the matching upstream tag -- see vendor/charls/README.md).
 * CharLS operates on planar/interleaved byte samples, not RGB565 directly,
 * so the RGB565<->RGB888 conversion is measured as part of encode/decode,
 * same as QOI/QOIR.
 *
 * Provides lossless (NEAR=0) and two near-lossless settings (NEAR=1, NEAR=3)
 * as required by PROJECT SPEC section 11 / C3.
 */
#include <stdlib.h>
#include <string.h>

#include "charls/charls.h"

#include "../codec.h"

static size_t charls_bound(uint32_t w, uint32_t h)
{
	/* A fixed "raw size + small margin" bound, and even CharLS's own
	 * charls_jpegls_encoder_get_estimated_destination_size(), were both
	 * found empirically to be *unsafe* upper bounds for incompressible
	 * (random noise) content: on a 256x256 noise frame,
	 * get_estimated_destination_size() returned 197666 bytes, but the
	 * encoder needed a >=294912-byte destination buffer to succeed
	 * (actual output was 209757 bytes -- CharLS appears to require
	 * working room beyond the final compacted bitstream size). This
	 * was caught by --verify (encode failures surfaced as unverifiable
	 * lossless results). A generous fixed 2x-raw-plus-margin bound
	 * comfortably covers this in testing across every corpus used by
	 * this harness.
	 */
	return (size_t)w * h * 3 * 2 + 4096;
}

static size_t charls_encode_generic(const uint16_t *src, uint32_t w,
				     uint32_t h, uint8_t *dst, size_t dst_cap,
				     int32_t near_lossless)
{
	uint8_t *rgb888 = malloc((size_t)w * h * 3);
	charls_jpegls_encoder *enc;
	charls_frame_info fi;
	size_t bytes_written = 0;
	size_t n = (size_t)-1;

	if (!rgb888)
		return (size_t)-1;

	{
		size_t i, n_px = (size_t)w * h;

		for (i = 0; i < n_px; i++)
			rgb565_to_rgb888(src[i], &rgb888[3 * i],
					  &rgb888[3 * i + 1],
					  &rgb888[3 * i + 2]);
	}

	enc = charls_jpegls_encoder_create();
	if (!enc) {
		free(rgb888);
		return (size_t)-1;
	}

	fi.width = w;
	fi.height = h;
	fi.bits_per_sample = 8;
	fi.component_count = 3;

	if (charls_jpegls_encoder_set_frame_info(enc, &fi) != 0)
		goto out;
	if (charls_jpegls_encoder_set_near_lossless(enc, near_lossless) != 0)
		goto out;
	if (charls_jpegls_encoder_set_interleave_mode(
		    enc, CHARLS_INTERLEAVE_MODE_SAMPLE) != 0)
		goto out;
	if (charls_jpegls_encoder_set_destination_buffer(enc, dst,
							  dst_cap) != 0)
		goto out;
	if (charls_jpegls_encoder_encode_from_buffer(
		    enc, rgb888, (size_t)w * h * 3, 0) != 0)
		goto out;
	if (charls_jpegls_encoder_get_bytes_written(enc, &bytes_written) != 0)
		goto out;
	n = bytes_written;

out:
	charls_jpegls_encoder_destroy(enc);
	free(rgb888);
	return n;
}

static int charls_decode_generic(const uint8_t *src, size_t src_len,
				  uint16_t *dst, uint32_t w, uint32_t h)
{
	charls_jpegls_decoder *dec = charls_jpegls_decoder_create();
	charls_frame_info fi;
	uint8_t *rgb888 = NULL;
	size_t dest_size = 0;
	int rc = -1;
	size_t i, n_px = (size_t)w * h;

	if (!dec)
		return -1;
	if (charls_jpegls_decoder_set_source_buffer(dec, src, src_len) != 0)
		goto out;
	if (charls_jpegls_decoder_read_header(dec) != 0)
		goto out;
	if (charls_jpegls_decoder_get_frame_info(dec, &fi) != 0)
		goto out;
	if (fi.width != w || fi.height != h || fi.component_count != 3) {
		goto out;
	}
	if (charls_jpegls_decoder_get_destination_size(dec, 0, &dest_size) !=
	    0)
		goto out;
	rgb888 = malloc(dest_size);
	if (!rgb888)
		goto out;
	if (charls_jpegls_decoder_decode_to_buffer(dec, rgb888, dest_size,
						    0) != 0)
		goto out;

	for (i = 0; i < n_px; i++) {
		uint8_t *p = rgb888 + 3 * i;

		dst[i] = rgb888_to_rgb565(p[0], p[1], p[2]);
	}
	rc = 0;

out:
	free(rgb888);
	charls_jpegls_decoder_destroy(dec);
	return rc;
}

#define DEFINE_CHARLS_CODEC(cname, near_val, codec_id, codec_label,       \
			     lossy_flag)                                    \
	static size_t cname##_encode(void *ctx, const uint16_t *src,       \
				      uint32_t w, uint32_t h, uint8_t *dst,  \
				      size_t cap)                            \
	{                                                                   \
		(void)ctx;                                                 \
		return charls_encode_generic(src, w, h, dst, cap, near_val); \
	}                                                                   \
	static int cname##_decode(void *ctx, const uint8_t *src,           \
				   size_t len, uint16_t *dst, uint32_t w,   \
				   uint32_t h)                              \
	{                                                                   \
		(void)ctx;                                                 \
		return charls_decode_generic(src, len, dst, w, h);          \
	}                                                                   \
	const fbcodec_desc fbcodec_##cname = {                             \
		.name = codec_id,                                          \
		.label = codec_label,                                      \
		.category = "external",                                     \
		.is_lossy = lossy_flag,                                      \
		.is_stateful = false,                                        \
		.native_rgb565 = false,                                      \
		.complexity = "high",                                        \
		.bound = charls_bound,                                       \
		.encode = cname##_encode,                                    \
		.decode = cname##_decode,                                    \
	}

DEFINE_CHARLS_CODEC(charls_lossless, 0, "charls-lossless",
		     "CharLS JPEG-LS lossless (C3)", false);
DEFINE_CHARLS_CODEC(charls_near1, 1, "charls-near1",
		     "CharLS JPEG-LS near-lossless NEAR=1 (C3)", true);
DEFINE_CHARLS_CODEC(charls_near3, 3, "charls-near3",
		     "CharLS JPEG-LS near-lossless NEAR=3 (C3)", true);

#undef DEFINE_CHARLS_CODEC
