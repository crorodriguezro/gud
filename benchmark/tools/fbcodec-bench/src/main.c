/*
 * main.c - fbcodec-bench CLI: benchmarks every registered RGB565
 * compression candidate against synthetic and/or real-asset corpora,
 * verifies round trips, computes quality metrics for lossy modes, and
 * emits machine-readable results (CSV/JSON) plus optional quality PNGs.
 *
 * See README.md in this directory for full usage and PROJECT SPEC section
 * 34 for the required interface surface.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "codec.h"
#include "codecs/qoir_codec.h"
#include "common.h"
#include "corpus/capture.h"
#include "corpus/synthetic.h"
#include "quality/metrics.h"
#include "quality/png_out.h"
#include "report/csv_out.h"
#include "report/json_out.h"
#include "report/result.h"
#include "report/stats.h"
#include "transforms/predict.h"
#include "transforms/quant.h"
#include "transforms/temporal_policy.h"

/* ---------------------------------------------------------------------- */
/* CLI option parsing.                                                    */
/* ---------------------------------------------------------------------- */

typedef struct {
	const char *mode; /* frame | sequence | damage-stream */
	const char *codec;
	const char *synthetic;
	const char *synthetic_sequence;
	const char *input;
	uint32_t width;
	uint32_t height;
	uint32_t frames;
	const char *rect; /* "WxH" or "full" */
	size_t iterations;
	size_t warmup;
	char usb_list[256];
	const char *quality_dir;
	const char *csv_path;
	const char *json_path;
	const char *corpus_tag;
	uint32_t seed;
	uint32_t tile;
	uint64_t frame_interval_ns;
	bool list_only;
	bool verify; /* --verify: fail (nonzero exit) on any lossless
		      * round-trip mismatch. Round-trip verification and
		      * quality-metric computation always run regardless of
		      * this flag (PROJECT SPEC section 24: "fail the
		      * benchmark if any lossless candidate does not
		      * round-trip exactly"); --verify only makes that
		      * failure fatal to the process exit code instead of
		      * being reported solely via roundtrip_exact in the
		      * CSV/JSON output.
		      */
	const char *predictor; /* --mode residual */
	uint32_t keyframe_interval; /* --mode temporal-policy */
	bool adaptive;		     /* --mode temporal-policy */
} cli_options;

static void cli_defaults(cli_options *o)
{
	memset(o, 0, sizeof(*o));
	o->mode = "frame";
	o->codec = "all";
	o->width = 1280;
	o->height = 720;
	o->frames = 30;
	o->rect = "full";
	o->iterations = 20;
	o->warmup = 5;
	strncpy(o->usb_list, "30,35,40,45", sizeof(o->usb_list) - 1);
	o->seed = 1;
	o->tile = 32;
	o->frame_interval_ns = 33333333ULL; /* ~30 fps */
}

static bool arg_is(const char *a, const char *name)
{
	return strcmp(a, name) == 0;
}

static const char *safe_str(const char *v)
{
	return v ? v : "";
}

static bool parse_args(int argc, char **argv, cli_options *o)
{
	int i;

	cli_defaults(o);
	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

#define NEXT() (v ? (i++, v) : NULL)
		if (arg_is(a, "--mode")) {
			o->mode = NEXT();
		} else if (arg_is(a, "--codec")) {
			o->codec = NEXT();
		} else if (arg_is(a, "--synthetic")) {
			o->synthetic = NEXT();
		} else if (arg_is(a, "--synthetic-sequence")) {
			o->synthetic_sequence = NEXT();
		} else if (arg_is(a, "--input")) {
			o->input = NEXT();
		} else if (arg_is(a, "--width")) {
			o->width = (uint32_t)atoi(safe_str(NEXT()));
		} else if (arg_is(a, "--height")) {
			o->height = (uint32_t)atoi(safe_str(NEXT()));
		} else if (arg_is(a, "--frames")) {
			o->frames = (uint32_t)atoi(safe_str(NEXT()));
		} else if (arg_is(a, "--rect")) {
			o->rect = NEXT();
		} else if (arg_is(a, "--iterations")) {
			o->iterations = (size_t)atoi(safe_str(NEXT()));
		} else if (arg_is(a, "--warmup")) {
			o->warmup = (size_t)atoi(safe_str(NEXT()));
		} else if (arg_is(a, "--usb-mib-s")) {
			const char *val = NEXT();

			if (val)
				strncpy(o->usb_list, val,
					sizeof(o->usb_list) - 1);
		} else if (arg_is(a, "--quality-dir")) {
			o->quality_dir = NEXT();
		} else if (arg_is(a, "--csv")) {
			o->csv_path = NEXT();
		} else if (arg_is(a, "--json")) {
			o->json_path = NEXT();
		} else if (arg_is(a, "--corpus-tag")) {
			o->corpus_tag = NEXT();
		} else if (arg_is(a, "--seed")) {
			o->seed = (uint32_t)atoi(safe_str(NEXT()));
		} else if (arg_is(a, "--tile")) {
			o->tile = (uint32_t)atoi(safe_str(NEXT()));
		} else if (arg_is(a, "--frame-interval-ns")) {
			o->frame_interval_ns =
				(uint64_t)strtoull(safe_str(NEXT()), NULL, 10);
		} else if (arg_is(a, "--list")) {
			o->list_only = true;
		} else if (arg_is(a, "--verify")) {
			o->verify = true;
		} else if (arg_is(a, "--predictor")) {
			o->predictor = NEXT();
		} else if (arg_is(a, "--keyframe-interval")) {
			o->keyframe_interval =
				(uint32_t)atoi(safe_str(NEXT()));
		} else if (arg_is(a, "--adaptive")) {
			o->adaptive = true;
		} else if (arg_is(a, "--help") || arg_is(a, "-h")) {
			return false;
		} else {
			fprintf(stderr, "unknown argument: %s\n", a);
			return false;
		}
#undef NEXT
	}
	return true;
}

