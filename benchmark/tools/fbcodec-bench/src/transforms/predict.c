#include <string.h>

#include "predict.h"

static inline void put_u16le(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline uint16_t get_u16le(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* ---------------------------------------------------------------------- */
/* B1: byte-wise Sub filter, two-byte pixel stride.                       */
/* ---------------------------------------------------------------------- */

void predict_sub_byte_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			   uint8_t *dst)
{
	size_t row_bytes = (size_t)w * 2U;
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint8_t *srow = (const uint8_t *)(src + (size_t)y * w);
		uint8_t *drow = dst + (size_t)y * row_bytes;
		size_t i;

		for (i = 0; i < row_bytes; i++) {
			uint8_t left = (i >= 2) ? srow[i - 2] : 0;

			drow[i] = (uint8_t)(srow[i] - left);
		}
	}
}

void predict_sub_byte_inv(const uint8_t *src, uint32_t w, uint32_t h,
			   uint16_t *dst)
{
	size_t row_bytes = (size_t)w * 2U;
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint8_t *srow = src + (size_t)y * row_bytes;
		uint8_t *drow = (uint8_t *)(dst + (size_t)y * w);
		size_t i;

		for (i = 0; i < row_bytes; i++) {
			uint8_t left = (i >= 2) ? drow[i - 2] : 0;

			drow[i] = (uint8_t)(srow[i] + left);
		}
	}
}

/* ---------------------------------------------------------------------- */
/* B2: uint16 left-subtraction.                                          */
/* ---------------------------------------------------------------------- */

void predict_u16_sub_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			  uint8_t *dst)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint16_t *srow = src + (size_t)y * w;
		uint8_t *drow = dst + (size_t)y * w * 2U;
		uint16_t prev = 0;
		uint32_t x;

		for (x = 0; x < w; x++) {
			uint16_t cur = srow[x];
			uint16_t residual = (uint16_t)(cur - (x ? prev : 0));

			put_u16le(drow + (size_t)x * 2U, residual);
			prev = cur;
		}
	}
}

void predict_u16_sub_inv(const uint8_t *src, uint32_t w, uint32_t h,
			  uint16_t *dst)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint8_t *srow = src + (size_t)y * w * 2U;
		uint16_t *drow = dst + (size_t)y * w;
		uint16_t prev = 0;
		uint32_t x;

		for (x = 0; x < w; x++) {
			uint16_t residual = get_u16le(srow + (size_t)x * 2U);
			uint16_t cur = (uint16_t)(residual + (x ? prev : 0));

			drow[x] = cur;
			prev = cur;
		}
	}
}

/* ---------------------------------------------------------------------- */
/* B3: uint16 left-XOR.                                                  */
/* ---------------------------------------------------------------------- */

void predict_u16_xor_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			  uint8_t *dst)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint16_t *srow = src + (size_t)y * w;
		uint8_t *drow = dst + (size_t)y * w * 2U;
		uint16_t prev = 0;
		uint32_t x;

		for (x = 0; x < w; x++) {
			uint16_t cur = srow[x];
			uint16_t encoded = (uint16_t)(cur ^ (x ? prev : 0));

			put_u16le(drow + (size_t)x * 2U, encoded);
			prev = cur;
		}
	}
}

void predict_u16_xor_inv(const uint8_t *src, uint32_t w, uint32_t h,
			  uint16_t *dst)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint8_t *srow = src + (size_t)y * w * 2U;
		uint16_t *drow = dst + (size_t)y * w;
		uint16_t prev = 0;
		uint32_t x;

		for (x = 0; x < w; x++) {
			uint16_t encoded = get_u16le(srow + (size_t)x * 2U);
			uint16_t cur = (uint16_t)(encoded ^ (x ? prev : 0));

			drow[x] = cur;
			prev = cur;
		}
	}
}

/* ---------------------------------------------------------------------- */
/* B4a: byte-wise PNG Paeth predictor, bpp = 2.                           */
/* ---------------------------------------------------------------------- */

static inline uint8_t paeth_predict_byte(int a, int b, int c)
{
	int p = a + b - c;
	int pa = p > a ? p - a : a - p;
	int pb = p > b ? p - b : b - p;
	int pc = p > c ? p - c : c - p;

	if (pa <= pb && pa <= pc)
		return (uint8_t)a;
	if (pb <= pc)
		return (uint8_t)b;
	return (uint8_t)c;
}

