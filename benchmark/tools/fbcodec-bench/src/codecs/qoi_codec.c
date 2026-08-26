/*
 * qoi_codec.c - C1: QOI reference implementation. Standard QOI expects
 * RGB888/RGBA8888, so RGB565 -> RGB888 -> QOI -> RGB888 -> RGB565 conversion
 * happens on both sides *and is included in the measured encode/decode
 * time*, per PROJECT SPEC section 11 / C1.
 */
#include <stdlib.h>
#include <string.h>

#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "../../vendor/qoi/qoi.h"

#include "../codec.h"

static size_t qoi_bound(uint32_t w, uint32_t h)
{
	/* Exact worst case per qoi_encode()'s own formula: 4 bytes/pixel
	 * (channels+1, channels=3 for the RGB path we use) plus header and
	 * padding. Anything smaller can make qoi_encode's output not fit,
	 * which showed up as spurious encode failures on incompressible
	 * (random noise) corpora during the first full benchmark pass.
	 */
	return (size_t)w * h * 4 + 14 /* QOI_HEADER_SIZE */ +
	       sizeof(qoi_padding);
}

static size_t qoi_codec_encode(void *ctx, const uint16_t *src, uint32_t w,
				uint32_t h, uint8_t *dst, size_t dst_cap)
{
	uint8_t *rgb888 = malloc((size_t)w * h * 3);
	qoi_desc desc;
	int out_len = 0;
	void *encoded;
	size_t n;

	(void)ctx;
	if (!rgb888)
		return (size_t)-1;

	{
		size_t i, n_px = (size_t)w * h;

		for (i = 0; i < n_px; i++)
			rgb565_to_rgb888(src[i], &rgb888[3 * i],
					  &rgb888[3 * i + 1],
					  &rgb888[3 * i + 2]);
	}

	desc.width = w;
	desc.height = h;
	desc.channels = 3;
	desc.colorspace = QOI_SRGB;

	encoded = qoi_encode(rgb888, &desc, &out_len);
	free(rgb888);
	if (!encoded)
		return (size_t)-1;
	if ((size_t)out_len > dst_cap) {
		free(encoded);
		return (size_t)-1;
	}
	memcpy(dst, encoded, (size_t)out_len);
	n = (size_t)out_len;
	free(encoded);
	return n;
}

static int qoi_codec_decode(void *ctx, const uint8_t *src, size_t src_len,
			     uint16_t *dst, uint32_t w, uint32_t h)
{
	qoi_desc desc;
	void *rgb888;
	size_t i, n_px = (size_t)w * h;

	(void)ctx;
	rgb888 = qoi_decode(src, (int)src_len, &desc, 3);
	if (!rgb888)
		return -1;
	if (desc.width != w || desc.height != h) {
		free(rgb888);
		return -1;
	}
	for (i = 0; i < n_px; i++) {
		uint8_t *p = (uint8_t *)rgb888 + 3 * i;

		dst[i] = rgb888_to_rgb565(p[0], p[1], p[2]);
	}
	free(rgb888);
	return 0;
}

const fbcodec_desc fbcodec_qoi = {
	.name = "qoi",
	.label = "QOI (RGB565<->RGB888 conversion, C1)",
	.category = "external",
	.is_lossy = false,
	.is_stateful = false,
	.native_rgb565 = false,
	.complexity = "medium",
	.bound = qoi_bound,
	.encode = qoi_codec_encode,
	.decode = qoi_codec_decode,
};