static void print_usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [--mode frame|sequence|damage-stream|temporal-policy]\n"
		"          [--codec NAME|all] [--input PATH] [--synthetic PATTERN]\n"
		"          [--synthetic-sequence PATTERN] [--width W] [--height H]\n"
		"          [--frames N] [--rect WxH|full] [--iterations N]\n"
		"          [--warmup N] [--usb-mib-s \"30,35,40,45\"] [--verify]\n"
		"          [--quality-dir DIR] [--csv PATH] [--json PATH]\n"
		"          [--corpus-tag NAME] [--seed N] [--tile N]\n"
		"          [--frame-interval-ns N] [--predictor NAME] [--list]\n"
		"          [--keyframe-interval N] [--adaptive]\n"
		"\n"
		"--verify: round-trip verification (exact match for lossless\n"
		"  codecs) and quality-metric computation always run regardless\n"
		"  of this flag; --verify additionally makes any lossless\n"
		"  round-trip mismatch a fatal (nonzero exit) error.\n"
		"\n"
		"--mode temporal-policy: PROJECT SPEC next-phase Phase 4/8.\n"
		"  --keyframe-interval N: 0 = keyframe only on the first frame\n"
		"  (no automatic re-keyframing); N>0 = keyframe every N frames.\n"
		"  --adaptive: TEMPORAL_ADAPTIVE -- encode both the normal and\n"
		"  XOR-delta candidates every frame and keep whichever is\n"
		"  smaller (overrides --keyframe-interval's cadence, though a\n"
		"  fresh/reset reference still forces a keyframe).\n",
		prog);
}

static size_t parse_double_list(const char *s, double *out, size_t max)
{
	size_t n = 0;
	const char *p = s;

	while (*p && n < max) {
		char *end;
		double v = strtod(p, &end);

		if (end == p)
			break;
		out[n++] = v;
		p = end;
		while (*p == ',' || *p == ' ')
			p++;
	}
	return n;
}

static void mkdir_p(const char *path)
{
	mkdir(path, 0755); /* best-effort; ignore EEXIST/other errors */
}

/* ---------------------------------------------------------------------- */
/* USB / FPS modeling (PROJECT SPEC sections 20-21).                      */
/* ---------------------------------------------------------------------- */

static void compute_usb_models(bench_result *r, const double *usb_mib_s,
				size_t usb_count)
{
	size_t i;

	r->usb_model_count = 0;
	if (!r->roundtrip_verified || r->mean_encoded_bytes <= 0.0)
		return; /* encode failed for every iteration: no meaningful
			 * USB/FPS model to compute (and dividing by a zero
			 * transfer time would otherwise produce a
			 * non-finite, JSON-invalid "inf" value).
			 */
	for (i = 0; i < usb_count && i < RESULT_MAX_USB_POINTS; i++) {
		double throughput_bytes_per_s = usb_mib_s[i] * 1048576.0;
		double transfer_ns =
			(r->mean_encoded_bytes / throughput_bytes_per_s) *
			1.0e9;
		usb_model_point *m = &r->usb_models[r->usb_model_count++];

		m->usb_mib_s = usb_mib_s[i];
		m->usb_transfer_ns = transfer_ns;
		m->fps_usb_only = 1.0e9 / transfer_ns;
		m->fps_encode_usb =
			1.0e9 / (r->encode_ns.mean + transfer_ns);
		m->fps_total_serial =
			1.0e9 / (r->encode_ns.mean + transfer_ns +
				 r->decode_ns.mean);
	}
}

/* ---------------------------------------------------------------------- */
/* Frame-mode benchmarking: one candidate against one fixed-size buffer.   */
/* ---------------------------------------------------------------------- */