void predict_paeth_byte_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			     uint8_t *dst)
{
	size_t row_bytes = (size_t)w * 2U;
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint8_t *srow = (const uint8_t *)(src + (size_t)y * w);
		const uint8_t *prow = y ? (const uint8_t *)(src + (size_t)(y - 1) * w)
					 : NULL;
		uint8_t *drow = dst + (size_t)y * row_bytes;
		size_t i;

		for (i = 0; i < row_bytes; i++) {
			int a = (i >= 2) ? srow[i - 2] : 0;
			int b = prow ? prow[i] : 0;
			int c = (prow && i >= 2) ? prow[i - 2] : 0;
			uint8_t predicted = paeth_predict_byte(a, b, c);

			drow[i] = (uint8_t)(srow[i] - predicted);
		}
	}
}

void predict_paeth_byte_inv(const uint8_t *src, uint32_t w, uint32_t h,
			     uint16_t *dst)
{
	size_t row_bytes = (size_t)w * 2U;
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint8_t *srow = src + (size_t)y * row_bytes;
		const uint8_t *prow = y ? (const uint8_t *)(dst + (size_t)(y - 1) * w)
					 : NULL;
		uint8_t *drow = (uint8_t *)(dst + (size_t)y * w);
		size_t i;

		for (i = 0; i < row_bytes; i++) {
			int a = (i >= 2) ? drow[i - 2] : 0;
			int b = prow ? prow[i] : 0;
			int c = (prow && i >= 2) ? prow[i - 2] : 0;
			uint8_t predicted = paeth_predict_byte(a, b, c);

			drow[i] = (uint8_t)(srow[i] + predicted);
		}
	}
}

/* ---------------------------------------------------------------------- */
/* B4b: RGB565-pixel Paeth variant (scalar 16-bit prediction).            */
/* ---------------------------------------------------------------------- */

static inline uint16_t paeth_predict_u16(int a, int b, int c)
{
	int p = a + b - c;
	int pa = p > a ? p - a : a - p;
	int pb = p > b ? p - b : b - p;
	int pc = p > c ? p - c : c - p;

	if (pa <= pb && pa <= pc)
		return (uint16_t)a;
	if (pb <= pc)
		return (uint16_t)b;
	return (uint16_t)c;
}

void predict_paeth_pixel_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			      uint8_t *dst)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint16_t *srow = src + (size_t)y * w;
		const uint16_t *prow = y ? src + (size_t)(y - 1) * w : NULL;
		uint8_t *drow = dst + (size_t)y * w * 2U;
		uint32_t x;

		for (x = 0; x < w; x++) {
			int a = x ? srow[x - 1] : 0;
			int b = prow ? prow[x] : 0;
			int c = (prow && x) ? prow[x - 1] : 0;
			uint16_t predicted = paeth_predict_u16(a, b, c);
			uint16_t residual = (uint16_t)(srow[x] - predicted);

			put_u16le(drow + (size_t)x * 2U, residual);
		}
	}
}

void predict_paeth_pixel_inv(const uint8_t *src, uint32_t w, uint32_t h,
			      uint16_t *dst)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint8_t *srow = src + (size_t)y * w * 2U;
		const uint16_t *prow = y ? dst + (size_t)(y - 1) * w : NULL;
		uint16_t *drow = dst + (size_t)y * w;
		uint32_t x;

		for (x = 0; x < w; x++) {
			int a = x ? drow[x - 1] : 0;
			int b = prow ? prow[x] : 0;
			int c = (prow && x) ? prow[x - 1] : 0;
			uint16_t predicted = paeth_predict_u16(a, b, c);
			uint16_t residual = get_u16le(srow + (size_t)x * 2U);

			drow[x] = (uint16_t)(residual + predicted);
		}
	}
}

/* ---------------------------------------------------------------------- */
/* B5: MED / JPEG-LS style predictor, per channel.                       */
/* ---------------------------------------------------------------------- */

static inline int med_predict(int a, int b, int c)
{
	int lo = a < b ? a : b;
	int hi = a > b ? a : b;

	if (c >= hi)
		return lo;
	if (c <= lo)
		return hi;
	return a + b - c;
}

/* channel_bits: 5 for R/B, 6 for G. mod = 1 << channel_bits. */
static inline uint8_t med_channel_fwd(int a, int b, int c, int x,
				       int channel_bits)
{
	int mod = 1 << channel_bits;
	int predicted = med_predict(a, b, c);
	int residual = (x - predicted) & (mod - 1);

	return (uint8_t)residual;
}

