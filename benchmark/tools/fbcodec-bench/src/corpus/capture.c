#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capture.h"

void damage_stream_free(damage_stream *ds)
{
	size_t i;

	if (!ds || !ds->events)
		return;
	for (i = 0; i < ds->count; i++)
		free(ds->events[i].pixels);
	free(ds->events);
	ds->events = NULL;
	ds->count = 0;
}

size_t capture_read_raw_frames(const char *path, uint32_t width,
				uint32_t height, size_t max_frames,
				rgb565_buffer *out_frames)
{
	FILE *f = fopen(path, "rb");
	size_t frame_bytes = rgb565_byte_size(width, height);
	size_t i;

	if (!f)
		return 0;
	for (i = 0; i < max_frames; i++) {
		if (!rgb565_buffer_alloc(&out_frames[i], width, height)) {
			fclose(f);
			return i;
		}
		if (fread(out_frames[i].pixels, 1, frame_bytes, f) !=
		    frame_bytes) {
			rgb565_buffer_free(&out_frames[i]);
			break;
		}
	}
	fclose(f);
	return i;
}

static uint32_t tiles_across(uint32_t dim, uint32_t tile)
{
	return (dim + tile - 1) / tile;
}

bool capture_make_damage_stream_from_frames(const rgb565_buffer *frames,
					      size_t frame_count,
					      uint32_t tile,
					      uint64_t frame_interval_ns,
					      damage_stream *out)
{
	uint32_t w, h, tiles_x, tiles_y;
	size_t max_events, ev_idx = 0;
	size_t f;

	if (frame_count == 0)
		return false;
	w = frames[0].width;
	h = frames[0].height;
	tiles_x = tiles_across(w, tile);
	tiles_y = tiles_across(h, tile);
	max_events = frame_count * (size_t)tiles_x * (size_t)tiles_y;

	out->width = w;
	out->height = h;
	out->events = calloc(max_events, sizeof(damage_event));
	if (!out->events)
		return false;

	for (f = 0; f < frame_count; f++) {
		uint32_t ty;

		for (ty = 0; ty < tiles_y; ty++) {
			uint32_t tx;

			for (tx = 0; tx < tiles_x; tx++) {
				uint32_t x0 = tx * tile, y0 = ty * tile;
				uint32_t tw = (x0 + tile <= w) ? tile
								: (w - x0);
				uint32_t th = (y0 + tile <= h) ? tile
								: (h - y0);
				bool changed = (f == 0);
				uint32_t yy;

				if (!changed) {
					for (yy = y0; yy < y0 + th && !changed;
					     yy++) {
						size_t off =
							(size_t)yy * w + x0;

						if (memcmp(frames[f].pixels +
								   off,
							   frames[f - 1]
								   .pixels +
								   off,
							   (size_t)tw *
								   sizeof(uint16_t)) !=
						    0)
							changed = true;
					}
				}
				if (!changed)
					continue;

				{
					damage_event *e = &out->events[ev_idx];

					e->timestamp_ns =
						f * frame_interval_ns;
					e->frame_number = (uint32_t)f;
					e->x = x0;
					e->y = y0;
					e->w = tw;
					e->h = th;
					e->pixels = malloc(
						rgb565_byte_size(tw, th));
					if (!e->pixels) {
						out->count = ev_idx;
						damage_stream_free(out);
						return false;
					}
					for (yy = 0; yy < th; yy++) {
						memcpy(e->pixels +
							       (size_t)yy * tw,
						       frames[f].pixels +
							       (size_t)(y0 + yy) *
									       w +
							       x0,
						       (size_t)tw *
							       sizeof(uint16_t));
					}
					ev_idx++;
				}
			}
		}
	}
	out->count = ev_idx;
	return true;
}

#define FBCB_MAGIC "FBCB"

bool capture_write_fbcb(const char *path, const damage_stream *ds)
{
	FILE *f = fopen(path, "wb");
	uint32_t version = 1, format = 0, count32;
	size_t i;

	if (!f)
		return false;
	if (ds->count > 0xFFFFFFFFu) {
		fclose(f);
		return false;
	}
	count32 = (uint32_t)ds->count;

	fwrite(FBCB_MAGIC, 1, 4, f);
	fwrite(&version, sizeof(version), 1, f);
	fwrite(&ds->width, sizeof(ds->width), 1, f);
	fwrite(&ds->height, sizeof(ds->height), 1, f);
	fwrite(&format, sizeof(format), 1, f);
	fwrite(&count32, sizeof(count32), 1, f);

	for (i = 0; i < ds->count; i++) {
		const damage_event *e = &ds->events[i];
		uint32_t stride = e->w * 2;
		uint32_t payload_len = e->h * stride;

		fwrite(&e->timestamp_ns, sizeof(e->timestamp_ns), 1, f);
		fwrite(&e->frame_number, sizeof(e->frame_number), 1, f);
		fwrite(&e->x, sizeof(e->x), 1, f);
		fwrite(&e->y, sizeof(e->y), 1, f);
		fwrite(&e->w, sizeof(e->w), 1, f);
		fwrite(&e->h, sizeof(e->h), 1, f);
		fwrite(&stride, sizeof(stride), 1, f);
		fwrite(&payload_len, sizeof(payload_len), 1, f);
		fwrite(e->pixels, 1, payload_len, f);
	}
	fclose(f);
	return true;
}

bool capture_read_fbcb(const char *path, damage_stream *out)
{
	FILE *f = fopen(path, "rb");
	char magic[4];
	uint32_t version, format, count32;
	size_t i;

	if (!f)
		return false;
	if (fread(magic, 1, 4, f) != 4 || memcmp(magic, FBCB_MAGIC, 4) != 0)
		goto fail;
	if (fread(&version, sizeof(version), 1, f) != 1 || version != 1)
		goto fail;
	if (fread(&out->width, sizeof(out->width), 1, f) != 1)
		goto fail;
	if (fread(&out->height, sizeof(out->height), 1, f) != 1)
		goto fail;
	if (fread(&format, sizeof(format), 1, f) != 1 || format != 0)
		goto fail;
	if (fread(&count32, sizeof(count32), 1, f) != 1)
		goto fail;

	out->count = count32;
	out->events = calloc(out->count ? out->count : 1,
			       sizeof(damage_event));
	if (!out->events)
		goto fail;

	for (i = 0; i < out->count; i++) {
		damage_event *e = &out->events[i];
		uint32_t stride, payload_len;

		if (fread(&e->timestamp_ns, sizeof(e->timestamp_ns), 1, f) !=
			    1 ||
		    fread(&e->frame_number, sizeof(e->frame_number), 1, f) !=
			    1 ||
		    fread(&e->x, sizeof(e->x), 1, f) != 1 ||
		    fread(&e->y, sizeof(e->y), 1, f) != 1 ||
		    fread(&e->w, sizeof(e->w), 1, f) != 1 ||
		    fread(&e->h, sizeof(e->h), 1, f) != 1 ||
		    fread(&stride, sizeof(stride), 1, f) != 1 ||
		    fread(&payload_len, sizeof(payload_len), 1, f) != 1) {
			out->count = i;
			damage_stream_free(out);
			goto fail;
		}
		e->pixels = malloc(payload_len ? payload_len : 1);
		if (!e->pixels || fread(e->pixels, 1, payload_len, f) !=
					    payload_len) {
			out->count = i + 1;
			damage_stream_free(out);
			goto fail;
		}
	}
	fclose(f);
	return true;

fail:
	fclose(f);
	return false;
}
