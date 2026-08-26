#include <string.h>

#include "quant.h"

const quant_mode_info quant_mild_info = {
	"mild-r4g5b4", 4, 5, 4, 16u * 32u * 16u, 16.0, 16.0
};
const quant_mode_info quant_moderate_info = {
	"moderate-r4g4b4-16bit", 4, 4, 4, 16u * 16u * 16u, 16.0, 16.0
};
const quant_mode_info quant_moderate_packed_info = {
	"moderate-r4g4b4-packed12", 4, 4, 4, 16u * 16u * 16u, 12.0, 12.0
};
const quant_mode_info quant_aggressive_info = {
	"aggressive-r3g3b2-packed8", 3, 3, 2, 8u * 8u * 4u, 8.0, 8.0
};

/* Q1: mask to keep the top N bits of a field that is `field_bits` wide. */
static inline uint8_t keep_top_bits(uint8_t value, int field_bits,
				     int keep_bits)
{
	int drop = field_bits - keep_bits;
	uint8_t mask;

	if (drop <= 0)
		return value;
	mask = (uint8_t)(((1 << keep_bits) - 1) << drop);
	return (uint8_t)(value & mask);
}

void quant_mild_fwd(const uint16_t *src, uint32_t w, uint32_t h,
		     uint8_t *dst)
{
	size_t n = (size_t)w * h;
	uint16_t *out = (uint16_t *)dst;
	size_t i;

	for (i = 0; i < n; i++) {
		uint8_t r, g, b;

		rgb565_unpack(src[i], &r, &g, &b);
		r = keep_top_bits(r, 5, 4);
		g = keep_top_bits(g, 6, 5);
		b = keep_top_bits(b, 5, 4);
		out[i] = rgb565_pack(r, g, b);
	}
}

void quant_moderate_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			 uint8_t *dst)
{
	size_t n = (size_t)w * h;
	uint16_t *out = (uint16_t *)dst;
	size_t i;

	for (i = 0; i < n; i++) {
		uint8_t r, g, b;

		rgb565_unpack(src[i], &r, &g, &b);
		r = keep_top_bits(r, 5, 4);
		g = keep_top_bits(g, 6, 4);
		b = keep_top_bits(b, 5, 4);
		out[i] = rgb565_pack(r, g, b);
	}
}

void quant_identity_inv(const uint8_t *src, uint32_t w, uint32_t h,
			 uint16_t *dst)
{
	memcpy(dst, src, (size_t)w * h * sizeof(uint16_t));
}

/* ---------------------------------------------------------------------- */
/* Q2 packed: two R4G4B4 pixels -> 3 bytes.                               */
/* ---------------------------------------------------------------------- */

size_t quant_rgb444_packed_bound(uint32_t w, uint32_t h)
{
	size_t n = (size_t)w * h;

	return ((n + 1) / 2) * 3;
}

static inline uint16_t rgb444_of(uint16_t pixel)
{
	uint8_t r, g, b;

	rgb565_unpack(pixel, &r, &g, &b);
	r = (uint8_t)(keep_top_bits(r, 5, 4) >> 1); /* 4-bit value, 0..15 */
	g = (uint8_t)(keep_top_bits(g, 6, 4) >> 2); /* 4-bit value, 0..15 */
	b = (uint8_t)(keep_top_bits(b, 5, 4) >> 1); /* 4-bit value, 0..15 */
	return (uint16_t)((r << 8) | (g << 4) | b);
}

static inline uint16_t rgb444_to_rgb565(uint16_t v)
{
	uint8_t r4 = (uint8_t)((v >> 8) & 0xF);
	uint8_t g4 = (uint8_t)((v >> 4) & 0xF);
	uint8_t b4 = (uint8_t)(v & 0xF);
	uint8_t r5 = (uint8_t)((r4 << 1) | (r4 >> 3));
	uint8_t g6 = (uint8_t)((g4 << 2) | (g4 >> 2));
	uint8_t b5 = (uint8_t)((b4 << 1) | (b4 >> 3));

	return rgb565_pack(r5, g6, b5);
}