static inline uint8_t med_channel_inv(int a, int b, int c, int residual,
				       int channel_bits)
{
	int mod = 1 << channel_bits;
	int predicted = med_predict(a, b, c);

	return (uint8_t)((predicted + residual) & (mod - 1));
}

void predict_med_fwd(const uint16_t *src, uint32_t w, uint32_t h,
		      uint8_t *dst)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint16_t *srow = src + (size_t)y * w;
		const uint16_t *prow = y ? src + (size_t)(y - 1) * w : NULL;
		uint16_t *drow16 = (uint16_t *)(dst + (size_t)y * w * 2U);
		uint32_t x;

		for (x = 0; x < w; x++) {
			uint8_t xr, xg, xb, ar = 0, ag = 0, ab = 0;
			uint8_t br = 0, bg = 0, bb = 0, cr = 0, cg = 0, cb = 0;
			uint8_t rr, rg, rb;

			rgb565_unpack(srow[x], &xr, &xg, &xb);
			if (x)
				rgb565_unpack(srow[x - 1], &ar, &ag, &ab);
			if (prow)
				rgb565_unpack(prow[x], &br, &bg, &bb);
			if (prow && x)
				rgb565_unpack(prow[x - 1], &cr, &cg, &cb);

			rr = med_channel_fwd(ar, br, cr, xr, 5);
			rg = med_channel_fwd(ag, bg, cg, xg, 6);
			rb = med_channel_fwd(ab, bb, cb, xb, 5);

			drow16[x] = rgb565_pack(rr, rg, rb);
		}
	}
}

void predict_med_inv(const uint8_t *src, uint32_t w, uint32_t h,
		      uint16_t *dst)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint16_t *srow16 =
			(const uint16_t *)(src + (size_t)y * w * 2U);
		const uint16_t *prow = y ? dst + (size_t)(y - 1) * w : NULL;
		uint16_t *drow = dst + (size_t)y * w;
		uint32_t x;

		for (x = 0; x < w; x++) {
			uint8_t rr, rg, rb, ar = 0, ag = 0, ab = 0;
			uint8_t br = 0, bg = 0, bb = 0, cr = 0, cg = 0, cb = 0;
			uint8_t xr, xg, xb;

			rgb565_unpack(srow16[x], &rr, &rg, &rb);
			if (x)
				rgb565_unpack(drow[x - 1], &ar, &ag, &ab);
			if (prow)
				rgb565_unpack(prow[x], &br, &bg, &bb);
			if (prow && x)
				rgb565_unpack(prow[x - 1], &cr, &cg, &cb);

			xr = med_channel_inv(ar, br, cr, rr, 5);
			xg = med_channel_inv(ag, bg, cg, rg, 6);
			xb = med_channel_inv(ab, bb, cb, rb, 5);

			drow[x] = rgb565_pack(xr, xg, xb);
		}
	}
}

/* ---------------------------------------------------------------------- */
/* Channel-aware left-subtraction / left-XOR.                            */
/* ---------------------------------------------------------------------- */

void predict_channel_sub_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			      uint8_t *dst)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint16_t *srow = src + (size_t)y * w;
		uint16_t *drow = (uint16_t *)(dst + (size_t)y * w * 2U);
		uint8_t pr = 0, pg = 0, pb = 0;
		uint32_t x;

		for (x = 0; x < w; x++) {
			uint8_t r, g, b, rr, rg, rb;

			rgb565_unpack(srow[x], &r, &g, &b);
			rr = (uint8_t)((r - (x ? pr : 0)) & 0x1F);
			rg = (uint8_t)((g - (x ? pg : 0)) & 0x3F);
			rb = (uint8_t)((b - (x ? pb : 0)) & 0x1F);
			drow[x] = rgb565_pack(rr, rg, rb);
			pr = r;
			pg = g;
			pb = b;
		}
	}
}

void predict_channel_sub_inv(const uint8_t *src, uint32_t w, uint32_t h,
			      uint16_t *dst)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint16_t *srow = (const uint16_t *)(src + (size_t)y * w * 2U);
		uint16_t *drow = dst + (size_t)y * w;
		uint8_t pr = 0, pg = 0, pb = 0;
		uint32_t x;

		for (x = 0; x < w; x++) {
			uint8_t rr, rg, rb, r, g, b;

			rgb565_unpack(srow[x], &rr, &rg, &rb);
			r = (uint8_t)((rr + (x ? pr : 0)) & 0x1F);
			g = (uint8_t)((rg + (x ? pg : 0)) & 0x3F);
			b = (uint8_t)((rb + (x ? pb : 0)) & 0x1F);
			drow[x] = rgb565_pack(r, g, b);
			pr = r;
			pg = g;
			pb = b;
		}
	}
}

