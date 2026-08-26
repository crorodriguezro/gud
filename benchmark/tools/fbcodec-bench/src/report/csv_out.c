#include <stdio.h>
#include <sys/stat.h>

#include "csv_out.h"

static const char *const CSV_HEADER =
	"codec,label,category,complexity,is_lossy,is_stateful,native_rgb565,"
	"corpus,rect_class,width,height,iterations,input_bytes,"
	"mean_encoded_bytes,min_encoded_bytes,max_encoded_bytes,"
	"compression_ratio,bytes_per_pixel,"
	"encode_ns_mean,encode_ns_median,encode_ns_p90,encode_ns_p95,"
	"encode_ns_p99,encode_ns_min,encode_ns_max,"
	"decode_ns_mean,decode_ns_median,decode_ns_p90,decode_ns_p95,"
	"decode_ns_p99,decode_ns_min,decode_ns_max,"
	"encode_MBps,decode_MBps,encode_MiBps,decode_MiBps,"
	"roundtrip_verified,roundtrip_exact,"
	"mae,rmse,psnr_db,max_abs_error,"
	"usb_mib_s_ref,fps_usb_only_ref,fps_encode_usb_ref,"
	"fps_total_serial_ref\n";

bool csv_append_results(const char *path, const bench_result *results,
			 size_t count)
{
	struct stat st;
	bool need_header = (stat(path, &st) != 0 || st.st_size == 0);
	FILE *f = fopen(path, "a");
	size_t i;

	if (!f)
		return false;
	if (need_header)
		fputs(CSV_HEADER, f);

	for (i = 0; i < count; i++) {
		const bench_result *r = &results[i];
		const usb_model_point *m0 =
			r->usb_model_count ? &r->usb_models[0] : NULL;

		fprintf(f,
			"%s,%s,%s,%s,%d,%d,%d,%s,%s,%u,%u,%zu,%zu,"
			"%.2f,%.2f,%.2f,%.4f,%.4f,"
			"%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,"
			"%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,"
			"%.3f,%.3f,%.3f,%.3f,"
			"%d,%d,"
			"%.4f,%.4f,%.4f,%.4f,"
			"%.1f,%.3f,%.3f,%.3f\n",
			r->codec, r->label, r->category, r->complexity,
			r->is_lossy, r->is_stateful, r->native_rgb565,
			r->corpus, r->rect_class, r->width, r->height,
			r->iterations, r->input_bytes, r->mean_encoded_bytes,
			r->min_encoded_bytes, r->max_encoded_bytes,
			r->compression_ratio, r->bytes_per_pixel,
			r->encode_ns.mean, r->encode_ns.median,
			r->encode_ns.p90, r->encode_ns.p95, r->encode_ns.p99,
			r->encode_ns.min, r->encode_ns.max,
			r->decode_ns.mean, r->decode_ns.median,
			r->decode_ns.p90, r->decode_ns.p95, r->decode_ns.p99,
			r->decode_ns.min, r->decode_ns.max,
			r->encode_mb_per_s, r->decode_mb_per_s,
			r->encode_mib_per_s, r->decode_mib_per_s,
			r->roundtrip_verified, r->roundtrip_exact,
			r->has_quality ? r->quality.mae : 0.0,
			r->has_quality ? r->quality.rmse : 0.0,
			r->has_quality &&
					!(r->quality.psnr_db > 1e18)
				? r->quality.psnr_db
				: 99.0,
			r->has_quality ? r->quality.max_abs_error : 0.0,
			m0 ? m0->usb_mib_s : 0.0,
			m0 ? m0->fps_usb_only : 0.0,
			m0 ? m0->fps_encode_usb : 0.0,
			m0 ? m0->fps_total_serial : 0.0);
	}
	fclose(f);
	return true;
}
