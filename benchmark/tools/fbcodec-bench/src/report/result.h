/*
 * result.h - one aggregated benchmark result record: everything required by
 * results.csv / results.json (PROJECT SPEC section 18-19, 36-37).
 */
#ifndef FBCODEC_RESULT_H
#define FBCODEC_RESULT_H

#include "../common.h"
#include "../quality/metrics.h"
#include "stats.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RESULT_TAG_LEN 128
#define RESULT_NAME_LEN 64
#define RESULT_MAX_USB_POINTS 8

typedef struct {
	double usb_mib_s;
	double usb_transfer_ns;   /* mean_encoded_bytes / throughput */
	double fps_usb_only;
	double fps_encode_usb;
	double fps_total_serial;
} usb_model_point;


typedef struct {
	char codec[RESULT_NAME_LEN];
	char label[RESULT_NAME_LEN * 2];
	char category[RESULT_NAME_LEN];
	char complexity[16];
	bool is_lossy;
	bool is_stateful;
	bool native_rgb565;

	char corpus[RESULT_TAG_LEN];      /* e.g. "gradient-h", "real-motion" */
	char rect_class[RESULT_TAG_LEN]; /* e.g. "64x64", "full" */
	uint32_t width;
	uint32_t height;

	size_t iterations;
	size_t input_bytes;         /* per-item input size (bytes) */
	double mean_encoded_bytes;
	double min_encoded_bytes;
	double max_encoded_bytes;
	double compression_ratio;   /* input_bytes / mean_encoded_bytes */
	double bytes_per_pixel;

	percentile_stats encode_ns;
	percentile_stats decode_ns;
	double encode_mb_per_s;  /* MB/s = 1e6 bytes/s */
	double decode_mb_per_s;
	double encode_mib_per_s; /* MiB/s = 2^20 bytes/s */
	double decode_mib_per_s;

	bool roundtrip_verified;
	bool roundtrip_exact; /* only meaningful if roundtrip_verified */

	bool has_quality;
	quality_metrics quality;

	/* PROJECT SPEC next-phase Phase 3.D / Phase 7: RGB565<->RGB888
	 * conversion cost reported separately from the QOIR library's own
	 * encode/decode cost ("Do not hide RGB565 conversion inside QOIR
	 * timing"). Only populated for the qoir-* codecs; zeroed/false
	 * otherwise (encode_ns/decode_ns above remain the authoritative
	 * *total* codec-path time for every codec, unchanged).
	 */
	bool has_conversion_breakdown;
	percentile_stats encode_conversion_ns;
	percentile_stats encode_codec_ns;
	percentile_stats decode_conversion_ns;
	percentile_stats decode_codec_ns;

	usb_model_point usb_models[RESULT_MAX_USB_POINTS];
	size_t usb_model_count;
} bench_result;

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_RESULT_H */
