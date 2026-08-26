/*
 * test_temporal_policy.c - PROJECT SPEC next-phase Phase 4 "Temporal
 * keyframe model" and recovery-test validation for
 * transforms/temporal_policy.{h,c}.
 *
 * Covers:
 *   - normal in-order KEYFRAME/DELTA sequencing at a fixed interval;
 *   - TEMPORAL_ADAPTIVE always choosing the smaller candidate;
 *   - forced keyframe;
 *   - lost delta frame -> decoder detects reference mismatch by sequence
 *     number instead of silently decoding a corrupted frame;
 *   - decoder restart -> rejects DELTA until the next KEYFRAME, then
 *     recovers exactly;
 *   - simulated USB reconnect -> decoder reset plus an encoder-forced
 *     keyframe recovers cleanly;
 *   - corrupted compressed payload -> decoder rejects it (does not crash,
 *     does not silently emit garbage), decoder state is left untouched so
 *     a subsequent well-formed frame is unaffected once resynchronized.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/transforms/temporal_policy.h"

static int g_failures;

#define CHECK(cond, msg)                                                    \
	do {                                                                  \
		if (!(cond)) {                                                \
			fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__,   \
				__LINE__);                                    \
			g_failures++;                                         \
		}                                                              \
	} while (0)

static uint32_t xorshift32(uint32_t *state)
{
	uint32_t x = *state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static void fill_random(uint16_t *buf, size_t n, uint32_t seed)
{
	uint32_t state = seed ? seed : 1;
	size_t i;

	for (i = 0; i < n; i++)
		buf[i] = (uint16_t)xorshift32(&state);
}

/* Frames that differ only slightly from frame 0 in a small region, so XOR
 * deltas are small and reliably smaller than a fresh keyframe -- makes the
 * adaptive-selection tests deterministic.
 */
static void make_similar_frame(uint16_t *base, uint16_t *out, size_t w,
				 size_t h, uint32_t seed)
{
	memcpy(out, base, w * h * sizeof(uint16_t));
	{
		uint32_t state = seed ? seed : 7;
		size_t changes = (w * h) / 64; /* ~1.5% of pixels */
		size_t i;

		for (i = 0; i < changes; i++) {
			size_t idx = xorshift32(&state) % (w * h);

			out[idx] = (uint16_t)xorshift32(&state);
		}
	}
}

static void test_fixed_interval_cadence(void)
{
	const uint32_t w = 64, h = 64, interval = 4;
	size_t n = (size_t)w * h;
	tpolicy_encoder *enc = tpolicy_encoder_create(w, h);
	tpolicy_decoder *dec = tpolicy_decoder_create(w, h);
	uint16_t *base = malloc(n * sizeof(uint16_t));
	uint16_t *frame = malloc(n * sizeof(uint16_t));
	uint16_t *decoded = malloc(n * sizeof(uint16_t));
	uint8_t *wire = malloc(tpolicy_bound(w, h));
	uint32_t i;

	fill_random(base, n, 100);

	for (i = 0; i < 10; i++) {
		tpolicy_frame_record rec;
		size_t len;
		enum tpolicy_decode_status st;

		make_similar_frame(base, frame, w, h, 200 + i);
		len = tpolicy_encode(enc, frame, w, h, TPOLICY_FIXED_INTERVAL,
				      interval, i, wire, tpolicy_bound(w, h),
				      &rec);
		CHECK(len != (size_t)-1, "fixed-interval encode succeeds");
		CHECK(rec.frame_type ==
			      (i % interval == 0 ? TPOLICY_KEYFRAME
						  : TPOLICY_DELTA),
		      "keyframe cadence matches configured interval");

		st = tpolicy_decode(dec, rec.frame_type, rec.sequence_number,
				     rec.base_sequence_number, wire, len,
				     decoded, w, h);
		CHECK(st == TPOLICY_OK, "fixed-interval decode succeeds");
		CHECK(memcmp(decoded, frame, n * sizeof(uint16_t)) == 0,
		      "fixed-interval decode reconstructs exact frame");
	}

	free(base);
	free(frame);
	free(decoded);
	free(wire);
	tpolicy_encoder_destroy(enc);
	tpolicy_decoder_destroy(dec);
}

