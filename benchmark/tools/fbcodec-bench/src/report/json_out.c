#include <math.h>
#include <stdio.h>

#include "json_out.h"

static double json_safe(double v)
{
	if (isinf(v) || isnan(v))
		return 99999.0; /* sentinel for "effectively infinite" PSNR */
	return v;
}

static void json_write_percentiles(FILE *f, const char *name,
				    const percentile_stats *s)
{
	fprintf(f,
		"\"%s\":{\"mean\":%.1f,\"median\":%.1f,\"p90\":%.1f,"
		"\"p95\":%.1f,\"p99\":%.1f,\"min\":%.1f,\"max\":%.1f}",
		name, json_safe(s->mean), json_safe(s->median),
		json_safe(s->p90), json_safe(s->p95), json_safe(s->p99),
		json_safe(s->min), json_safe(s->max));
}

bool json_write_results(const char *path, const bench_result *results,
			 size_t count, const char *run_metadata_json)
{
	FILE *f = fopen(path, "w");
	size_t i;

	if (!f)
		return false;

	fprintf(f, "{\n\"metadata\":%s,\n\"results\":[\n",
		run_metadata_json ? run_metadata_json : "{}");

	for (i = 0; i < count; i++) {
		const bench_result *r = &results[i];

		fprintf(f, "{");
		fprintf(f,
			"\"codec\":\"%s\",\"label\":\"%s\",\"category\":\"%s\","
			"\"complexity\":\"%s\",\"is_lossy\":%s,"
			"\"is_stateful\":%s,\"native_rgb565\":%s,",
			r->codec, r->label, r->category, r->complexity,
			r->is_lossy ? "true" : "false",
			r->is_stateful ? "true" : "false",
			r->native_rgb565 ? "true" : "false");
		fprintf(f,
			"\"corpus\":\"%s\",\"rect_class\":\"%s\","
			"\"width\":%u,\"height\":%u,\"iterations\":%zu,",
			r->corpus, r->rect_class, r->width, r->height,
			r->iterations);
		fprintf(f,
			"\"input_bytes\":%zu,\"mean_encoded_bytes\":%.2f,"
			"\"min_encoded_bytes\":%.2f,\"max_encoded_bytes\":%.2f,"
			"\"compression_ratio\":%.4f,\"bytes_per_pixel\":%.4f,",
			r->input_bytes, json_safe(r->mean_encoded_bytes),
			json_safe(r->min_encoded_bytes),
			json_safe(r->max_encoded_bytes),
			json_safe(r->compression_ratio),
			json_safe(r->bytes_per_pixel));

		json_write_percentiles(f, "encode_ns", &r->encode_ns);
		fprintf(f, ",");
		json_write_percentiles(f, "decode_ns", &r->decode_ns);
		fprintf(f, ",");

		fprintf(f,
			"\"encode_MBps\":%.3f,\"decode_MBps\":%.3f,"
			"\"encode_MiBps\":%.3f,\"decode_MiBps\":%.3f,",
			json_safe(r->encode_mb_per_s),
			json_safe(r->decode_mb_per_s),
			json_safe(r->encode_mib_per_s),
			json_safe(r->decode_mib_per_s));

		fprintf(f,
			"\"roundtrip_verified\":%s,\"roundtrip_exact\":%s,",
			r->roundtrip_verified ? "true" : "false",
			r->roundtrip_exact ? "true" : "false");

		if (r->has_quality) {
			fprintf(f,
				"\"quality\":{\"mae\":%.4f,\"rmse\":%.4f,"
				"\"psnr_db\":%.4f,\"max_abs_error\":%.4f}",
				json_safe(r->quality.mae),
				json_safe(r->quality.rmse),
				json_safe(r->quality.psnr_db),
				json_safe(r->quality.max_abs_error));
		} else {
			fprintf(f, "\"quality\":null");
		}

		fprintf(f, ",\"usb_models\":[");
		{
			size_t u;

			for (u = 0; u < r->usb_model_count; u++) {
				const usb_model_point *m = &r->usb_models[u];

				fprintf(f,
					"{\"usb_mib_s\":%.1f,"
					"\"usb_transfer_ns\":%.1f,"
					"\"fps_usb_only\":%.3f,"
					"\"fps_encode_usb\":%.3f,"
					"\"fps_total_serial\":%.3f}%s",
					json_safe(m->usb_mib_s),
					json_safe(m->usb_transfer_ns),
					json_safe(m->fps_usb_only),
					json_safe(m->fps_encode_usb),
					json_safe(m->fps_total_serial),
					(u + 1 < r->usb_model_count) ? "," : "");
			}
		}
		fprintf(f, "]");

		fprintf(f, "}%s\n", (i + 1 < count) ? "," : "");
	}

	fprintf(f, "]\n}\n");
	fclose(f);
	return true;
}
