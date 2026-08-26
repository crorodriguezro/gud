/*
 * test_temporal.c - temporal codec validation scenarios (PROJECT SPEC
 * section 25): normal sequence, dropped update, reordered update, forced
 * decoder reset, new keyframe/full refresh, and reconnect behavior.
 *
 * Encoder and decoder always use *independent* temporal_ctx instances,
 * exactly like a real sender/receiver pair (see main.c's
 * run_codec_on_sequence for the same rule and why sharing one context
 * would hide drift bugs).
 *
 * Scoping note: this benchmark's temporal codecs operate on whole frames
 * with a single persistent reference buffer (see transforms/temporal.h).
 * "damage rectangle overlapping previous updates" from section 25 item 6
 * therefore does not apply in the same form here -- exercising that
 * scenario faithfully would require per-region reference tracking across
 * arbitrary damage rectangles, which is exactly the kind of "fragile
 * persistent state" complexity flagged in PROJECT SPEC sections 25/45/46
 * and is out of scope for this harness. This is called out explicitly
 * (not silently skipped) in summary.md's temporal findings section.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/transforms/quant.h"
#include "../src/transforms/temporal.h"

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

static bool expected_target(enum temporal_mode mode, const uint16_t *src,
			      uint16_t *out, size_t n)
{
	if (mode == TEMPORAL_MODE_QUANT_XOR) {
		quant_mild_fwd(src, (uint32_t)n, 1, (uint8_t *)out);
		return true;
	}
	memcpy(out, src, n * sizeof(uint16_t));
	return true;
}

/* Normal sequence: every frame delivered in order -> must match exactly
 * (or match the lossy target for T3).
 */
static void test_normal_sequence(enum temporal_mode mode, const char *label)
{
	uint32_t w = 64, h = 64;
	size_t n = (size_t)w * h;
	temporal_ctx *enc = temporal_create(w, h, mode, 8);
	temporal_ctx *dec = temporal_create(w, h, mode, 8);
	uint8_t *packet = malloc(temporal_bound(w, h));
	uint16_t *frame = malloc(n * sizeof(uint16_t));
	uint16_t *decoded = malloc(n * sizeof(uint16_t));
	uint16_t *expect = malloc(n * sizeof(uint16_t));
	int f;

	for (f = 0; f < 20; f++) {
		size_t plen;
		char msg[128];

		fill_random(frame, n, 1000 + (uint32_t)f);
		plen = temporal_encode(enc, frame, w, h, packet,
					 temporal_bound(w, h));
		CHECK(plen != (size_t)-1, "temporal_encode failed");
		CHECK(temporal_decode(dec, packet, plen, decoded, w, h) == 0,
		      "temporal_decode failed");

		expected_target(mode, frame, expect, n);
		snprintf(msg, sizeof(msg),
			 "%s: normal sequence mismatch at frame %d", label,
			 f);
		CHECK(memcmp(expect, decoded, n * sizeof(uint16_t)) == 0, msg);
	}

	free(packet);
	free(frame);
	free(decoded);
	free(expect);
	temporal_destroy(enc);
	temporal_destroy(dec);
}

/* Dropped update: the decoder never receives frame k. This documents (not
 * "fixes") the expected fragility: subsequent frames decode incorrectly
 * until an explicit reset + keyframe resynchronizes both sides.
 */