static bool run_codec_on_frame(const fbcodec_desc *cd, const uint16_t *src,
				uint32_t w, uint32_t h, size_t iterations,
				size_t warmup, const double *usb_mib_s,
				size_t usb_count, const char *corpus,
				const char *rect_class,
				const char *quality_dir, bench_result *out)
{
	size_t bound = cd->bound(w, h);
	uint8_t *encoded = malloc(bound ? bound : 1);
	uint16_t *decoded = malloc(rgb565_byte_size(w, h));
	uint64_t *enc_samples = malloc(iterations * sizeof(uint64_t));
	uint64_t *dec_samples = malloc(iterations * sizeof(uint64_t));
	void *ctx = NULL;
	size_t k;
	size_t last_encoded_len = 0;
	double min_bytes = 0, max_bytes = 0, sum_bytes = 0;
	bool ok = true;

	if (!encoded || !decoded || !enc_samples || !dec_samples) {
		free(encoded);
		free(decoded);
		free(enc_samples);
		free(dec_samples);
		return false;
	}

	if (cd->create)
		ctx = cd->create(w, h);

	/* Warm-up (not timed). */
	for (k = 0; k < warmup; k++) {
		size_t n = cd->encode(ctx, src, w, h, encoded, bound);

		if (n != (size_t)-1)
			cd->decode(ctx, encoded, n, decoded, w, h);
		if (cd->reset)
			cd->reset(ctx);
	}

	for (k = 0; k < iterations; k++) {
		uint64_t t0, t1;
		size_t n;

		t0 = fbcodec_now_ns();
		n = cd->encode(ctx, src, w, h, encoded, bound);
		t1 = fbcodec_now_ns();
		enc_samples[k] = t1 - t0;

		if (n == (size_t)-1) {
			ok = false;
			continue;
		}
		last_encoded_len = n;
		if (k == 0) {
			min_bytes = max_bytes = (double)n;
		} else {
			if ((double)n < min_bytes)
				min_bytes = (double)n;
			if ((double)n > max_bytes)
				max_bytes = (double)n;
		}
		sum_bytes += (double)n;

		t0 = fbcodec_now_ns();
		if (cd->decode(ctx, encoded, n, decoded, w, h) != 0)
			ok = false;
		t1 = fbcodec_now_ns();
		dec_samples[k] = t1 - t0;

		if (cd->reset)
			cd->reset(ctx);
	}

	memset(out, 0, sizeof(*out));
	strncpy(out->codec, cd->name, RESULT_NAME_LEN - 1);
	strncpy(out->label, cd->label, RESULT_NAME_LEN * 2 - 1);
	strncpy(out->category, cd->category, RESULT_NAME_LEN - 1);
	strncpy(out->complexity, cd->complexity, sizeof(out->complexity) - 1);
	out->is_lossy = cd->is_lossy;
	out->is_stateful = cd->is_stateful;
	out->native_rgb565 = cd->native_rgb565;
	strncpy(out->corpus, corpus, RESULT_TAG_LEN - 1);
	strncpy(out->rect_class, rect_class, RESULT_TAG_LEN - 1);
	out->width = w;
	out->height = h;
	out->iterations = iterations;
	out->input_bytes = rgb565_byte_size(w, h);
	out->mean_encoded_bytes = iterations ? sum_bytes / (double)iterations
					     : 0.0;
	out->min_encoded_bytes = min_bytes;
	out->max_encoded_bytes = max_bytes;
	out->compression_ratio =
		out->mean_encoded_bytes > 0
			? (double)out->input_bytes / out->mean_encoded_bytes
			: 0.0;
	out->bytes_per_pixel =
		out->mean_encoded_bytes / (double)((size_t)w * h);

	out->encode_ns = stats_compute(enc_samples, iterations);
	out->decode_ns = stats_compute(dec_samples, iterations);

	{
		double mean_bytes = out->mean_encoded_bytes;

		out->encode_mb_per_s =
			out->encode_ns.mean > 0
				? (mean_bytes / 1.0e6) /
					  (out->encode_ns.mean / 1.0e9)
				: 0.0;
		out->decode_mb_per_s =
			out->decode_ns.mean > 0
				? (mean_bytes / 1.0e6) /
					  (out->decode_ns.mean / 1.0e9)
				: 0.0;
		out->encode_mib_per_s =
			out->encode_ns.mean > 0
				? (mean_bytes / 1048576.0) /
					  (out->encode_ns.mean / 1.0e9)
				: 0.0;
		out->decode_mib_per_s =
			out->decode_ns.mean > 0
				? (mean_bytes / 1048576.0) /
					  (out->decode_ns.mean / 1.0e9)
				: 0.0;
	}

	out->roundtrip_verified = ok && iterations > 0;
	if (out->roundtrip_verified) {
		if (!cd->is_lossy) {
			out->roundtrip_exact =
				memcmp(src, decoded,
				       rgb565_byte_size(w, h)) == 0;
		} else {
			out->roundtrip_exact = false; /* not applicable */
		}
		out->has_quality = true;
		out->quality = metrics_compute(src, decoded, w, h);
	}

	compute_usb_models(out, usb_mib_s, usb_count);

	if (quality_dir && cd->is_lossy && iterations > 0) {
		char path[1024];

		mkdir_p(quality_dir);
		snprintf(path, sizeof(path), "%s/reconstructed-%s-%s-%s.png",
			 quality_dir, corpus, rect_class, cd->name);
		png_write_rgb565(path, decoded, w, h);
		snprintf(path, sizeof(path), "%s/diff-%s-%s-%s.png",
			 quality_dir, corpus, rect_class, cd->name);
		png_write_diff(path, src, decoded, w, h, 8.0);
	}

	(void)last_encoded_len;

	if (cd->destroy)
		cd->destroy(ctx);
	free(encoded);
	free(decoded);
	free(enc_samples);
	free(dec_samples);
	return true;
}

/* ---------------------------------------------------------------------- */
/* Sequence-mode benchmarking: N frames, stateful codecs keep state.       */
/* ---------------------------------------------------------------------- */

