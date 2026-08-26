/*
 * capture.h - simple binary damage-rectangle capture/replay format
 * (PROJECT SPEC section 13) plus helpers to read plain multi-frame raw
 * RGB565 files (used for the real xdisp-motion-1280x720-30fps-10s.rgb565le
 * asset already present in this repository under
 * backport-4.9/env/local/assets/).
 *
 * File layout (little-endian throughout):
 *   char     magic[4]      = "FBCB"
 *   uint32_t version       = 1
 *   uint32_t width
 *   uint32_t height
 *   uint32_t format        = 0 (RGB565LE)
 *   uint32_t event_count
 *   repeated event_count times:
 *     uint64_t timestamp_ns   (relative to stream start)
 *     uint32_t frame_number
 *     uint32_t x, y, w, h
 *     uint32_t stride_bytes   (= w * 2 for RGB565)
 *     uint32_t payload_len    (= h * stride_bytes)
 *     uint8_t  payload[payload_len]
 *
 * This is NOT a real Lomiri/Mir compositor damage-event capture (no such
 * instrumentation exists in this environment) -- see summary.md's
 * "Corpus" section for the explicit MEASURED vs MODELED/ESTIMATED
 * distinction. It is, however, a real, non-synthetic pixel-data source:
 * capture_make_damage_stream_from_frames() derives realistic damage
 * rectangles by diffing consecutive frames of the real captured motion
 * video asset on aligned tiles, which is a reasonable proxy for a damage-
 * rectangle transport while that instrumentation is unavailable.
 */
#ifndef FBCODEC_CAPTURE_H
#define FBCODEC_CAPTURE_H

#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint64_t timestamp_ns;
	uint32_t frame_number;
	uint32_t x, y, w, h;
	uint16_t *pixels; /* w*h, tightly packed, owned */
} damage_event;

typedef struct {
	uint32_t width;
	uint32_t height;
	size_t count;
	damage_event *events;
} damage_stream;

void damage_stream_free(damage_stream *ds);

/* Reads a raw multi-frame RGB565LE file (no header, just concatenated
 * width*height*2-byte frames). Returns the number of complete frames
 * available, or 0 on error. `max_frames` limits how many are read.
 */
size_t capture_read_raw_frames(const char *path, uint32_t width,
				uint32_t height, size_t max_frames,
				rgb565_buffer *out_frames);

/* Builds a synthetic damage-rectangle stream by tiling each frame into
 * `tile` x `tile` blocks and emitting one damage_event per changed tile
 * per frame transition (plus a full-frame keyframe event for frame 0).
 * frame_interval_ns spaces consecutive input frames in time.
 */
bool capture_make_damage_stream_from_frames(const rgb565_buffer *frames,
					      size_t frame_count,
					      uint32_t tile,
					      uint64_t frame_interval_ns,
					      damage_stream *out);

bool capture_write_fbcb(const char *path, const damage_stream *ds);
bool capture_read_fbcb(const char *path, damage_stream *out);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_CAPTURE_H */