void predict_channel_xor_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			      uint8_t *dst)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint16_t *srow = src + (size_t)y * w;
		uint16_t *drow = (uint16_t *)(dst + (size_t)y * w * 2U);
		uint8_t pr = 0, pg = 0, pb = 0;
		uint32_t x;

		for (x = 0; x < w; x++) {
			uint8_t r, g, b, rr, rg, rb;

			rgb565_unpack(srow[x], &r, &g, &b);
			rr = (uint8_t)(r ^ (x ? pr : 0));
			rg = (uint8_t)(g ^ (x ? pg : 0));
			rb = (uint8_t)(b ^ (x ? pb : 0));
			drow[x] = rgb565_pack(rr, rg, rb);
			pr = r;
			pg = g;
			pb = b;
		}
	}
}

void predict_channel_xor_inv(const uint8_t *src, uint32_t w, uint32_t h,
			      uint16_t *dst)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		const uint16_t *srow = (const uint16_t *)(src + (size_t)y * w * 2U);
		uint16_t *drow = dst + (size_t)y * w;
		uint8_t pr = 0, pg = 0, pb = 0;
		uint32_t x;

		for (x = 0; x < w; x++) {
			uint8_t rr, rg, rb, r, g, b;

			rgb565_unpack(srow[x], &rr, &rg, &rb);
			r = (uint8_t)(rr ^ (x ? pr : 0));
			g = (uint8_t)(rg ^ (x ? pg : 0));
			b = (uint8_t)(rb ^ (x ? pb : 0));
			drow[x] = rgb565_pack(r, g, b);
			pr = r;
			pg = g;
			pb = b;
		}
	}
}

/* ---------------------------------------------------------------------- */
/* S1: RGB565 byte shuffle (planar lo/hi byte separation).                */
/* ---------------------------------------------------------------------- */

void predict_shuffle_fwd(const uint16_t *src, uint32_t w, uint32_t h,
			  uint8_t *dst)
{
	size_t n = (size_t)w * h;
	const uint8_t *bytes = (const uint8_t *)src;
	uint8_t *lo = dst;
	uint8_t *hi = dst + n;
	size_t i;

	for (i = 0; i < n; i++) {
		lo[i] = bytes[2 * i];
		hi[i] = bytes[2 * i + 1];
	}
}

void predict_shuffle_inv(const uint8_t *src, uint32_t w, uint32_t h,
			  uint16_t *dst)
{
	size_t n = (size_t)w * h;
	const uint8_t *lo = src;
	const uint8_t *hi = src + n;
	uint8_t *bytes = (uint8_t *)dst;
	size_t i;

	for (i = 0; i < n; i++) {
		bytes[2 * i] = lo[i];
		bytes[2 * i + 1] = hi[i];
	}
}

/* ---------------------------------------------------------------------- */
/* Registry.                                                              */
/* ---------------------------------------------------------------------- */

static const rgb565_transform g_transforms[] = {
	{ "sub-byte", predict_sub_byte_fwd, predict_sub_byte_inv },
	{ "u16-sub", predict_u16_sub_fwd, predict_u16_sub_inv },
	{ "u16-xor", predict_u16_xor_fwd, predict_u16_xor_inv },
	{ "paeth-byte", predict_paeth_byte_fwd, predict_paeth_byte_inv },
	{ "paeth-pixel", predict_paeth_pixel_fwd, predict_paeth_pixel_inv },
	{ "med", predict_med_fwd, predict_med_inv },
	{ "channel-sub", predict_channel_sub_fwd, predict_channel_sub_inv },
	{ "channel-xor", predict_channel_xor_fwd, predict_channel_xor_inv },
	{ "shuffle", predict_shuffle_fwd, predict_shuffle_inv },
};

const rgb565_transform *predict_find(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof(g_transforms) / sizeof(g_transforms[0]); i++) {
		if (strcmp(g_transforms[i].name, name) == 0)
			return &g_transforms[i];
	}
	return NULL;
}

size_t predict_all(const rgb565_transform **out, size_t max)
{
	size_t n = sizeof(g_transforms) / sizeof(g_transforms[0]);
	size_t i;

	if (n > max)
		n = max;
	for (i = 0; i < n; i++)
		out[i] = &g_transforms[i];
	return n;
}
