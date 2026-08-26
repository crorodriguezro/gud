#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "synthetic.h"

static inline uint32_t xorshift32(uint32_t *state)
{
	uint32_t x = *state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static void fill_flat(rgb565_buffer *out, uint16_t color)
{
	size_t i, n = rgb565_pixel_count(out->width, out->height);

	for (i = 0; i < n; i++)
		out->pixels[i] = color;
}

static void fill_checkerboard(rgb565_buffer *out, uint32_t cell)
{
	uint32_t y;

	for (y = 0; y < out->height; y++) {
		uint32_t x;
		uint16_t *row = out->pixels + (size_t)y * out->width;

		for (x = 0; x < out->width; x++) {
			bool dark = ((x / cell) + (y / cell)) & 1U;

			row[x] = dark ? rgb565_pack(4, 8, 4)
				      : rgb565_pack(28, 56, 28);
		}
	}
}

static void fill_gradient_h(rgb565_buffer *out)
{
	uint32_t y;

	for (y = 0; y < out->height; y++) {
		uint32_t x;
		uint16_t *row = out->pixels + (size_t)y * out->width;

		for (x = 0; x < out->width; x++) {
			uint8_t v5 = (uint8_t)((x * 31U) /
						(out->width > 1
							 ? out->width - 1
							 : 1));
			uint8_t v6 = (uint8_t)((x * 63U) /
						(out->width > 1
							 ? out->width - 1
							 : 1));

			row[x] = rgb565_pack(v5, v6, v5);
		}
	}
}

static void fill_gradient_v(rgb565_buffer *out)
{
	uint32_t y;

	for (y = 0; y < out->height; y++) {
		uint32_t x;
		uint16_t *row = out->pixels + (size_t)y * out->width;
		uint8_t v5 = (uint8_t)((y * 31U) /
					(out->height > 1 ? out->height - 1
							  : 1));
		uint8_t v6 = (uint8_t)((y * 63U) /
					(out->height > 1 ? out->height - 1
							  : 1));

		for (x = 0; x < out->width; x++)
			row[x] = rgb565_pack(v5, v6, v5);
	}
}

static void fill_gradient_2d(rgb565_buffer *out)
{
	uint32_t y;

	for (y = 0; y < out->height; y++) {
		uint32_t x;
		uint16_t *row = out->pixels + (size_t)y * out->width;
		uint8_t g6 = (uint8_t)((y * 63U) /
					(out->height > 1 ? out->height - 1
							  : 1));

		for (x = 0; x < out->width; x++) {
			uint8_t r5 = (uint8_t)((x * 31U) /
						(out->width > 1
							 ? out->width - 1
							 : 1));
			uint8_t b5 = (uint8_t)(31U - r5);

			row[x] = rgb565_pack(r5, g6, b5);
		}
	}
}

static void fill_text_like(rgb565_buffer *out)
{
	uint32_t y;

	fill_flat(out, rgb565_pack(2, 4, 2));
	for (y = 0; y < out->height; y++) {
		uint32_t x;
		uint16_t *row = out->pixels + (size_t)y * out->width;
		bool text_row = (y % 16) < 10;

		if (!text_row)
			continue;
		for (x = 0; x < out->width; x++) {
			/* High-contrast thin vertical strokes, like glyph
			 * stems, with gaps -- a cheap but representative
			 * stand-in for rendered text.
			 */
			bool stroke = ((x % 8) < 2) && ((x / 8) % 5 != 0);

			if (stroke)
				row[x] = rgb565_pack(31, 63, 31);
		}
	}
}

static void fill_noise(rgb565_buffer *out, uint32_t seed)
{
	size_t i, n = rgb565_pixel_count(out->width, out->height);
	uint32_t state = seed ? seed : 0x9e3779b9U;

	for (i = 0; i < n; i++)
		out->pixels[i] = (uint16_t)xorshift32(&state);
}

static void fill_photo_like(rgb565_buffer *out, uint32_t seed)
{
	/* Smoothly varying colored blobs: sum of a handful of 2D sinusoids
	 * with pseudo-random frequency/phase, quantized to RGB565. This is
	 * not a real photograph but exercises smooth gradients + soft edges
	 * the way natural images do, which is the property that matters for
	 * predictor/entropy-coder comparisons.
	 */
	uint32_t state = seed ? seed : 12345U;
	double fr[3], fg[3], fb[3], pr[3], pg[3], pb[3];
	int k;
	uint32_t y;

	for (k = 0; k < 3; k++) {
		fr[k] = 1.0 + (double)(xorshift32(&state) % 500) / 100.0;
		fg[k] = 1.0 + (double)(xorshift32(&state) % 500) / 100.0;
		fb[k] = 1.0 + (double)(xorshift32(&state) % 500) / 100.0;
		pr[k] = (double)(xorshift32(&state) % 628) / 100.0;
		pg[k] = (double)(xorshift32(&state) % 628) / 100.0;
		pb[k] = (double)(xorshift32(&state) % 628) / 100.0;
	}

	for (y = 0; y < out->height; y++) {
		double ny = (double)y / (double)(out->height > 1
							  ? out->height - 1
							  : 1);
		uint32_t x;
		uint16_t *row = out->pixels + (size_t)y * out->width;

		for (x = 0; x < out->width; x++) {
			double nx = (double)x /
				    (double)(out->width > 1 ? out->width - 1
							     : 1);
			double r = 0, g = 0, b = 0;

			for (k = 0; k < 3; k++) {
				r += sin(nx * fr[k] * 6.28318 + pr[k]) +
				     cos(ny * fr[k] * 3.14159 + pr[k]);
				g += sin(nx * fg[k] * 5.0 + pg[k]) +
				     cos(ny * fg[k] * 4.0 + pg[k]);
				b += sin(nx * fb[k] * 4.0 + pb[k]) +
				     cos(ny * fb[k] * 5.0 + pb[k]);
			}
			r = (r + 6.0) / 12.0;
			g = (g + 6.0) / 12.0;
			b = (b + 6.0) / 12.0;
			if (r < 0)
				r = 0;
			if (r > 1)
				r = 1;
			if (g < 0)
				g = 0;
			if (g > 1)
				g = 1;
			if (b < 0)
				b = 0;
			if (b > 1)
				b = 1;

			row[x] = rgb565_pack((uint8_t)(r * 31.0),
					      (uint8_t)(g * 63.0),
					      (uint8_t)(b * 31.0));
		}
	}
}

static const char *const g_pattern_names[] = {
	"flat-black",  "flat-white",     "flat-gray",     "flat-ui",
	"checker-8",   "checker-32",     "checker-64",    "gradient-h",
	"gradient-v",  "gradient-2d",    "text-like",     "noise",
	"photo-like",
};

size_t synth_pattern_names(const char **out, size_t max)
{
	size_t n = sizeof(g_pattern_names) / sizeof(g_pattern_names[0]);
	size_t i;

	if (n > max)
		n = max;
	for (i = 0; i < n; i++)
		out[i] = g_pattern_names[i];
	return n;
}

bool synth_generate(const char *pattern, uint32_t w, uint32_t h,
		     uint32_t seed, rgb565_buffer *out)
{
	if (!rgb565_buffer_alloc(out, w, h))
		return false;

	if (strcmp(pattern, "flat-black") == 0) {
		fill_flat(out, rgb565_pack(0, 0, 0));
	} else if (strcmp(pattern, "flat-white") == 0) {
		fill_flat(out, rgb565_pack(31, 63, 31));
	} else if (strcmp(pattern, "flat-gray") == 0) {
		fill_flat(out, rgb565_pack(16, 32, 16));
	} else if (strcmp(pattern, "flat-ui") == 0) {
		fill_flat(out, rgb565_pack(6, 12, 20)); /* dark UI chrome */
	} else if (strcmp(pattern, "checker-8") == 0) {
		fill_checkerboard(out, 8);
	} else if (strcmp(pattern, "checker-32") == 0) {
		fill_checkerboard(out, 32);
	} else if (strcmp(pattern, "checker-64") == 0) {
		fill_checkerboard(out, 64);
	} else if (strcmp(pattern, "gradient-h") == 0) {
		fill_gradient_h(out);
	} else if (strcmp(pattern, "gradient-v") == 0) {
		fill_gradient_v(out);
	} else if (strcmp(pattern, "gradient-2d") == 0) {
		fill_gradient_2d(out);
	} else if (strcmp(pattern, "text-like") == 0) {
		fill_text_like(out);
	} else if (strcmp(pattern, "noise") == 0) {
		fill_noise(out, seed);
	} else if (strcmp(pattern, "photo-like") == 0) {
		fill_photo_like(out, seed);
	} else {
		rgb565_buffer_free(out);
		return false;
	}
	return true;
}

static const char *const g_sequence_names[] = {
	"static", "scroll-h", "scroll-v", "small-changes", "fullscreen-anim",
};

size_t synth_sequence_names(const char **out, size_t max)
{
	size_t n = sizeof(g_sequence_names) / sizeof(g_sequence_names[0]);
	size_t i;

	if (n > max)
		n = max;
	for (i = 0; i < n; i++)
		out[i] = g_sequence_names[i];
	return n;
}

bool synth_generate_sequence(const char *pattern, uint32_t w, uint32_t h,
			      uint32_t seed, uint32_t frame_count,
			      rgb565_buffer *out_frames)
{
	rgb565_buffer base;
	uint32_t f = 0;

	if (!synth_generate("photo-like", w, h, seed, &base))
		return false;

	if (strcmp(pattern, "static") == 0) {
		for (f = 0; f < frame_count; f++) {
			if (!rgb565_buffer_alloc(&out_frames[f], w, h))
				goto fail;
			memcpy(out_frames[f].pixels, base.pixels,
			       rgb565_byte_size(w, h));
		}
	} else if (strcmp(pattern, "scroll-h") == 0 ||
		   strcmp(pattern, "scroll-v") == 0) {
		bool horiz = strcmp(pattern, "scroll-h") == 0;

		for (f = 0; f < frame_count; f++) {
			uint32_t y;

			if (!rgb565_buffer_alloc(&out_frames[f], w, h))
				goto fail;
			for (y = 0; y < h; y++) {
				uint32_t x;
				uint16_t *drow =
					out_frames[f].pixels + (size_t)y * w;

				for (x = 0; x < w; x++) {
					uint32_t sx = x, sy = y;

					if (horiz)
						sx = (x + f) % w;
					else
						sy = (y + f) % h;
					drow[x] = base.pixels[
						(size_t)sy * w + sx];
				}
			}
		}
	} else if (strcmp(pattern, "small-changes") == 0) {
		uint32_t state = seed ? seed : 777;

		for (f = 0; f < frame_count; f++) {
			uint32_t rw = 32, rh = 32;
			uint32_t rx, ry, yy;

			if (!rgb565_buffer_alloc(&out_frames[f], w, h))
				goto fail;
			memcpy(out_frames[f].pixels, base.pixels,
			       rgb565_byte_size(w, h));
			rx = (w > rw) ? (xorshift32(&state) % (w - rw)) : 0;
			ry = (h > rh) ? (xorshift32(&state) % (h - rh)) : 0;
			for (yy = ry; yy < ry + rh && yy < h; yy++) {
				uint32_t xx;
				uint16_t *drow =
					out_frames[f].pixels +
					(size_t)yy * w;
				uint16_t color = (uint16_t)(0xFFFF ^ (f * 131));

				for (xx = rx; xx < rx + rw && xx < w; xx++)
					drow[xx] = color;
			}
		}
	} else if (strcmp(pattern, "fullscreen-anim") == 0) {
		for (f = 0; f < frame_count; f++) {
			if (!synth_generate("photo-like", w, h,
					      seed + f * 7919U + 1U,
					      &out_frames[f]))
				goto fail;
		}
	} else {
		goto fail;
	}

	rgb565_buffer_free(&base);
	return true;

fail:
	rgb565_buffer_free(&base);
	{
		uint32_t k;

		for (k = 0; k < f && k < frame_count; k++)
			rgb565_buffer_free(&out_frames[k]);
	}
	return false;
}