static bool run_codec_on_sequence(const fbcodec_desc *cd,
				   const rgb565_buffer *frames,
				   uint32_t frame_count, uint32_t w,
				   uint32_t h, const double *usb_mib_s,
				   size_t usb_count, const char *corpus,
				   bench_result *out)
{
	size_t bound = cd->bound(w, h);
	uint8_t *encoded = malloc(bound ? bound : 1);
	uint16_t *decoded = malloc(rgb565_byte_size(w, h));
	uint64_t *enc_samples = malloc(frame_count * sizeof(uint64_t));
	uint64_t *dec_samples = malloc(frame_count * sizeof(uint64_t));
	/* Stateful (temporal) codecs must use *independent* encoder/decoder
	 * contexts, exactly like a real sender/receiver pair: each side only
	 * ever reconstructs its reference from what it produced/received
	 * itself. Sharing one context here would let the decoder silently
	 * "see" the encoder's just-updated reference before decoding,
	 * masking exactly the drift bugs this benchmark needs to catch.
	 */
	void *enc_ctx = NULL;
	void *dec_ctx = NULL;
	uint32_t k;
	double min_bytes = 0, max_bytes = 0, sum_bytes = 0;
	bool all_exact = true;
	bool ok = true;

	if (!encoded || !decoded || !enc_samples || !dec_samples) {
		free(encoded);
		free(decoded);
		free(enc_samples);
		free(dec_samples);
		return false;
	}

	bool is_qoir = strncmp(cd->name, "qoir", 4) == 0;
	uint64_t *enc_conv_samples = is_qoir
					     ? malloc(frame_count *
						      sizeof(uint64_t))
					     : NULL;
	uint64_t *enc_codec_samples = is_qoir
					      ? malloc(frame_count *
						       sizeof(uint64_t))
					      : NULL;
	uint64_t *dec_conv_samples = is_qoir
					     ? malloc(frame_count *
						      sizeof(uint64_t))
					     : NULL;
	uint64_t *dec_codec_samples = is_qoir
					      ? malloc(frame_count *
						       sizeof(uint64_t))
					      : NULL;

	if (cd->create) {
		enc_ctx = cd->create(w, h);
		dec_ctx = cd->create(w, h);
	}

	for (k = 0; k < frame_count; k++) {
		uint64_t t0, t1;
		size_t n;
		const uint16_t *src = frames[k].pixels;

		t0 = fbcodec_now_ns();
		n = cd->encode(enc_ctx, src, w, h, encoded, bound);
		t1 = fbcodec_now_ns();
		enc_samples[k] = t1 - t0;
		if (is_qoir) {
			double conv_ns = 0, codec_ns = 0;

			qoir_last_encode_breakdown_ns(&conv_ns, &codec_ns);
			enc_conv_samples[k] = (uint64_t)conv_ns;
			enc_codec_samples[k] = (uint64_t)codec_ns;
		}

		if (n == (size_t)-1) {
			ok = false;
			dec_samples[k] = 0;
			continue;
		}
		if (k == 0) {
			min_bytes = max_bytes = (double)n;
		} else {
			if ((double)n < min_bytes)
				min_bytes = (double)n;
			if ((double)n > max_bytes)
				max_bytes = (double)n;
		}
		sum_bytes += (double)n;

		t0 = fbcodec_now_ns();
		if (cd->decode(dec_ctx, encoded, n, decoded, w, h) != 0)
			ok = false;
		t1 = fbcodec_now_ns();
		dec_samples[k] = t1 - t0;
		if (is_qoir) {
			double conv_ns = 0, codec_ns = 0;

			qoir_last_decode_breakdown_ns(&conv_ns, &codec_ns);
			dec_conv_samples[k] = (uint64_t)conv_ns;
			dec_codec_samples[k] = (uint64_t)codec_ns;
		}

		if (!cd->is_lossy &&
		    memcmp(src, decoded, rgb565_byte_size(w, h)) != 0)
			all_exact = false;
	}

	memset(out, 0, sizeof(*out));
	strncpy(out->codec, cd->name, RESULT_NAME_LEN - 1);
	strncpy(out->label, cd->label, RESULT_NAME_LEN * 2 - 1);
	strncpy(out->category, cd->category, RESULT_NAME_LEN - 1);
	strncpy(out->complexity, cd->complexity, sizeof(out->complexity) - 1);
	out->is_lossy = cd->is_lossy;
	out->is_stateful = cd->is_stateful;
	out->native_rgb565 = cd->native_rgb565;
	strncpy(out->corpus, corpus, RESULT_TAG_LEN - 1);
	strncpy(out->rect_class, "full-sequence", RESULT_TAG_LEN - 1);
	out->width = w;
	out->height = h;
	out->iterations = frame_count;
	out->input_bytes = rgb565_byte_size(w, h);
	out->mean_encoded_bytes = frame_count ? sum_bytes / (double)frame_count
					      : 0.0;
	out->min_encoded_bytes = min_bytes;
	out->max_encoded_bytes = max_bytes;
	out->compression_ratio =
		out->mean_encoded_bytes > 0
			? (double)out->input_bytes / out->mean_encoded_bytes
			: 0.0;
	out->bytes_per_pixel =
		out->mean_encoded_bytes / (double)((size_t)w * h);
	out->encode_ns = stats_compute(enc_samples, frame_count);
	out->decode_ns = stats_compute(dec_samples, frame_count);
	{
		double mean_bytes = out->mean_encoded_bytes;

		out->encode_mb_per_s =
			out->encode_ns.mean > 0
				? (mean_bytes / 1.0e6) /
					  (out->encode_ns.mean / 1.0e9)
				: 0.0;
		out->decode_mb_per_s =
			out->decode_ns.mean > 0
				? (mean_bytes / 1.0e6) /
					  (out->decode_ns.mean / 1.0e9)
				: 0.0;
		out->encode_mib_per_s =
			out->encode_ns.mean > 0
				? (mean_bytes / 1048576.0) /
					  (out->encode_ns.mean / 1.0e9)
				: 0.0;
		out->decode_mib_per_s =
			out->decode_ns.mean > 0
				? (mean_bytes / 1048576.0) /
					  (out->decode_ns.mean / 1.0e9)
				: 0.0;
	}
	out->roundtrip_verified = ok;
	out->roundtrip_exact = ok && all_exact;
	out->has_quality = ok && frame_count > 0;
	if (out->has_quality)
		out->quality = metrics_compute(frames[frame_count - 1].pixels,
						decoded, w, h);
	compute_usb_models(out, usb_mib_s, usb_count);

	if (is_qoir && frame_count > 0) {
		out->has_conversion_breakdown = true;
		out->encode_conversion_ns =
			stats_compute(enc_conv_samples, frame_count);
		out->encode_codec_ns =
			stats_compute(enc_codec_samples, frame_count);
		out->decode_conversion_ns =
			stats_compute(dec_conv_samples, frame_count);
		out->decode_codec_ns =
			stats_compute(dec_codec_samples, frame_count);
	}
	free(enc_conv_samples);
	free(enc_codec_samples);
	free(dec_conv_samples);
	free(dec_codec_samples);

	if (cd->destroy) {
		cd->destroy(enc_ctx);
		cd->destroy(dec_ctx);
	}
	free(encoded);
	free(decoded);
	free(enc_samples);
	free(dec_samples);
	return true;
}

/* ---------------------------------------------------------------------- */
/* Damage-stream mode: stateless codecs replayed against real event sizes. */
/* ---------------------------------------------------------------------- */