static void test_adaptive_selection(void)
{
	const uint32_t w = 128, h = 128;
	size_t n = (size_t)w * h;
	tpolicy_encoder *enc = tpolicy_encoder_create(w, h);
	uint16_t *base = malloc(n * sizeof(uint16_t));
	uint16_t *similar = malloc(n * sizeof(uint16_t));
	uint16_t *random_frame = malloc(n * sizeof(uint16_t));
	uint8_t *wire = malloc(tpolicy_bound(w, h));
	tpolicy_frame_record rec;
	size_t len;

	fill_random(base, n, 1);
	fill_random(random_frame, n, 999); /* uncorrelated with base */
	make_similar_frame(base, similar, w, h, 2);

	/* Frame 0 must be a keyframe (no reference yet). */
	len = tpolicy_encode(enc, base, w, h, TPOLICY_ADAPTIVE, 0, 0, wire,
			      tpolicy_bound(w, h), &rec);
	CHECK(len != (size_t)-1, "adaptive frame 0 encodes");
	CHECK(rec.frame_type == TPOLICY_KEYFRAME,
	      "adaptive frame 0 is a keyframe (no reference yet)");

	/* A near-identical frame should make the XOR-delta candidate much
	 * smaller than the fresh keyframe candidate, so adaptive selection
	 * must choose DELTA.
	 */
	len = tpolicy_encode(enc, similar, w, h, TPOLICY_ADAPTIVE, 0, 1, wire,
			      tpolicy_bound(w, h), &rec);
	CHECK(len != (size_t)-1, "adaptive similar-frame encodes");
	CHECK(rec.frame_type == TPOLICY_DELTA,
	      "adaptive selection picks DELTA when it is smaller");
	CHECK(rec.delta_candidate_bytes < rec.normal_candidate_bytes,
	      "adaptive delta candidate is smaller than normal candidate");
	CHECK(rec.compressed_size == rec.delta_candidate_bytes,
	      "adaptive compressed_size matches the winning candidate");

	/* An uncorrelated random frame should make XOR-delta *larger* than
	 * (or at least not smaller than) a fresh keyframe of similarly
	 * incompressible content, so adaptive selection must fall back to
	 * KEYFRAME. This is the scrolling/video "automatic fallback"
	 * behavior PROJECT SPEC Phase 4/8 asks about.
	 */
	len = tpolicy_encode(enc, random_frame, w, h, TPOLICY_ADAPTIVE, 0, 2,
			      wire, tpolicy_bound(w, h), &rec);
	CHECK(len != (size_t)-1, "adaptive uncorrelated-frame encodes");
	CHECK(rec.frame_type == TPOLICY_KEYFRAME,
	      "adaptive selection falls back to KEYFRAME for large deltas");

	free(base);
	free(similar);
	free(random_frame);
	free(wire);
	tpolicy_encoder_destroy(enc);
}

static void test_forced_keyframe(void)
{
	const uint32_t w = 32, h = 32;
	size_t n = (size_t)w * h;
	tpolicy_encoder *enc = tpolicy_encoder_create(w, h);
	tpolicy_decoder *dec = tpolicy_decoder_create(w, h);
	uint16_t *base = malloc(n * sizeof(uint16_t));
	uint16_t *frame = malloc(n * sizeof(uint16_t));
	uint16_t *decoded = malloc(n * sizeof(uint16_t));
	uint8_t *wire = malloc(tpolicy_bound(w, h));
	tpolicy_frame_record rec;
	size_t len;
	enum tpolicy_decode_status st;

	fill_random(base, n, 5);

	/* Establish a reference with a keyframe, then a delta. */
	len = tpolicy_encode(enc, base, w, h, TPOLICY_FIXED_INTERVAL, 0, 0,
			      wire, tpolicy_bound(w, h), &rec);
	tpolicy_decode(dec, rec.frame_type, rec.sequence_number,
		       rec.base_sequence_number, wire, len, decoded, w, h);

	make_similar_frame(base, frame, w, h, 6);
	tpolicy_encoder_force_keyframe(enc);
	len = tpolicy_encode(enc, frame, w, h, TPOLICY_FIXED_INTERVAL,
			      0 /* would otherwise never re-keyframe */, 1,
			      wire, tpolicy_bound(w, h), &rec);
	CHECK(rec.frame_type == TPOLICY_KEYFRAME,
	      "forced keyframe overrides interval==0 (reset-only) policy");
	st = tpolicy_decode(dec, rec.frame_type, rec.sequence_number,
			     rec.base_sequence_number, wire, len, decoded, w,
			     h);
	CHECK(st == TPOLICY_OK, "forced-keyframe decode succeeds");
	CHECK(memcmp(decoded, frame, n * sizeof(uint16_t)) == 0,
	      "forced-keyframe decode reconstructs exact frame");

	free(base);
	free(frame);
	free(decoded);
	free(wire);
	tpolicy_encoder_destroy(enc);
	tpolicy_decoder_destroy(dec);
}

