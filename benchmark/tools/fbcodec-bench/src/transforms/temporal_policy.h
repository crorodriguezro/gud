/*
 * temporal_policy.h - keyframe/delta transport policy for PROJECT SPEC
 * next-phase Phase 4 ("Temporal keyframe model") and Phase 8 ("Temporal
 * XOR analysis"), plus the Phase 4 recovery-test primitives.
 *
 * This sits *above* the existing T1 previous-frame XOR primitive
 * (transforms/temporal.c) and adds exactly the pieces that primitive does
 * not have:
 *
 *   - an explicit per-frame wire record: sequence_number, frame_type
 *     (KEYFRAME|DELTA), base_sequence_number (for DELTA: which decoded
 *     frame it was diffed against) and compressed_size;
 *   - decoder-side reference-mismatch detection *by sequence number*
 *     (PROJECT SPEC: "Verify the decoder detects reference mismatch using
 *     sequence numbers. It must not silently display corruption."). The
 *     existing temporal_ctx T1/T2 primitive intentionally documents (see
 *     its own tests) that a dropped update silently desyncs until the
 *     next keyframe -- that is fine as evidence of *why* this layer is
 *     needed, but is not itself a protocol with resync detection;
 *   - a TEMPORAL_ADAPTIVE encode policy: computes both the normal
 *     (keyframe) and delta encodings for every frame and keeps whichever
 *     is smaller, per PROJECT SPEC Phase 4;
 *   - explicit forced-keyframe / decoder-reset / reference-reset support
 *     for the Phase 4 recovery tests (lost delta frame, forced keyframe,
 *     decoder restart, USB reconnect, corrupted payload).
 *
 * Encoding reuses the project's existing RGB565 + LZ4 baseline (for
 * keyframes) and XOR + LZ4 (for deltas) -- no new compression algorithm,
 * only new sequencing/protocol logic around the two already-benchmarked
 * primitives.
 */
#ifndef FBCODEC_TEMPORAL_POLICY_H
#define FBCODEC_TEMPORAL_POLICY_H

#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TPOLICY_NO_SEQ 0xFFFFFFFFu

enum tpolicy_frame_type {
	TPOLICY_KEYFRAME = 0,
	TPOLICY_DELTA = 1,
};

enum tpolicy_mode {
	TPOLICY_FIXED_INTERVAL = 0, /* keyframe_interval==0 => only on
				      * reset/forced keyframe */
	TPOLICY_ADAPTIVE = 1,	     /* encode both candidates, keep smaller */
};

enum tpolicy_decode_status {
	TPOLICY_OK = 0,
	TPOLICY_ERR_REFERENCE_MISMATCH = 1, /* base_sequence_number does not
					      * match the decoder's last
					      * successfully decoded sequence
					      * number -- a delta frame was
					      * lost or reordered */
	TPOLICY_ERR_CORRUPT = 2,	     /* payload failed to
					      * decompress/validate */
	TPOLICY_ERR_NEED_KEYFRAME = 3,	     /* decoder has no reference yet
					      * (fresh/reset) and was handed
					      * a DELTA frame */
};

typedef struct {
	uint32_t sequence_number;
	enum tpolicy_frame_type frame_type;
	uint32_t base_sequence_number; /* valid only for DELTA */
	size_t compressed_size;
	double encode_ns;
	/* Only meaningful for TPOLICY_ADAPTIVE: both candidate sizes, so
	 * the "cost of calculating both" is auditable (PROJECT SPEC Phase
	 * 4 explicitly asks this be visible before considering a
	 * single-pass heuristic).
	 */
	size_t normal_candidate_bytes;
	size_t delta_candidate_bytes;
} tpolicy_frame_record;

typedef struct {
	uint32_t width, height;
	uint16_t *reference;	 /* last frame handed to the encoder */
	uint32_t last_sequence;	 /* sequence number `reference` corresponds
				  * to, or TPOLICY_NO_SEQ if none yet */
	bool force_keyframe_next;
} tpolicy_encoder;

typedef struct {
	uint32_t width, height;
	uint16_t *reference;	 /* last successfully decoded frame */
	uint32_t last_sequence;	 /* TPOLICY_NO_SEQ if the decoder currently
				  * has no valid reference (fresh or reset) */
} tpolicy_decoder;

tpolicy_encoder *tpolicy_encoder_create(uint32_t width, uint32_t height);
void tpolicy_encoder_destroy(tpolicy_encoder *enc);
/* The frame encoded immediately after this call is forced to be a
 * KEYFRAME regardless of the configured interval/adaptive policy
 * (recovery test primitive: "forced keyframe").
 */
void tpolicy_encoder_force_keyframe(tpolicy_encoder *enc);

tpolicy_decoder *tpolicy_decoder_create(uint32_t width, uint32_t height);
void tpolicy_decoder_destroy(tpolicy_decoder *dec);
/* Models a decoder restart / USB reconnect: all decoder state is
 * discarded; the decoder will reject any DELTA frame with
 * TPOLICY_ERR_NEED_KEYFRAME until the next KEYFRAME arrives.
 */
void tpolicy_decoder_reset(tpolicy_decoder *dec);

size_t tpolicy_bound(uint32_t width, uint32_t height);

/* Encodes one frame per `mode`/`keyframe_interval`, writes the wire
 * payload (which the caller is responsible for "transmitting", i.e.
 * feeding to tpolicy_decode -- or intentionally dropping/corrupting it to
 * exercise recovery) to `dst` (capacity `dst_cap`, see tpolicy_bound()),
 * and fills `rec` with the frame's metadata. Returns the wire payload
 * length, or (size_t)-1 on error.
 */
size_t tpolicy_encode(tpolicy_encoder *enc, const uint16_t *src, uint32_t w,
		       uint32_t h, enum tpolicy_mode mode,
		       uint32_t keyframe_interval, uint32_t sequence_number,
		       uint8_t *dst, size_t dst_cap,
		       tpolicy_frame_record *rec);

/* Decodes one wire payload (as produced by tpolicy_encode, or a
 * deliberately corrupted/truncated copy of one for recovery testing).
 * `frame_type`/`base_sequence_number`/`sequence_number` are the record
 * fields the transport would carry out-of-band (in the real GUD/xdisp
 * transport these would be a small frame header rather than a side
 * channel; PROJECT SPEC Phase 9 discusses what the production protocol
 * would need). On TPOLICY_OK, `dst` holds the reconstructed frame and the
 * decoder's reference/last_sequence are updated. On any error, decoder
 * state is left untouched (does not silently corrupt the reference).
 */
enum tpolicy_decode_status tpolicy_decode(tpolicy_decoder *dec,
					   enum tpolicy_frame_type frame_type,
					   uint32_t sequence_number,
					   uint32_t base_sequence_number,
					   const uint8_t *src, size_t src_len,
					   uint16_t *dst, uint32_t w,
					   uint32_t h);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_TEMPORAL_POLICY_H */
