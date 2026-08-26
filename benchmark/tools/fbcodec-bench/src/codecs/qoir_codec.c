/*
 * qoir_codec.c - C2: QOIR reference implementation (upstream single-file
 * library). Provides one lossless and two lossy configurations
 * (lossiness=3, lossiness=5 on QOIR's 0..7 scale), as required by PROJECT
 * SPEC section 11 / C2. QOIR natively accepts an RGB (3 bytes/pixel)
 * pixel format, so the RGB565<->RGB888 conversion cost is still measured
 * as part of encode()/decode(), same as QOI.
 *
 * PROJECT SPEC next-phase Phase 3.D / Phase 7 additionally require the
 * RGB565<->RGB888 conversion cost to be reported *separately* from the
 * QOIR library's own encode/decode cost ("Do not hide RGB565 conversion
 * inside QOIR timing"). qoir_last_encode_breakdown_ns()/
 * qoir_last_decode_breakdown_ns() (declared in qoir_codec.h) expose the
 * most recent call's breakdown for that purpose, in addition to the
 * combined total the generic fbcodec_desc encode()/decode() interface
 * still returns (so every other existing measurement/report is
 * unaffected).
 */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define QOIR_IMPLEMENTATION
#include "../../vendor/qoir/qoir.h"

#include "../codec.h"
#include "qoir_codec.h"

static double g_last_encode_conversion_ns;
static double g_last_encode_codec_ns;
static double g_last_decode_conversion_ns;
static double g_last_decode_codec_ns;

static double now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

void qoir_last_encode_breakdown_ns(double *conversion_ns, double *codec_ns)
{
	if (conversion_ns)
		*conversion_ns = g_last_encode_conversion_ns;
	if (codec_ns)
		*codec_ns = g_last_encode_codec_ns;
}

void qoir_last_decode_breakdown_ns(double *conversion_ns, double *codec_ns)
{
	if (conversion_ns)
		*conversion_ns = g_last_decode_conversion_ns;
	if (codec_ns)
		*codec_ns = g_last_decode_codec_ns;
}

static size_t qoir_bound(uint32_t w, uint32_t h)
{
	/* Generous bound: worst case is close to raw size plus a small
	 * fixed header/tiling overhead; double it for safety margin.
	 */
	return (size_t)w * h * 3 * 2 + 4096;
}

static size_t qoir_encode_generic(const uint16_t *src, uint32_t w,
				   uint32_t h, uint8_t *dst, size_t dst_cap,
				   uint32_t lossiness)
{
	uint8_t *rgb888 = malloc((size_t)w * h * 3);
	qoir_pixel_buffer pixbuf;
	qoir_encode_options opts;
	qoir_encode_result res;
	size_t n;
	double t0, t1, t2;

	if (!rgb888)
		return (size_t)-1;

	t0 = now_ns();
	{
		size_t i, n_px = (size_t)w * h;

		for (i = 0; i < n_px; i++)
			rgb565_to_rgb888(src[i], &rgb888[3 * i],
					  &rgb888[3 * i + 1],
					  &rgb888[3 * i + 2]);
	}
	t1 = now_ns();

	memset(&pixbuf, 0, sizeof(pixbuf));
	pixbuf.pixcfg.pixfmt = QOIR_PIXEL_FORMAT__RGB;
	pixbuf.pixcfg.width_in_pixels = w;
	pixbuf.pixcfg.height_in_pixels = h;
	pixbuf.data = rgb888;
	pixbuf.stride_in_bytes = (size_t)w * 3;

	memset(&opts, 0, sizeof(opts));
	opts.lossiness = lossiness;

	res = qoir_encode(&pixbuf, &opts);
	t2 = now_ns();
	g_last_encode_conversion_ns = t1 - t0;
	g_last_encode_codec_ns = t2 - t1;

	free(rgb888);
	if (res.status_message != NULL) {
		free(res.owned_memory);
		return (size_t)-1;
	}
	if (res.dst_len > dst_cap) {
		free(res.owned_memory);
		return (size_t)-1;
	}
	memcpy(dst, res.dst_ptr, res.dst_len);
	n = res.dst_len;
	free(res.owned_memory);
	return n;
}

static int qoir_decode_generic(const uint8_t *src, size_t src_len,
				uint16_t *dst, uint32_t w, uint32_t h)
{
	qoir_decode_options opts;
	qoir_decode_result res;
	size_t i, n_px = (size_t)w * h;
	double t0, t1, t2;

	memset(&opts, 0, sizeof(opts));
	opts.pixfmt = QOIR_PIXEL_FORMAT__RGB;

	t0 = now_ns();
	res = qoir_decode(src, src_len, &opts);
	t1 = now_ns();
	if (res.status_message != NULL) {
		free(res.owned_memory);
		return -1;
	}
	if (res.dst_pixbuf.pixcfg.width_in_pixels != w ||
	    res.dst_pixbuf.pixcfg.height_in_pixels != h) {
		free(res.owned_memory);
		return -1;
	}
	for (i = 0; i < n_px; i++) {
		const uint8_t *row =
			res.dst_pixbuf.data +
			(i / w) * res.dst_pixbuf.stride_in_bytes;
		const uint8_t *p = row + (i % w) * 3;

		dst[i] = rgb888_to_rgb565(p[0], p[1], p[2]);
	}
	t2 = now_ns();
	g_last_decode_codec_ns = t1 - t0;
	g_last_decode_conversion_ns = t2 - t1;
	free(res.owned_memory);
	return 0;
}

#define DEFINE_QOIR_CODEC(cname, lossiness_val, codec_id, codec_label,     \
			   lossy_flag)                                       \
	static size_t cname##_encode(void *ctx, const uint16_t *src,        \
				      uint32_t w, uint32_t h, uint8_t *dst,   \
				      size_t cap)                             \
	{                                                                    \
		(void)ctx;                                                  \
		return qoir_encode_generic(src, w, h, dst, cap,              \
					    lossiness_val);                   \
	}                                                                    \
	static int cname##_decode(void *ctx, const uint8_t *src,            \
				   size_t len, uint16_t *dst, uint32_t w,    \
				   uint32_t h)                               \
	{                                                                    \
		(void)ctx;                                                  \
		return qoir_decode_generic(src, len, dst, w, h);             \
	}                                                                    \
	const fbcodec_desc fbcodec_##cname = {                              \
		.name = codec_id,                                           \
		.label = codec_label,                                       \
		.category = "external",                                     \
		.is_lossy = lossy_flag,                                      \
		.is_stateful = false,                                        \
		.native_rgb565 = false,                                      \
		.complexity = "medium",                                      \
		.bound = qoir_bound,                                         \
		.encode = cname##_encode,                                    \
		.decode = cname##_decode,                                    \
	}

DEFINE_QOIR_CODEC(qoir_lossless, 0, "qoir-lossless",
		   "QOIR lossless (C2)", false);
DEFINE_QOIR_CODEC(qoir_lossy3, 3, "qoir-lossy-l3",
		   "QOIR lossy, lossiness=3 (C2)", true);
DEFINE_QOIR_CODEC(qoir_lossy5, 5, "qoir-lossy-l5",
		   "QOIR lossy, lossiness=5 (C2)", true);

#undef DEFINE_QOIR_CODEC