/* Recovery test: "Lost delta frame" (PROJECT SPEC next-phase Phase 4/8
 * "Temporal recovery tests"). Drops frame 1's wire payload before it
 * reaches the decoder, then verifies frame 2 (whose base_sequence_number
 * refers to frame 1) is detected as a reference mismatch rather than
 * being blindly XORed against the stale frame-0 reference.
 */
static void test_lost_delta_frame(void)
{
	const uint32_t w = 32, h = 32, interval = 0; /* no automatic keyframes */
	size_t n = (size_t)w * h;
	tpolicy_encoder *enc = tpolicy_encoder_create(w, h);
	tpolicy_decoder *dec = tpolicy_decoder_create(w, h);
	uint16_t *base = malloc(n * sizeof(uint16_t));
	uint16_t *f1 = malloc(n * sizeof(uint16_t));
	uint16_t *f2 = malloc(n * sizeof(uint16_t));
	uint16_t *decoded = malloc(n * sizeof(uint16_t));
	uint8_t *wire0 = malloc(tpolicy_bound(w, h));
	uint8_t *wire1 = malloc(tpolicy_bound(w, h));
	uint8_t *wire2 = malloc(tpolicy_bound(w, h));
	tpolicy_frame_record rec0, rec1, rec2;
	size_t len0, len2;
	enum tpolicy_decode_status st;

	fill_random(base, n, 42);
	make_similar_frame(base, f1, w, h, 43);
	make_similar_frame(f1, f2, w, h, 44);

	len0 = tpolicy_encode(enc, base, w, h, TPOLICY_FIXED_INTERVAL,
			       interval, 0, wire0, tpolicy_bound(w, h), &rec0);
	(void)tpolicy_encode(enc, f1, w, h, TPOLICY_FIXED_INTERVAL, interval,
			      1, wire1, tpolicy_bound(w, h), &rec1);
	len2 = tpolicy_encode(enc, f2, w, h, TPOLICY_FIXED_INTERVAL, interval,
			       2, wire2, tpolicy_bound(w, h), &rec2);
	CHECK(rec1.frame_type == TPOLICY_DELTA && rec2.frame_type == TPOLICY_DELTA,
	      "both follow-on frames are DELTA (interval==0, no auto keyframe)");

	/* Decoder receives frame 0 (fine), then frame 1 is LOST in transit
	 * (never delivered), then frame 2 arrives referring to frame 1.
	 */
	st = tpolicy_decode(dec, rec0.frame_type, rec0.sequence_number,
			     rec0.base_sequence_number, wire0, len0, decoded,
			     w, h);
	CHECK(st == TPOLICY_OK, "keyframe 0 decodes fine");

	/* frame 1 intentionally not delivered to the decoder */

	st = tpolicy_decode(dec, rec2.frame_type, rec2.sequence_number,
			     rec2.base_sequence_number, wire2, len2, decoded,
			     w, h);
	CHECK(st == TPOLICY_ERR_REFERENCE_MISMATCH,
	      "decoder detects the lost-frame-1 reference mismatch by "
	      "sequence number instead of silently corrupting frame 2");

	/* And a subsequent keyframe must still recover cleanly. */
	{
		uint16_t *f3 = malloc(n * sizeof(uint16_t));
		uint8_t *wire3 = malloc(tpolicy_bound(w, h));
		tpolicy_frame_record rec3;
		size_t len3;

		make_similar_frame(f2, f3, w, h, 45);
		tpolicy_encoder_force_keyframe(enc);
		len3 = tpolicy_encode(enc, f3, w, h, TPOLICY_FIXED_INTERVAL,
				       interval, 3, wire3, tpolicy_bound(w, h),
				       &rec3);
		st = tpolicy_decode(dec, rec3.frame_type, rec3.sequence_number,
				     rec3.base_sequence_number, wire3, len3,
				     decoded, w, h);
		CHECK(st == TPOLICY_OK,
		      "a forced keyframe after a lost delta frame recovers");
		CHECK(memcmp(decoded, f3, n * sizeof(uint16_t)) == 0,
		      "post-recovery decode reconstructs the exact frame");
		free(f3);
		free(wire3);
	}

	free(base);
	free(f1);
	free(f2);
	free(decoded);
	free(wire0);
	free(wire1);
	free(wire2);
	tpolicy_encoder_destroy(enc);
	tpolicy_decoder_destroy(dec);
}

