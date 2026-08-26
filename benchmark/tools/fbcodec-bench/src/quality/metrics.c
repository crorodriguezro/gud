#include <math.h>

#include "metrics.h"

quality_metrics metrics_compute(const uint16_t *original,
				 const uint16_t *reconstructed, uint32_t w,
				 uint32_t h)
{
	quality_metrics m;
	size_t n = rgb565_pixel_count(w, h);
	double sum_abs = 0.0, sum_sq = 0.0, max_abs = 0.0;
	size_t i;

	for (i = 0; i < n; i++) {
		uint8_t or_, og, ob, rr, rg, rb;
		double dr, dg, db;

		rgb565_to_rgb888(original[i], &or_, &og, &ob);
		rgb565_to_rgb888(reconstructed[i], &rr, &rg, &rb);
		dr = (double)or_ - (double)rr;
		dg = (double)og - (double)rg;
		db = (double)ob - (double)rb;

		sum_abs += fabs(dr) + fabs(dg) + fabs(db);
		sum_sq += dr * dr + dg * dg + db * db;
		if (fabs(dr) > max_abs)
			max_abs = fabs(dr);
		if (fabs(dg) > max_abs)
			max_abs = fabs(dg);
		if (fabs(db) > max_abs)
			max_abs = fabs(db);
	}

	if (n == 0) {
		m.mae = 0.0;
		m.rmse = 0.0;
		m.psnr_db = HUGE_VAL;
		m.max_abs_error = 0.0;
		return m;
	}

	m.mae = sum_abs / ((double)n * 3.0);
	m.rmse = sqrt(sum_sq / ((double)n * 3.0));
	m.max_abs_error = max_abs;
	if (sum_sq == 0.0)
		m.psnr_db = HUGE_VAL;
	else
		m.psnr_db = 10.0 * log10((255.0 * 255.0) /
					  (sum_sq / ((double)n * 3.0)));
	return m;
}

residual_stats residual_stats_compute(const uint8_t *buf, size_t len)
{
	residual_stats s;
	size_t hist[256] = { 0 };
	size_t zero_bytes = 0, zero_u16 = 0;
	double sum_abs = 0.0;
	size_t i;

	for (i = 0; i < len; i++) {
		hist[buf[i]]++;
		if (buf[i] == 0)
			zero_bytes++;
		{
			int v = buf[i];
			int signed_mag = v <= 128 ? v : 256 - v;

			sum_abs += signed_mag;
		}
	}
	for (i = 0; i + 1 < len; i += 2) {
		if (buf[i] == 0 && buf[i + 1] == 0)
			zero_u16++;
	}

	s.zero_byte_percent = len ? (100.0 * (double)zero_bytes / (double)len)
				   : 0.0;
	s.zero_u16_percent =
		(len >= 2) ? (100.0 * (double)zero_u16 / (double)(len / 2))
			   : 0.0;
	s.mean_abs_byte = len ? (sum_abs / (double)len) : 0.0;

	{
		double entropy = 0.0;

		for (i = 0; i < 256; i++) {
			if (hist[i] == 0)
				continue;
			double p = (double)hist[i] / (double)len;

			entropy -= p * log2(p);
		}
		s.shannon_entropy_bits_per_byte = len ? entropy : 0.0;
	}

	return s;
}
