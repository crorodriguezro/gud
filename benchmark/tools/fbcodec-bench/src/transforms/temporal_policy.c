#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "temporal_policy.h"
#include "../lz4_util.h"

static double now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

tpolicy_encoder *tpolicy_encoder_create(uint32_t width, uint32_t height)
{
	tpolicy_encoder *enc = calloc(1, sizeof(*enc));

	if (!enc)
		return NULL;
	enc->width = width;
	enc->height = height;
	enc->reference = calloc(rgb565_pixel_count(width, height),
				 sizeof(uint16_t));
	if (!enc->reference) {
		free(enc);
		return NULL;
	}
	enc->last_sequence = TPOLICY_NO_SEQ;
	enc->force_keyframe_next = false;
	return enc;
}

void tpolicy_encoder_destroy(tpolicy_encoder *enc)
{
	if (!enc)
		return;
	free(enc->reference);
	free(enc);
}

void tpolicy_encoder_force_keyframe(tpolicy_encoder *enc)
{
	enc->force_keyframe_next = true;
}

tpolicy_decoder *tpolicy_decoder_create(uint32_t width, uint32_t height)
{
	tpolicy_decoder *dec = calloc(1, sizeof(*dec));

	if (!dec)
		return NULL;
	dec->width = width;
	dec->height = height;
	dec->reference = calloc(rgb565_pixel_count(width, height),
				 sizeof(uint16_t));
	if (!dec->reference) {
		free(dec);
		return NULL;
	}
	dec->last_sequence = TPOLICY_NO_SEQ;
	return dec;
}

void tpolicy_decoder_destroy(tpolicy_decoder *dec)
{
	if (!dec)
		return;
	free(dec->reference);
	free(dec);
}

void tpolicy_decoder_reset(tpolicy_decoder *dec)
{
	dec->last_sequence = TPOLICY_NO_SEQ;
	memset(dec->reference, 0,
	       rgb565_pixel_count(dec->width, dec->height) * sizeof(uint16_t));
}

size_t tpolicy_bound(uint32_t width, uint32_t height)
{
	return lz4u_compress_bound(rgb565_byte_size(width, height));
}