/* Recovery test: "Decoder restart" -- decoder state is fully discarded
 * (models a process restart), so it must reject the next DELTA frame
 * (TPOLICY_ERR_NEED_KEYFRAME) until a KEYFRAME arrives.
 */
static void test_decoder_restart(void)
{
	const uint32_t w = 40, h = 40, interval = 0;
	size_t n = (size_t)w * h;
	tpolicy_encoder *enc = tpolicy_encoder_create(w, h);
	tpolicy_decoder *dec = tpolicy_decoder_create(w, h);
	uint16_t *base = malloc(n * sizeof(uint16_t));
	uint16_t *f1 = malloc(n * sizeof(uint16_t));
	uint16_t *decoded = malloc(n * sizeof(uint16_t));
	uint8_t *wire = malloc(tpolicy_bound(w, h));
	tpolicy_frame_record rec;
	size_t len;
	enum tpolicy_decode_status st;

	fill_random(base, n, 11);
	make_similar_frame(base, f1, w, h, 12);

	len = tpolicy_encode(enc, base, w, h, TPOLICY_FIXED_INTERVAL, interval,
			      0, wire, tpolicy_bound(w, h), &rec);
	tpolicy_decode(dec, rec.frame_type, rec.sequence_number,
		       rec.base_sequence_number, wire, len, decoded, w, h);

	/* Simulate the decoder process restarting (all state lost) while
	 * the encoder keeps running unaware.
	 */
	tpolicy_decoder_reset(dec);

	len = tpolicy_encode(enc, f1, w, h, TPOLICY_FIXED_INTERVAL, interval,
			      1, wire, tpolicy_bound(w, h), &rec);
	CHECK(rec.frame_type == TPOLICY_DELTA,
	      "encoder (unaware of the decoder restart) still sends DELTA");
	st = tpolicy_decode(dec, rec.frame_type, rec.sequence_number,
			     rec.base_sequence_number, wire, len, decoded, w,
			     h);
	CHECK(st == TPOLICY_ERR_NEED_KEYFRAME,
	      "restarted decoder rejects DELTA and reports NEED_KEYFRAME "
	      "instead of decoding garbage");

	/* The next keyframe must recover it. */
	tpolicy_encoder_force_keyframe(enc);
	len = tpolicy_encode(enc, f1, w, h, TPOLICY_FIXED_INTERVAL, interval,
			      2, wire, tpolicy_bound(w, h), &rec);
	st = tpolicy_decode(dec, rec.frame_type, rec.sequence_number,
			     rec.base_sequence_number, wire, len, decoded, w,
			     h);
	CHECK(st == TPOLICY_OK,
	      "keyframe after decoder restart recovers correct decode");
	CHECK(memcmp(decoded, f1, n * sizeof(uint16_t)) == 0,
	      "post-restart-recovery decode reconstructs the exact frame");

	free(base);
	free(f1);
	free(decoded);
	free(wire);
	tpolicy_encoder_destroy(enc);
	tpolicy_decoder_destroy(dec);
}

/* Recovery test: "USB reconnect". Modeled as: decoder state is reset (the
 * gadget/consumer side lost its buffered reference across the physical
 * reconnect) AND the encoder is told about the reconnect so it proactively
 * forces a keyframe for the next frame, exactly as a real transport would
 * need to do (see summary-next-phase.md "production protocol changes").
 */