size_t quant_rgb444_pack(const uint16_t *src, uint32_t w, uint32_t h,
			   uint8_t *dst)
{
	size_t n = (size_t)w * h;
	size_t pairs = n / 2;
	size_t i;
	size_t out_off = 0;

	for (i = 0; i < pairs; i++) {
		uint16_t p0 = rgb444_of(src[2 * i]);
		uint16_t p1 = rgb444_of(src[2 * i + 1]);

		dst[out_off + 0] = (uint8_t)(p0 >> 4);
		dst[out_off + 1] =
			(uint8_t)(((p0 & 0xF) << 4) | ((p1 >> 8) & 0xF));
		dst[out_off + 2] = (uint8_t)(p1 & 0xFF);
		out_off += 3;
	}
	if (n & 1) {
		uint16_t p0 = rgb444_of(src[n - 1]);

		dst[out_off + 0] = (uint8_t)(p0 >> 4);
		dst[out_off + 1] = (uint8_t)((p0 & 0xF) << 4);
		dst[out_off + 2] = 0;
		out_off += 3;
	}
	return out_off;
}

void quant_rgb444_unpack(const uint8_t *src, uint32_t w, uint32_t h,
			   uint16_t *dst)
{
	size_t n = (size_t)w * h;
	size_t pairs = n / 2;
	size_t i;
	size_t in_off = 0;

	for (i = 0; i < pairs; i++) {
		uint16_t p0 = (uint16_t)((src[in_off + 0] << 4) |
					  (src[in_off + 1] >> 4));
		uint16_t p1 = (uint16_t)(((src[in_off + 1] & 0xF) << 8) |
					  src[in_off + 2]);

		dst[2 * i] = rgb444_to_rgb565(p0);
		dst[2 * i + 1] = rgb444_to_rgb565(p1);
		in_off += 3;
	}
	if (n & 1) {
		uint16_t p0 = (uint16_t)((src[in_off + 0] << 4) |
					  (src[in_off + 1] >> 4));

		dst[n - 1] = rgb444_to_rgb565(p0);
	}
}

/* ---------------------------------------------------------------------- */
/* Q3: RGB332, 1 byte/pixel.                                              */
/* ---------------------------------------------------------------------- */

size_t quant_rgb332_bound(uint32_t w, uint32_t h)
{
	return (size_t)w * h;
}

size_t quant_rgb332_pack(const uint16_t *src, uint32_t w, uint32_t h,
			   uint8_t *dst)
{
	size_t n = (size_t)w * h;
	size_t i;

	for (i = 0; i < n; i++) {
		uint8_t r, g, b, r3, g3, b2;

		rgb565_unpack(src[i], &r, &g, &b);
		r3 = (uint8_t)(r >> 2); /* top 3 of 5 bits */
		g3 = (uint8_t)(g >> 3); /* top 3 of 6 bits */
		b2 = (uint8_t)(b >> 3); /* top 2 of 5 bits */
		dst[i] = (uint8_t)((r3 << 5) | (g3 << 2) | b2);
	}
	return n;
}

void quant_rgb332_unpack(const uint8_t *src, uint32_t w, uint32_t h,
			   uint16_t *dst)
{
	size_t n = (size_t)w * h;
	size_t i;

	for (i = 0; i < n; i++) {
		uint8_t v = src[i];
		uint8_t r3 = (uint8_t)((v >> 5) & 0x7);
		uint8_t g3 = (uint8_t)((v >> 2) & 0x7);
		uint8_t b2 = (uint8_t)(v & 0x3);
		/* Expand back to 5/6/5 bit fields by left-shift + MSB replicate. */
		uint8_t r5 = (uint8_t)((r3 << 2) | (r3 >> 1));
		uint8_t g6 = (uint8_t)((g3 << 3) | g3);
		uint8_t b5 = (uint8_t)((b2 << 3) | (b2 << 1) | (b2 >> 1));

		dst[i] = rgb565_pack(r5, g6, b5);
	}
}