static void test_dropped_update_then_recovers(enum temporal_mode mode,
					       const char *label)
{
	uint32_t w = 32, h = 32;
	size_t n = (size_t)w * h;
	temporal_ctx *enc = temporal_create(w, h, mode, 0 /* manual keyframes */);
	temporal_ctx *dec = temporal_create(w, h, mode, 0);
	uint8_t *packet = malloc(temporal_bound(w, h));
	uint16_t *frame = malloc(n * sizeof(uint16_t));
	uint16_t *decoded = malloc(n * sizeof(uint16_t));
	uint16_t *expect = malloc(n * sizeof(uint16_t));
	size_t plen;
	char msg[160];

	/* Frame 0: keyframe, both sides in sync. */
	fill_random(frame, n, 1);
	plen = temporal_encode(enc, frame, w, h, packet, temporal_bound(w, h));
	temporal_decode(dec, packet, plen, decoded, w, h);
	expected_target(mode, frame, expect, n);
	CHECK(memcmp(expect, decoded, n * sizeof(uint16_t)) == 0,
	      "keyframe 0 should match");

	/* Frame 1: encoder advances its reference; decoder never sees the
	 * packet (dropped on the wire) so its reference silently goes
	 * stale relative to the encoder's.
	 */
	fill_random(frame, n, 2);
	plen = temporal_encode(enc, frame, w, h, packet, temporal_bound(w, h));
	(void)plen; /* intentionally not delivered to the decoder */

	/* Frame 2: encoder deltas against frame 1's reference; the decoder
	 * (still holding frame 0's reference) is now desynchronized and is
	 * expected to reconstruct garbage, not frame 2.
	 */
	fill_random(frame, n, 3);
	plen = temporal_encode(enc, frame, w, h, packet, temporal_bound(w, h));
	temporal_decode(dec, packet, plen, decoded, w, h);
	expected_target(mode, frame, expect, n);
	snprintf(msg, sizeof(msg),
		 "%s: decoder should NOT silently match after a dropped "
		 "update (this documents fragility, not a bug)",
		 label);
	CHECK(memcmp(expect, decoded, n * sizeof(uint16_t)) != 0, msg);

	/* Recovery: force both sides back to a keyframe (equivalent to a
	 * reconnect / explicit resync signal in a real transport).
	 */
	temporal_reset(enc);
	temporal_reset(dec);
	fill_random(frame, n, 4);
	plen = temporal_encode(enc, frame, w, h, packet, temporal_bound(w, h));
	temporal_decode(dec, packet, plen, decoded, w, h);
	expected_target(mode, frame, expect, n);
	snprintf(msg, sizeof(msg),
		 "%s: reset + keyframe must resynchronize encoder/decoder",
		 label);
	CHECK(memcmp(expect, decoded, n * sizeof(uint16_t)) == 0, msg);

	free(packet);
	free(frame);
	free(decoded);
	free(expect);
	temporal_destroy(enc);
	temporal_destroy(dec);
}

/* Reordered update: deliver two encoded packets to the decoder in the
 * wrong order. Documents that reordering corrupts reconstruction (no
 * sequence numbering/reordering buffer exists in this simple design) and
 * that a reset recovers.
 */
static void test_reordered_update(enum temporal_mode mode, const char *label)
{
	uint32_t w = 32, h = 32;
	size_t n = (size_t)w * h;
	temporal_ctx *enc = temporal_create(w, h, mode, 0);
	temporal_ctx *dec = temporal_create(w, h, mode, 0);
	uint8_t *packet_a = malloc(temporal_bound(w, h));
	uint8_t *packet_b = malloc(temporal_bound(w, h));
	uint16_t *frame_a = malloc(n * sizeof(uint16_t));
	uint16_t *frame_b = malloc(n * sizeof(uint16_t));
	uint16_t *decoded = malloc(n * sizeof(uint16_t));
	uint16_t *expect = malloc(n * sizeof(uint16_t));
	size_t len_a, len_b;
	char msg[160];

	fill_random(frame_a, n, 10);
	len_a = temporal_encode(enc, frame_a, w, h, packet_a,
				  temporal_bound(w, h));
	temporal_decode(dec, packet_a, len_a, decoded, w, h); /* keyframe, in sync */

	fill_random(frame_b, n, 11);
	len_b = temporal_encode(enc, frame_b, w, h, packet_b,
				  temporal_bound(w, h));

	fill_random(frame_a, n, 12); /* reuse frame_a storage for frame "C" */
	len_a = temporal_encode(enc, frame_a, w, h, packet_a,
				  temporal_bound(w, h));

	/* Deliver C before B (reordered). */
	temporal_decode(dec, packet_a, len_a, decoded, w, h);
	expected_target(mode, frame_a, expect, n);
	snprintf(msg, sizeof(msg),
		 "%s: reordered delivery must not silently produce the "
		 "correct frame",
		 label);
	CHECK(memcmp(expect, decoded, n * sizeof(uint16_t)) != 0, msg);

	temporal_reset(enc);
	temporal_reset(dec);
	fill_random(frame_a, n, 13);
	len_a = temporal_encode(enc, frame_a, w, h, packet_a,
				  temporal_bound(w, h));
	temporal_decode(dec, packet_a, len_a, decoded, w, h);
	expected_target(mode, frame_a, expect, n);
	CHECK(memcmp(expect, decoded, n * sizeof(uint16_t)) == 0,
	      "reset after reordering must resynchronize");

	(void)len_b;
	free(packet_a);
	free(packet_b);
	free(frame_a);
	free(frame_b);
	free(decoded);
	free(expect);
	temporal_destroy(enc);
	temporal_destroy(dec);
}

