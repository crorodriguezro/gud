/*
 * png_out.h - dumps RGB565 buffers (and amplified-difference images) as PNG
 * files for the quality/ comparison deliverable (PROJECT SPEC section 23).
 * Uses the vendored stb_image_write single-header library.
 */
#ifndef FBCODEC_PNG_OUT_H
#define FBCODEC_PNG_OUT_H

#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

int png_write_rgb565(const char *path, const uint16_t *pixels, uint32_t w,
		      uint32_t h);

/* Writes an amplified |a-b| difference image (scaled by `amplify`, clamped
 * to 255) so subtle lossy artifacts become visible.
 */
int png_write_diff(const char *path, const uint16_t *a, const uint16_t *b,
		    uint32_t w, uint32_t h, double amplify);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_PNG_OUT_H */
