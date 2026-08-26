/*
 * metrics.h - visual quality and residual statistics helpers (PROJECT SPEC
 * sections 23, 42, 43).
 *
 * Quality metrics (MAE/RMSE/PSNR) are computed on the RGB888-expanded
 * representation of both images so that different RGB565 quantization
 * levels remain numerically comparable on a common 0..255 scale.
 */
#ifndef FBCODEC_METRICS_H
#define FBCODEC_METRICS_H

#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	double mae;          /* mean absolute error, 0..255 scale */
	double rmse;          /* root mean squared error, 0..255 scale */
	double psnr_db;        /* dB; HUGE_VAL if images are identical */
	double max_abs_error;  /* worst single-channel error, 0..255 scale */
} quality_metrics;

quality_metrics metrics_compute(const uint16_t *original,
				 const uint16_t *reconstructed, uint32_t w,
				 uint32_t h);

/* Residual/byte-histogram statistics used to explain *why* one predictor
 * beats another (section 43). Operates directly on a transformed byte
 * buffer of length `len` (as produced by a predict_fwd_fn).
 */
typedef struct {
	double zero_byte_percent;
	double zero_u16_percent; /* pairs of bytes (little-endian residual)
				   * that are exactly zero */
	double mean_abs_byte;    /* mean of bytes reinterpreted as signed
				   * offsets from zero, i.e. min(v,256-v) */
	double shannon_entropy_bits_per_byte;
} residual_stats;

residual_stats residual_stats_compute(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_METRICS_H */
