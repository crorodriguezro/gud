/*
 * synthetic.h - synthetic RGB565 corpus generator (PROJECT SPEC section 14).
 *
 * Deterministic (seeded xorshift32 for noise) so every benchmark run is
 * reproducible. Covers: flat colors, checkerboards, gradients (h/v/2d),
 * text-like high-contrast edges, random noise, and simple photographic-like
 * synthetic content (smooth colored blobs) for when no real photograph is
 * supplied. Sequence generators cover scrolling, small local changes and
 * fullscreen animation for temporal-prediction experiments.
 */
#ifndef FBCODEC_SYNTHETIC_H
#define FBCODEC_SYNTHETIC_H

#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Single-frame patterns. */
bool synth_generate(const char *pattern, uint32_t w, uint32_t h,
		     uint32_t seed, rgb565_buffer *out);

/* Returns the built-in single-frame pattern name list. */
size_t synth_pattern_names(const char **out, size_t max);

/* Multi-frame sequence patterns: "static", "scroll-h", "scroll-v",
 * "small-changes", "fullscreen-anim". Allocates `frame_count` buffers
 * (caller frees each with rgb565_buffer_free and frees the array itself).
 */
bool synth_generate_sequence(const char *pattern, uint32_t w, uint32_t h,
			      uint32_t seed, uint32_t frame_count,
			      rgb565_buffer *out_frames);

size_t synth_sequence_names(const char **out, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_SYNTHETIC_H */