/* Automatic periodic keyframe: with keyframe_interval=4, frame 4k must be
 * self-contained (decodable even if the reference were wrong), which we
 * verify by deliberately corrupting the decoder's reference beforehand.
 */
static void test_periodic_keyframe_recovers(void)
{
	uint32_t w = 16, h = 16;
	size_t n = (size_t)w * h;
	temporal_ctx *enc = temporal_create(w, h, TEMPORAL_MODE_XOR, 4);
	temporal_ctx *dec = temporal_create(w, h, TEMPORAL_MODE_XOR, 4);
	uint8_t *packet = malloc(temporal_bound(w, h));
	uint16_t *frame = malloc(n * sizeof(uint16_t));
	uint16_t *decoded = malloc(n * sizeof(uint16_t));
	int f;

	for (f = 0; f < 4; f++) {
		size_t plen;

		fill_random(frame, n, (uint32_t)(100 + f));
		plen = temporal_encode(enc, frame, w, h, packet,
					 temporal_bound(w, h));
		if (f == 2) {
			/* Simulate the decoder having gone out of sync
			 * (e.g. after a missed update elsewhere): corrupt
			 * its reference directly.
			 */
			memset(dec->reference, 0xAA, n * sizeof(uint16_t));
			continue; /* frame 2 itself is also "dropped" */
		}
		temporal_decode(dec, packet, plen, decoded, w, h);
	}

	/* Frame 4 (index 4) is due for an automatic keyframe
	 * (keyframe_interval=4), so it must decode correctly even though
	 * the decoder's reference was corrupted and frame 2 was dropped.
	 */
	fill_random(frame, n, 200);
	{
		size_t plen = temporal_encode(enc, frame, w, h, packet,
						temporal_bound(w, h));

		CHECK(temporal_decode(dec, packet, plen, decoded, w, h) == 0,
		      "keyframe decode failed");
		CHECK(memcmp(frame, decoded, n * sizeof(uint16_t)) == 0,
		      "periodic keyframe must recover sync regardless of "
		      "prior decoder corruption");
	}

	free(packet);
	free(frame);
	free(decoded);
	temporal_destroy(enc);
	temporal_destroy(dec);
}

int main(void)
{
	test_normal_sequence(TEMPORAL_MODE_XOR, "T1 prev-XOR");
	test_normal_sequence(TEMPORAL_MODE_SUB, "T2 prev-SUB");
	test_normal_sequence(TEMPORAL_MODE_QUANT_XOR, "T3 quant-XOR");

	test_dropped_update_then_recovers(TEMPORAL_MODE_XOR, "T1 prev-XOR");
	test_dropped_update_then_recovers(TEMPORAL_MODE_SUB, "T2 prev-SUB");
	test_dropped_update_then_recovers(TEMPORAL_MODE_QUANT_XOR,
					   "T3 quant-XOR");

	test_reordered_update(TEMPORAL_MODE_XOR, "T1 prev-XOR");
	test_reordered_update(TEMPORAL_MODE_SUB, "T2 prev-SUB");

	test_periodic_keyframe_recovers();

	if (g_failures) {
		fprintf(stderr, "\n%d test_temporal FAILURE(S)\n", g_failures);
		return 1;
	}
	printf("test_temporal: all checks passed\n");
	return 0;
}
