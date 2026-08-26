/*
 * common.h - shared types and helpers for the fbcodec-bench harness.
 *
 * All pixel buffers used by this tool are packed RGB565, little-endian,
 * row-major, with a stride of (width * 2) bytes -- i.e. no padding between
 * rows. This matches the wire format produced by the GUD RGB565 transport
 * (see backport-4.9/gud_frame_layout.h) closely enough for benchmarking
 * purposes while keeping every transform bounds-safe and easy to reason
 * about.
 */
#ifndef FBCODEC_COMMON_H
#define FBCODEC_COMMON_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FBCODEC_VERSION "1.0.0"

/* A rectangular RGB565 pixel buffer that owns its storage. */
typedef struct {
	uint32_t width;
	uint32_t height;
	uint16_t *pixels; /* width * height entries, row-major, no padding */
} rgb565_buffer;

static inline size_t rgb565_pixel_count(uint32_t width, uint32_t height)
{
	return (size_t)width * (size_t)height;
}

static inline size_t rgb565_byte_size(uint32_t width, uint32_t height)
{
	return rgb565_pixel_count(width, height) * sizeof(uint16_t);
}

/* Allocates a zero-initialized buffer. Returns false on allocation failure. */
bool rgb565_buffer_alloc(rgb565_buffer *buf, uint32_t width, uint32_t height);
void rgb565_buffer_free(rgb565_buffer *buf);

/* Monotonic nanosecond clock, used for all encode/decode timings. */
static inline uint64_t fbcodec_now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* RGB565 channel extraction/packing. Bit layout: RRRRR GGGGGG BBBBB. */
static inline void rgb565_unpack(uint16_t pixel, uint8_t *r5, uint8_t *g6,
				  uint8_t *b5)
{
	*r5 = (uint8_t)((pixel >> 11) & 0x1F);
	*g6 = (uint8_t)((pixel >> 5) & 0x3F);
	*b5 = (uint8_t)(pixel & 0x1F);
}

static inline uint16_t rgb565_pack(uint8_t r5, uint8_t g6, uint8_t b5)
{
	return (uint16_t)(((uint32_t)(r5 & 0x1F) << 11) |
			   ((uint32_t)(g6 & 0x3F) << 5) |
			   (uint32_t)(b5 & 0x1F));
}

/* Expands a 5/6/5 bit channel value to 8 bits by left-shifting and
 * replicating the high bits into the vacated low bits (standard, low-bias
 * free up-sampling used by most RGB565 -> RGB888 conversions).
 */
static inline uint8_t expand5(uint8_t v5)
{
	return (uint8_t)((v5 << 3) | (v5 >> 2));
}

static inline uint8_t expand6(uint8_t v6)
{
	return (uint8_t)((v6 << 2) | (v6 >> 4));
}

static inline void rgb565_to_rgb888(uint16_t pixel, uint8_t *r, uint8_t *g,
				     uint8_t *b)
{
	uint8_t r5, g6, b5;

	rgb565_unpack(pixel, &r5, &g6, &b5);
	*r = expand5(r5);
	*g = expand6(g6);
	*b = expand5(b5);
}

static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
	return rgb565_pack((uint8_t)(r >> 3), (uint8_t)(g >> 2),
			    (uint8_t)(b >> 3));
}

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_COMMON_H */