static bool run_codec_on_damage_stream(const fbcodec_desc *cd,
					const damage_stream *ds,
					const double *usb_mib_s,
					size_t usb_count, const char *corpus,
					bench_result *out)
{
	uint64_t *enc_samples = malloc(ds->count * sizeof(uint64_t));
	uint64_t *dec_samples = malloc(ds->count * sizeof(uint64_t));
	double sum_bytes = 0, min_bytes = 0, max_bytes = 0;
	size_t i;
	bool ok = true;
	bool all_exact = true;
	size_t verified = 0;

	if (cd->is_stateful) {
		free(enc_samples);
		free(dec_samples);
		return false; /* documented scoping: see README.md */
	}
	if (!enc_samples || !dec_samples) {
		free(enc_samples);
		free(dec_samples);
		return false;
	}

	for (i = 0; i < ds->count; i++) {
		const damage_event *e = &ds->events[i];
		size_t bound = cd->bound(e->w, e->h);
		uint8_t *encoded = malloc(bound ? bound : 1);
		uint16_t *decoded = malloc(rgb565_byte_size(e->w, e->h));
		uint64_t t0, t1;
		size_t n;

		if (!encoded || !decoded) {
			free(encoded);
			free(decoded);
			ok = false;
			enc_samples[i] = 0;
			dec_samples[i] = 0;
			continue;
		}

		t0 = fbcodec_now_ns();
		n = cd->encode(NULL, e->pixels, e->w, e->h, encoded, bound);
		t1 = fbcodec_now_ns();
		enc_samples[i] = t1 - t0;

		if (n == (size_t)-1) {
			ok = false;
			dec_samples[i] = 0;
			free(encoded);
			free(decoded);
			continue;
		}
		if (verified == 0) {
			min_bytes = max_bytes = (double)n;
		} else {
			if ((double)n < min_bytes)
				min_bytes = (double)n;
			if ((double)n > max_bytes)
				max_bytes = (double)n;
		}
		sum_bytes += (double)n;
		verified++;

		t0 = fbcodec_now_ns();
		if (cd->decode(NULL, encoded, n, decoded, e->w, e->h) != 0)
			ok = false;
		t1 = fbcodec_now_ns();
		dec_samples[i] = t1 - t0;

		if (!cd->is_lossy &&
		    memcmp(e->pixels, decoded,
			   rgb565_byte_size(e->w, e->h)) != 0)
			all_exact = false;

		free(encoded);
		free(decoded);
	}

	memset(out, 0, sizeof(*out));
	strncpy(out->codec, cd->name, RESULT_NAME_LEN - 1);
	strncpy(out->label, cd->label, RESULT_NAME_LEN * 2 - 1);
	strncpy(out->category, cd->category, RESULT_NAME_LEN - 1);
	strncpy(out->complexity, cd->complexity, sizeof(out->complexity) - 1);
	out->is_lossy = cd->is_lossy;
	out->is_stateful = cd->is_stateful;
	out->native_rgb565 = cd->native_rgb565;
	strncpy(out->corpus, corpus, RESULT_TAG_LEN - 1);
	strncpy(out->rect_class, "damage-stream", RESULT_TAG_LEN - 1);
	out->width = ds->width;
	out->height = ds->height;
	out->iterations = ds->count;
	out->input_bytes = 0; /* varies per event; see mean_encoded_bytes */
	out->mean_encoded_bytes = verified ? sum_bytes / (double)verified
					    : 0.0;
	out->min_encoded_bytes = min_bytes;
	out->max_encoded_bytes = max_bytes;
	out->compression_ratio = 0.0; /* not meaningful across mixed sizes */
	out->bytes_per_pixel = 0.0;
	out->encode_ns = stats_compute(enc_samples, ds->count);
	out->decode_ns = stats_compute(dec_samples, ds->count);
	out->roundtrip_verified = ok;
	out->roundtrip_exact = ok && all_exact;
	out->has_quality = false;
	compute_usb_models(out, usb_mib_s, usb_count);

	free(enc_samples);
	free(dec_samples);
	return true;
}

/* ---------------------------------------------------------------------- */
/* main                                                                    */
/* ---------------------------------------------------------------------- */

static bool codec_selected(const fbcodec_desc *cd, const char *want)
{
	/* PROJECT SPEC next-phase Phase 3 "narrow the harness to the
	 * specified full-frame finalists": --codec also accepts a
	 * comma-separated list of names (e.g. "rgb565-lz4,qoir-lossless"),
	 * in addition to the original "all" or single-name forms, so a
	 * single invocation can restrict the sweep to exactly the
	 * finalist shortlist instead of paying for every candidate.
	 */
	const char *p = want;
	size_t name_len = strlen(cd->name);

	if (strcmp(want, "all") == 0)
		return true;

	while (*p) {
		const char *comma = strchr(p, ',');
		size_t tok_len = comma ? (size_t)(comma - p) : strlen(p);

		if (tok_len == name_len && strncmp(p, cd->name, tok_len) == 0)
			return true;

		if (!comma)
			break;
		p = comma + 1;
	}
	return false;
}