size_t tpolicy_encode(tpolicy_encoder *enc, const uint16_t *src, uint32_t w,
		       uint32_t h, enum tpolicy_mode mode,
		       uint32_t keyframe_interval, uint32_t sequence_number,
		       uint8_t *dst, size_t dst_cap,
		       tpolicy_frame_record *rec)
{
	size_t n = rgb565_pixel_count(w, h);
	size_t nbytes = n * sizeof(uint16_t);
	uint16_t *delta = NULL;
	uint8_t *normal_buf = NULL, *delta_buf = NULL;
	size_t normal_bytes = (size_t)-1, delta_bytes = (size_t)-1;
	bool have_reference = enc->last_sequence != TPOLICY_NO_SEQ;
	bool want_keyframe;
	double t0, t1;
	size_t written;

	memset(rec, 0, sizeof(*rec));
	rec->sequence_number = sequence_number;

	if (enc->force_keyframe_next) {
		want_keyframe = true;
		enc->force_keyframe_next = false;
	} else if (!have_reference) {
		want_keyframe = true;
	} else if (mode == TPOLICY_ADAPTIVE) {
		/* Encode both candidates into scratch buffers and keep
		 * whichever is smaller (PROJECT SPEC Phase 4
		 * TEMPORAL_ADAPTIVE algorithm). This deliberately pays for
		 * both compressions every frame -- `rec->encode_ns` is the
		 * sum of both, i.e. the real "cost of calculating both"; see
		 * the module header and summary-next-phase.md for the
		 * "avoid double compression" follow-up discussion.
		 */
		size_t i;

		normal_buf = malloc(dst_cap);
		delta_buf = malloc(dst_cap);
		delta = malloc(nbytes);
		if (!normal_buf || !delta_buf || !delta) {
			free(normal_buf);
			free(delta_buf);
			free(delta);
			return (size_t)-1;
		}

		t0 = now_ns();
		normal_bytes = lz4u_compress(src, nbytes, normal_buf, dst_cap);
		t1 = now_ns();
		rec->encode_ns = t1 - t0;

		for (i = 0; i < n; i++)
			delta[i] = (uint16_t)(src[i] ^ enc->reference[i]);

		t0 = now_ns();
		delta_bytes = lz4u_compress(delta, nbytes, delta_buf, dst_cap);
		t1 = now_ns();
		rec->encode_ns += (t1 - t0);

		rec->normal_candidate_bytes = normal_bytes;
		rec->delta_candidate_bytes = delta_bytes;
		want_keyframe = !(delta_bytes != (size_t)-1 &&
				   (normal_bytes == (size_t)-1 ||
				    delta_bytes < normal_bytes));
	} else {
		/* TPOLICY_FIXED_INTERVAL: keyframe_interval==0 means "only
		 * on reset/forced keyframe" (never automatic); otherwise
		 * every Nth frame (by absolute sequence number, so cadence
		 * is stable even across forced keyframes).
		 */
		want_keyframe = keyframe_interval != 0 &&
				(sequence_number % keyframe_interval) == 0;
	}

	if (want_keyframe) {
		if (normal_buf) {
			/* Already computed above for the adaptive case. */
			written = normal_bytes;
			memcpy(dst, normal_buf, written);
		} else {
			t0 = now_ns();
			written = lz4u_compress(src, nbytes, dst, dst_cap);
			t1 = now_ns();
			rec->encode_ns = t1 - t0;
		}
		if (written == (size_t)-1) {
			free(normal_buf);
			free(delta_buf);
			free(delta);
			return (size_t)-1;
		}
		rec->frame_type = TPOLICY_KEYFRAME;
		rec->base_sequence_number = TPOLICY_NO_SEQ;
	} else {
		if (delta_buf) {
			/* Already computed above for the adaptive case. */
			written = delta_bytes;
			memcpy(dst, delta_buf, written);
		} else {
			size_t i;

			delta = malloc(nbytes);
			if (!delta) {
				free(normal_buf);
				free(delta_buf);
				return (size_t)-1;
			}
			for (i = 0; i < n; i++)
				delta[i] = (uint16_t)(src[i] ^ enc->reference[i]);
			t0 = now_ns();
			written = lz4u_compress(delta, nbytes, dst, dst_cap);
			t1 = now_ns();
			rec->encode_ns = t1 - t0;
		}
		if (written == (size_t)-1) {
			free(normal_buf);
			free(delta_buf);
			free(delta);
			return (size_t)-1;
		}
		rec->frame_type = TPOLICY_DELTA;
		rec->base_sequence_number = enc->last_sequence;
	}

	rec->compressed_size = written;
	memcpy(enc->reference, src, nbytes);
	enc->last_sequence = sequence_number;

	free(normal_buf);
	free(delta_buf);
	free(delta);
	return written;
}

enum tpolicy_decode_status tpolicy_decode(tpolicy_decoder *dec,
					   enum tpolicy_frame_type frame_type,
					   uint32_t sequence_number,
					   uint32_t base_sequence_number,
					   const uint8_t *src, size_t src_len,
					   uint16_t *dst, uint32_t w,
					   uint32_t h)
{
	size_t n = rgb565_pixel_count(w, h);
	size_t nbytes = n * sizeof(uint16_t);
	size_t out_len = 0;

	if (frame_type == TPOLICY_DELTA) {
		if (dec->last_sequence == TPOLICY_NO_SEQ)
			return TPOLICY_ERR_NEED_KEYFRAME;
		if (dec->last_sequence != base_sequence_number)
			return TPOLICY_ERR_REFERENCE_MISMATCH;
	}

	if (lz4u_decompress(src, src_len, dst, nbytes, &out_len) != 0 ||
	    out_len != nbytes)
		return TPOLICY_ERR_CORRUPT;

	if (frame_type == TPOLICY_DELTA) {
		size_t i;

		for (i = 0; i < n; i++)
			dst[i] = (uint16_t)(dst[i] ^ dec->reference[i]);
	}

	memcpy(dec->reference, dst, nbytes);
	dec->last_sequence = sequence_number;
	return TPOLICY_OK;
}