static void test_usb_reconnect(void)
{
	const uint32_t w = 48, h = 48, interval = 0;
	size_t n = (size_t)w * h;
	tpolicy_encoder *enc = tpolicy_encoder_create(w, h);
	tpolicy_decoder *dec = tpolicy_decoder_create(w, h);
	uint16_t *base = malloc(n * sizeof(uint16_t));
	uint16_t *f1 = malloc(n * sizeof(uint16_t));
	uint16_t *decoded = malloc(n * sizeof(uint16_t));
	uint8_t *wire = malloc(tpolicy_bound(w, h));
	tpolicy_frame_record rec;
	size_t len;
	enum tpolicy_decode_status st;

	fill_random(base, n, 21);
	make_similar_frame(base, f1, w, h, 22);

	len = tpolicy_encode(enc, base, w, h, TPOLICY_FIXED_INTERVAL, interval,
			      0, wire, tpolicy_bound(w, h), &rec);
	tpolicy_decode(dec, rec.frame_type, rec.sequence_number,
		       rec.base_sequence_number, wire, len, decoded, w, h);

	/* USB reconnect: both sides reset/re-synchronize. */
	tpolicy_decoder_reset(dec);
	tpolicy_encoder_force_keyframe(enc);

	len = tpolicy_encode(enc, f1, w, h, TPOLICY_FIXED_INTERVAL, interval,
			      1, wire, tpolicy_bound(w, h), &rec);
	CHECK(rec.frame_type == TPOLICY_KEYFRAME,
	      "encoder sends a keyframe immediately after a modeled USB "
	      "reconnect");
	st = tpolicy_decode(dec, rec.frame_type, rec.sequence_number,
			     rec.base_sequence_number, wire, len, decoded, w,
			     h);
	CHECK(st == TPOLICY_OK, "decoder recovers cleanly after reconnect");
	CHECK(memcmp(decoded, f1, n * sizeof(uint16_t)) == 0,
	      "post-reconnect decode reconstructs the exact frame");

	free(base);
	free(f1);
	free(decoded);
	free(wire);
	tpolicy_encoder_destroy(enc);
	tpolicy_decoder_destroy(dec);
}

/* Recovery test: "Corrupted compressed payload". The decoder must reject
 * a corrupted/truncated LZ4 stream (TPOLICY_ERR_CORRUPT) rather than
 * crash or emit garbage, and must leave its reference/state untouched so
 * a subsequent well-formed keyframe still recovers.
 */
static void test_corrupted_payload(void)
{
	const uint32_t w = 32, h = 32;
	size_t n = (size_t)w * h;
	tpolicy_encoder *enc = tpolicy_encoder_create(w, h);
	tpolicy_decoder *dec = tpolicy_decoder_create(w, h);
	uint16_t *base = malloc(n * sizeof(uint16_t));
	uint16_t *decoded = malloc(n * sizeof(uint16_t));
	uint8_t *wire = malloc(tpolicy_bound(w, h));
	tpolicy_frame_record rec;
	size_t len;
	enum tpolicy_decode_status st;

	fill_random(base, n, 77);

	len = tpolicy_encode(enc, base, w, h, TPOLICY_FIXED_INTERVAL, 0, 0,
			      wire, tpolicy_bound(w, h), &rec);
	CHECK(rec.frame_type == TPOLICY_KEYFRAME, "sanity: frame 0 is a keyframe");

	/* Truncate the compressed payload to guarantee LZ4 rejects it as
	 * malformed/incomplete input.
	 */
	st = tpolicy_decode(dec, rec.frame_type, rec.sequence_number,
			     rec.base_sequence_number, wire, len / 2, decoded,
			     w, h);
	CHECK(st == TPOLICY_ERR_CORRUPT,
	      "truncated/corrupted payload is rejected, not silently decoded");
	CHECK(dec->last_sequence == TPOLICY_NO_SEQ,
	      "decoder state is untouched after a rejected corrupt payload");

	/* A well-formed keyframe afterwards must still work. */
	st = tpolicy_decode(dec, rec.frame_type, rec.sequence_number,
			     rec.base_sequence_number, wire, len, decoded, w,
			     h);
	CHECK(st == TPOLICY_OK,
	      "well-formed keyframe after a rejected corrupt payload decodes");
	CHECK(memcmp(decoded, base, n * sizeof(uint16_t)) == 0,
	      "post-corruption decode reconstructs the exact frame");

	free(base);
	free(decoded);
	free(wire);
	tpolicy_encoder_destroy(enc);
	tpolicy_decoder_destroy(dec);
}

int main(void)
{
	test_fixed_interval_cadence();
	test_adaptive_selection();
	test_forced_keyframe();
	test_lost_delta_frame();
	test_decoder_restart();
	test_usb_reconnect();
	test_corrupted_payload();

	if (g_failures) {
		fprintf(stderr, "test_temporal_policy: %d failure(s)\n",
			g_failures);
		return 1;
	}
	printf("test_temporal_policy: all checks passed\n");
	return 0;
}