int main(int argc, char **argv)
{
	cli_options o;
	const fbcodec_desc *registry[FBCODEC_MAX_CODECS];
	size_t registry_count;
	double usb_values[16];
	size_t usb_count;
	bench_result *results;
	size_t result_count = 0, result_cap = 64;
	size_t i;

	if (!parse_args(argc, argv, &o)) {
		print_usage(argv[0]);
		return 1;
	}

	registry_count = fbcodec_registry(registry, FBCODEC_MAX_CODECS);

	if (o.list_only) {
		for (i = 0; i < registry_count; i++) {
			printf("%-28s %-10s lossy=%d stateful=%d "
			       "native565=%d complexity=%s  %s\n",
			       registry[i]->name, registry[i]->category,
			       registry[i]->is_lossy, registry[i]->is_stateful,
			       registry[i]->native_rgb565,
			       registry[i]->complexity, registry[i]->label);
		}
		return 0;
	}

	usb_count = parse_double_list(o.usb_list, usb_values, 16);
	results = calloc(result_cap, sizeof(bench_result));
	if (!results) {
		fprintf(stderr, "out of memory\n");
		return 1;
	}

	if (strcmp(o.mode, "frame") == 0) {
		rgb565_buffer frame;
		uint32_t rw = o.width, rh = o.height;
		uint32_t rx0 = 0, ry0 = 0;
		char corpus_name[128];
		char rect_name[64];

		if (o.input) {
			if (capture_read_raw_frames(o.input, o.width,
						      o.height, 1,
						      &frame) != 1) {
				fprintf(stderr,
					"failed to read frame from %s\n",
					o.input);
				return 1;
			}
			snprintf(corpus_name, sizeof(corpus_name), "%s",
				 o.corpus_tag ? o.corpus_tag : "real-asset");
		} else {
			const char *pattern = o.synthetic ? o.synthetic
							   : "gradient-2d";

			if (!synth_generate(pattern, o.width, o.height,
					      o.seed, &frame)) {
				fprintf(stderr, "unknown pattern: %s\n",
					pattern);
				return 1;
			}
			snprintf(corpus_name, sizeof(corpus_name), "%s",
				 o.corpus_tag ? o.corpus_tag : pattern);
		}

		if (strcmp(o.rect, "full") == 0) {
			rw = o.width;
			rh = o.height;
			snprintf(rect_name, sizeof(rect_name), "full-%ux%u",
				 o.width, o.height);
		} else {
			unsigned rw_u = 0, rh_u = 0;

			sscanf(o.rect, "%ux%u", &rw_u, &rh_u);
			rw = rw_u ? rw_u : o.width;
			rh = rh_u ? rh_u : o.height;
			if (rw > o.width)
				rw = o.width;
			if (rh > o.height)
				rh = o.height;
			snprintf(rect_name, sizeof(rect_name), "%ux%u", rw,
				 rh);
		}

		{
			rgb565_buffer cropped;

			if (!rgb565_buffer_alloc(&cropped, rw, rh)) {
				fprintf(stderr, "OOM cropping frame\n");
				return 1;
			}
			{
				uint32_t yy;

				for (yy = 0; yy < rh; yy++)
					memcpy(cropped.pixels +
						       (size_t)yy * rw,
					       frame.pixels +
						       (size_t)(ry0 + yy) *
							       frame.width +
						       rx0,
					       (size_t)rw * sizeof(uint16_t));
			}

			for (i = 0; i < registry_count; i++) {
				const fbcodec_desc *cd = registry[i];

				if (!codec_selected(cd, o.codec))
					continue;
				if (cd->is_stateful)
					continue; /* use --mode sequence */

				if (result_count == result_cap) {
					result_cap *= 2;
					results = realloc(
						results,
						result_cap *
							sizeof(bench_result));
				}
				run_codec_on_frame(
					cd, cropped.pixels, rw, rh,
					o.iterations, o.warmup, usb_values,
					usb_count, corpus_name, rect_name,
					o.quality_dir,
					&results[result_count++]);
			}

			if (o.quality_dir) {
				char path[1024];

				mkdir_p(o.quality_dir);
				snprintf(path, sizeof(path),
					 "%s/original-%s-%s.png",
					 o.quality_dir, corpus_name,
					 rect_name);
				png_write_rgb565(path, cropped.pixels, rw, rh);
			}

			rgb565_buffer_free(&cropped);
		}
		rgb565_buffer_free(&frame);
	} else if (strcmp(o.mode, "sequence") == 0) {
		rgb565_buffer *frames = calloc(o.frames, sizeof(rgb565_buffer));
		char corpus_name[128];
		size_t got = 0;

		if (!frames) {
			fprintf(stderr, "OOM\n");
			return 1;
		}

		if (o.input) {
			got = capture_read_raw_frames(o.input, o.width,
							o.height, o.frames,
							frames);
			snprintf(corpus_name, sizeof(corpus_name), "%s",
				 o.corpus_tag ? o.corpus_tag : "real-asset");
		} else {
			const char *pattern = o.synthetic_sequence
						      ? o.synthetic_sequence
						      : "small-changes";

			if (synth_generate_sequence(pattern, o.width,
						      o.height, o.seed,
						      o.frames, frames))
				got = o.frames;
			snprintf(corpus_name, sizeof(corpus_name), "%s",
				 o.corpus_tag ? o.corpus_tag : pattern);
		}

		if (got == 0) {
			fprintf(stderr, "failed to obtain frame sequence\n");
			free(frames);
			return 1;
		}

		for (i = 0; i < registry_count; i++) {
			const fbcodec_desc *cd = registry[i];

			if (!codec_selected(cd, o.codec))
				continue;

			if (result_count == result_cap) {
				result_cap *= 2;
				results = realloc(
					results,
					result_cap * sizeof(bench_result));
			}
			run_codec_on_sequence(cd, frames, (uint32_t)got,
					       o.width, o.height, usb_values,
					       usb_count, corpus_name,
					       &results[result_count++]);
		}

		for (i = 0; i < got; i++)
			rgb565_buffer_free(&frames[i]);
		free(frames);
	} else if (strcmp(o.mode, "damage-stream") == 0) {
		damage_stream ds;
		char corpus_name[128];
		bool have_ds = false;

		memset(&ds, 0, sizeof(ds));

		if (o.input && strstr(o.input, ".fbcb")) {
			have_ds = capture_read_fbcb(o.input, &ds);
			snprintf(corpus_name, sizeof(corpus_name), "%s",
				 o.corpus_tag ? o.corpus_tag
					      : "damage-stream-capture");
		} else if (o.input) {
			rgb565_buffer *frames =
				calloc(o.frames, sizeof(rgb565_buffer));
			size_t got;

			if (!frames) {
				fprintf(stderr, "OOM\n");
				return 1;
			}
			got = capture_read_raw_frames(o.input, o.width,
							o.height, o.frames,
							frames);
			if (got > 0)
				have_ds = capture_make_damage_stream_from_frames(
					frames, got, o.tile,
					o.frame_interval_ns, &ds);
			for (i = 0; i < got; i++)
				rgb565_buffer_free(&frames[i]);
			free(frames);
			snprintf(corpus_name, sizeof(corpus_name), "%s",
				 o.corpus_tag ? o.corpus_tag
					      : "real-motion-damage-stream");
		} else {
			fprintf(stderr,
				"damage-stream mode requires --input\n");
			return 1;
		}

		if (!have_ds) {
			fprintf(stderr, "failed to build damage stream\n");
			return 1;
		}

		for (i = 0; i < registry_count; i++) {
			const fbcodec_desc *cd = registry[i];

			if (!codec_selected(cd, o.codec))
				continue;
			if (cd->is_stateful)
				continue;

			if (result_count == result_cap) {
				result_cap *= 2;
				results = realloc(
					results,
					result_cap * sizeof(bench_result));
			}
			if (run_codec_on_damage_stream(
				    cd, &ds, usb_values, usb_count,
				    corpus_name, &results[result_count]))
				result_count++;
		}

		fprintf(stderr, "damage-stream: %zu events, %ux%u\n",
			ds.count, ds.width, ds.height);
		damage_stream_free(&ds);
	} else if (strcmp(o.mode, "temporal-policy") == 0) {
		/* PROJECT SPEC next-phase Phase 4 "Temporal keyframe model"
		 * and Phase 8 "Temporal XOR analysis": drives the
		 * KEYFRAME/DELTA (or TEMPORAL_ADAPTIVE) protocol in
		 * transforms/temporal_policy.c across a real or synthetic
		 * frame sequence, with fully independent encoder/decoder
		 * instances (same rule as --mode sequence), verifies every
		 * frame round-trips exactly, and reports per-frame-type
		 * byte/timing distributions plus how often adaptive mode
		 * picks the delta candidate.
		 */
		rgb565_buffer *frames = calloc(o.frames, sizeof(rgb565_buffer));
		char corpus_name[128];
		size_t got = 0;
		tpolicy_encoder *enc;
		tpolicy_decoder *dec;
		uint8_t *wire;
		uint16_t *decoded;
		size_t wire_cap;
		uint64_t *encode_ns_samples;
		uint64_t *keyframe_bytes = NULL, *delta_bytes = NULL;
		size_t keyframe_n = 0, delta_n = 0;
		size_t adaptive_delta_wins = 0, adaptive_decisions = 0;
		size_t total_bytes = 0;
		size_t mismatch_count = 0;
		FILE *jf = NULL;

		if (!frames) {
			fprintf(stderr, "OOM\n");
			return 1;
		}
		if (o.input) {
			got = capture_read_raw_frames(o.input, o.width,
							o.height, o.frames,
							frames);
			snprintf(corpus_name, sizeof(corpus_name), "%s",
				 o.corpus_tag ? o.corpus_tag : "real-asset");
		} else {
			const char *pattern = o.synthetic_sequence
						      ? o.synthetic_sequence
						      : "small-changes";

			if (synth_generate_sequence(pattern, o.width,
						      o.height, o.seed,
						      o.frames, frames))
				got = o.frames;
			snprintf(corpus_name, sizeof(corpus_name), "%s",
				 o.corpus_tag ? o.corpus_tag : pattern);
		}
		if (got == 0) {
			fprintf(stderr, "failed to obtain frame sequence\n");
			free(frames);
			return 1;
		}

		enc = tpolicy_encoder_create(o.width, o.height);
		dec = tpolicy_decoder_create(o.width, o.height);
		wire_cap = tpolicy_bound(o.width, o.height);
		wire = malloc(wire_cap);
		decoded = malloc(rgb565_pixel_count(o.width, o.height) *
				  sizeof(uint16_t));
		encode_ns_samples = malloc(got * sizeof(uint64_t));
		keyframe_bytes = malloc(got * sizeof(uint64_t));
		delta_bytes = malloc(got * sizeof(uint64_t));

		if (o.json_path) {
			jf = fopen(o.json_path, "w");
			if (jf)
				fprintf(jf, "{\"mode\":\"temporal-policy\","
					     "\"corpus\":\"%s\",\"width\":%u,"
					     "\"height\":%u,\"keyframe_interval\":%u,"
					     "\"adaptive\":%s,\"frames\":[\n",
					corpus_name, o.width, o.height,
					o.keyframe_interval,
					o.adaptive ? "true" : "false");
		}

		for (i = 0; i < got; i++) {
			tpolicy_frame_record rec;
			size_t len;
			enum tpolicy_decode_status st;

			len = tpolicy_encode(
				enc, frames[i].pixels, o.width, o.height,
				o.adaptive ? TPOLICY_ADAPTIVE
					   : TPOLICY_FIXED_INTERVAL,
				o.keyframe_interval, (uint32_t)i, wire,
				wire_cap, &rec);
			if (len == (size_t)-1) {
				fprintf(stderr,
					"temporal-policy encode failed at "
					"frame %zu\n",
					i);
				continue;
			}
			encode_ns_samples[i] = (uint64_t)rec.encode_ns;
			total_bytes += rec.compressed_size;
			if (rec.frame_type == TPOLICY_KEYFRAME)
				keyframe_bytes[keyframe_n++] =
					rec.compressed_size;
			else
				delta_bytes[delta_n++] = rec.compressed_size;
			if (o.adaptive && i > 0) {
				adaptive_decisions++;
				if (rec.frame_type == TPOLICY_DELTA)
					adaptive_delta_wins++;
			}

			st = tpolicy_decode(dec, rec.frame_type,
					     rec.sequence_number,
					     rec.base_sequence_number, wire,
					     len, decoded, o.width, o.height);
			if (st != TPOLICY_OK) {
				mismatch_count++;
				fprintf(stderr,
					"temporal-policy: decode status %d "
					"at frame %zu\n",
					(int)st, i);
				continue;
			}
			if (memcmp(decoded, frames[i].pixels,
				   rgb565_byte_size(o.width, o.height)) != 0) {
				fprintf(stderr,
					"temporal-policy: MISMATCH at frame "
					"%zu (round-trip not exact)\n",
					i);
				mismatch_count++;
				if (o.verify) {
					free(encode_ns_samples);
					free(keyframe_bytes);
					free(delta_bytes);
					free(wire);
					free(decoded);
					tpolicy_encoder_destroy(enc);
					tpolicy_decoder_destroy(dec);
					for (i = 0; i < got; i++)
						rgb565_buffer_free(&frames[i]);
					free(frames);
					if (jf)
						fclose(jf);
					free(results);
					return 1;
				}
			}

			if (jf)
				fprintf(jf,
					"%s{\"seq\":%zu,\"type\":\"%s\","
					 "\"base_seq\":%d,\"bytes\":%zu,"
					 "\"encode_ns\":%.0f,"
					 "\"normal_candidate_bytes\":%zu,"
					 "\"delta_candidate_bytes\":%zu}",
					i ? ",\n" : "",
					i,
					rec.frame_type == TPOLICY_KEYFRAME
						? "KEYFRAME"
						: "DELTA",
					rec.base_sequence_number ==
							TPOLICY_NO_SEQ
						? -1
						: (int)rec.base_sequence_number,
					rec.compressed_size, rec.encode_ns,
					rec.normal_candidate_bytes ==
							(size_t)-1
						? 0
						: rec.normal_candidate_bytes,
					rec.delta_candidate_bytes ==
							(size_t)-1
						? 0
						: rec.delta_candidate_bytes);
		}

		{
			percentile_stats enc_stats =
				stats_compute(encode_ns_samples, got);
			percentile_stats kf_stats =
				keyframe_n ? stats_compute(keyframe_bytes,
							     keyframe_n)
					   : (percentile_stats){ 0 };
			percentile_stats d_stats =
				delta_n ? stats_compute(delta_bytes, delta_n)
					: (percentile_stats){ 0 };

			if (jf) {
				fprintf(jf,
					"\n],\"summary\":{"
					"\"total_frames\":%zu,"
					"\"keyframe_count\":%zu,"
					"\"delta_count\":%zu,"
					"\"total_bytes\":%zu,"
					"\"mismatch_count\":%zu,"
					"\"adaptive_decisions\":%zu,"
					"\"adaptive_delta_wins\":%zu,"
					"\"encode_ns_mean\":%.0f,"
					"\"encode_ns_p50\":%.0f,"
					"\"encode_ns_p95\":%.0f,"
					"\"encode_ns_p99\":%.0f,"
					"\"keyframe_bytes_mean\":%.0f,"
					"\"delta_bytes_mean\":%.0f}}\n",
					got, keyframe_n, delta_n, total_bytes,
					mismatch_count, adaptive_decisions,
					adaptive_delta_wins, enc_stats.mean,
					enc_stats.median, enc_stats.p95,
					enc_stats.p99, kf_stats.mean,
					d_stats.mean);
				fclose(jf);
			}

			printf("temporal-policy corpus=%s frames=%zu "
			       "keyframes=%zu deltas=%zu total_bytes=%zu "
			       "mismatches=%zu adaptive_delta_wins=%zu/%zu "
			       "encode_ns_mean=%.0f keyframe_bytes_mean=%.0f "
			       "delta_bytes_mean=%.0f\n",
			       corpus_name, got, keyframe_n, delta_n,
			       total_bytes, mismatch_count,
			       adaptive_delta_wins, adaptive_decisions,
			       enc_stats.mean, kf_stats.mean, d_stats.mean);
		}

		free(encode_ns_samples);
		free(keyframe_bytes);
		free(delta_bytes);
		free(wire);
		free(decoded);
		tpolicy_encoder_destroy(enc);
		tpolicy_decoder_destroy(dec);
		for (i = 0; i < got; i++)
			rgb565_buffer_free(&frames[i]);
		free(frames);
		free(results);
		return mismatch_count && o.verify ? 1 : 0;
	} else if (strcmp(o.mode, "residual") == 0) {
		/* Residual/entropy analysis for the predictor-comparison
		 * questions in PROJECT SPEC sections 42-43: for a *single*
		 * named predictor transform, report what fraction of the
		 * transformed byte stream is zero, the mean absolute
		 * residual, and a Shannon entropy estimate that helps
		 * explain *why* one predictor compresses better than
		 * another (not just that it does).
		 */
		const rgb565_transform *t =
			o.predictor ? predict_find(o.predictor) : NULL;
		rgb565_buffer frame;
		uint8_t *transformed;
		residual_stats rs;

		if (!t) {
			fprintf(stderr,
				"--mode residual requires a valid "
				"--predictor NAME (see --list-predictors)\n");
			return 1;
		}
		if (o.input) {
			if (capture_read_raw_frames(o.input, o.width,
						      o.height, 1,
						      &frame) != 1) {
				fprintf(stderr, "failed to read %s\n",
					o.input);
				return 1;
			}
		} else if (!synth_generate(o.synthetic ? o.synthetic
						  : "gradient-2d",
					     o.width, o.height, o.seed,
					     &frame)) {
			fprintf(stderr, "unknown pattern\n");
			return 1;
		}

		transformed = malloc(rgb565_byte_size(o.width, o.height));
		t->fwd(frame.pixels, o.width, o.height, transformed);
		rs = residual_stats_compute(transformed,
					      rgb565_byte_size(o.width,
								 o.height));

		printf("{\"predictor\":\"%s\",\"corpus\":\"%s\",\"width\":%u,"
		       "\"height\":%u,\"zero_byte_percent\":%.3f,"
		       "\"zero_u16_percent\":%.3f,\"mean_abs_byte\":%.3f,"
		       "\"shannon_entropy_bits_per_byte\":%.4f}\n",
		       t->name, o.synthetic ? o.synthetic : "input", o.width,
		       o.height, rs.zero_byte_percent, rs.zero_u16_percent,
		       rs.mean_abs_byte, rs.shannon_entropy_bits_per_byte);

		free(transformed);
		rgb565_buffer_free(&frame);
		free(results);
		return 0;
	} else {
		fprintf(stderr, "unknown --mode %s\n", o.mode);
		return 1;
	}

	if (o.csv_path)
		csv_append_results(o.csv_path, results, result_count);
	if (o.json_path) {
		char meta[512];

		snprintf(meta, sizeof(meta),
			 "{\"mode\":\"%s\",\"iterations\":%zu,\"warmup\":%zu,"
			 "\"seed\":%u}",
			 o.mode, o.iterations, o.warmup, o.seed);
		json_write_results(o.json_path, results, result_count, meta);
	}
	if (!o.csv_path && !o.json_path) {
		/* Always produce *some* visible output. */
		for (i = 0; i < result_count; i++) {
			printf("%-28s corpus=%-16s rect=%-14s bytes=%.0f "
			       "ratio=%.2f enc_ns=%.0f dec_ns=%.0f "
			       "roundtrip_ok=%d\n",
			       results[i].codec, results[i].corpus,
			       results[i].rect_class,
			       results[i].mean_encoded_bytes,
			       results[i].compression_ratio,
			       results[i].encode_ns.mean,
			       results[i].decode_ns.mean,
			       results[i].roundtrip_exact ||
				       results[i].is_lossy);
		}
	}

	/* Round-trip verification (exact-match checking for lossless
	 * codecs) and quality-metric computation already ran unconditionally
	 * for every result above -- that part of PROJECT SPEC section 24 is
	 * mandatory and is not gated by --verify. What --verify adds is
	 * strict enforcement: fail the whole benchmark (nonzero exit) if any
	 * lossless candidate either did not round-trip exactly, or could
	 * not be verified at all (e.g. every encode/decode iteration
	 * failed), instead of that fact being visible only via
	 * roundtrip_verified/roundtrip_exact in the CSV/JSON.
	 */
	if (o.verify) {
		bool any_mismatch = false;

		for (i = 0; i < result_count; i++) {
			const bench_result *r = &results[i];

			if (!r->is_lossy &&
			    (!r->roundtrip_verified || !r->roundtrip_exact)) {
				fprintf(stderr,
					"VERIFY FAIL: lossless codec '%s' %s "
					"(corpus=%s rect=%s)\n",
					r->codec,
					r->roundtrip_verified
						? "did not round-trip exactly"
						: "could not be verified "
						  "(encode/decode failed)",
					r->corpus, r->rect_class);
				any_mismatch = true;
			}
		}
		if (any_mismatch) {
			free(results);
			return 1;
		}
	}

	free(results);
	return 0;
}
