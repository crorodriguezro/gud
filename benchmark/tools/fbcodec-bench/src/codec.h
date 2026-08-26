/*
 * codec.h - common codec abstraction used by every benchmark candidate.
 *
 * Design goals (see PROJECT SPEC section 50):
 *  - one small vtable, no unnecessary layering
 *  - reusable buffers on the hot path (bound() lets callers size dst once)
 *  - explicit stateful vs. stateless codecs, so temporal candidates do not
 *    hide persistent state behind a "simple" interface
 *  - metadata needed for the final ranking tables (lossy?, stateful?,
 *    native RGB565?, qualitative implementation complexity)
 */
#ifndef FBCODEC_CODEC_H
#define FBCODEC_CODEC_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fbcodec_desc fbcodec_desc;

struct fbcodec_desc {
	const char *name;       /* stable machine-readable id, e.g. "u16-xor-lz4" */
	const char *label;      /* human-readable label for reports */
	const char *category;   /* "baseline" | "spatial" | "quant" | "combo" |
				  * "temporal" | "shuffle" | "rle" | "external"
				  */
	bool is_lossy;
	bool is_stateful;       /* needs persistent reference state (temporal) */
	bool native_rgb565;     /* false => needs RGB565<->RGB888 conversion */
	const char *complexity; /* "low" | "medium" | "high" (qualitative) */

	/* Stateful codecs: allocate/free/reset persistent per-stream state.
	 * Stateless codecs may leave these NULL.
	 */
	void *(*create)(uint32_t width, uint32_t height);
	void (*destroy)(void *ctx);
	void (*reset)(void *ctx); /* force a keyframe / drop reference state */

	/* Upper bound (bytes) on the encoded size for a width x height frame.
	 * Must be safe to use as the destination buffer capacity.
	 */
	size_t (*bound)(uint32_t width, uint32_t height);

	/* Encodes src (width*height RGB565 pixels, tightly packed) into dst
	 * (capacity dst_cap, guaranteed >= bound()). Returns the encoded
	 * length in bytes, or (size_t)-1 on failure.
	 */
	size_t (*encode)(void *ctx, const uint16_t *src, uint32_t width,
			  uint32_t height, uint8_t *dst, size_t dst_cap);

	/* Decodes src (src_len bytes) into dst (width*height RGB565 pixels,
	 * pre-allocated by the caller). Returns 0 on success, nonzero on
	 * failure.
	 */
	int (*decode)(void *ctx, const uint8_t *src, size_t src_len,
		      uint16_t *dst, uint32_t width, uint32_t height);
};

#define FBCODEC_MAX_CODECS 64

/* Returns the full static registry (stable order). */
size_t fbcodec_registry(const fbcodec_desc **out, size_t max);

/* Looks up a codec by name; returns NULL if not found. */
const fbcodec_desc *fbcodec_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_CODEC_H */
