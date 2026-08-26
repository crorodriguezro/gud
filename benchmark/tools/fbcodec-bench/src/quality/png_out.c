#include <math.h>
#include <stdlib.h>

#include "png_out.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../vendor/stb/stb_image_write.h"

int png_write_rgb565(const char *path, const uint16_t *pixels, uint32_t w,
		      uint32_t h)
{
	uint8_t *rgb888 = malloc((size_t)w * h * 3);
	int ok;
	size_t i, n = (size_t)w * h;

	if (!rgb888)
		return -1;
	for (i = 0; i < n; i++)
		rgb565_to_rgb888(pixels[i], &rgb888[3 * i], &rgb888[3 * i + 1],
				  &rgb888[3 * i + 2]);
	ok = stbi_write_png(path, (int)w, (int)h, 3, rgb888, (int)w * 3);
	free(rgb888);
	return ok ? 0 : -1;
}

int png_write_diff(const char *path, const uint16_t *a, const uint16_t *b,
		    uint32_t w, uint32_t h, double amplify)
{
	uint8_t *out = malloc((size_t)w * h * 3);
	int ok;
	size_t i, n = (size_t)w * h;

	if (!out)
		return -1;
	for (i = 0; i < n; i++) {
		uint8_t ar, ag, ab, br, bg, bb;
		double dr, dg, db;

		rgb565_to_rgb888(a[i], &ar, &ag, &ab);
		rgb565_to_rgb888(b[i], &br, &bg, &bb);
		dr = fabs((double)ar - (double)br) * amplify;
		dg = fabs((double)ag - (double)bg) * amplify;
		db = fabs((double)ab - (double)bb) * amplify;
		if (dr > 255.0)
			dr = 255.0;
		if (dg > 255.0)
			dg = 255.0;
		if (db > 255.0)
			db = 255.0;
		out[3 * i] = (uint8_t)dr;
		out[3 * i + 1] = (uint8_t)dg;
		out[3 * i + 2] = (uint8_t)db;
	}
	ok = stbi_write_png(path, (int)w, (int)h, 3, out, (int)w * 3);
	free(out);
	return ok ? 0 : -1;
}
