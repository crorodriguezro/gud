#ifndef __GUD_FRAME_LAYOUT_H__
#define __GUD_FRAME_LAYOUT_H__

#ifdef __KERNEL__
#include "gud_compat_4_9.h"
#else
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <drm/drm_fourcc.h>
typedef uint32_t u32;
#define GUD_LAYOUT_U32_MAX UINT32_MAX
#endif

#ifdef __KERNEL__
#define GUD_LAYOUT_U32_MAX U32_MAX
#endif

static inline unsigned int gud_format_bytes_per_pixel(u32 pixel_format)
{
	switch (pixel_format) {
	case DRM_FORMAT_RGB565:
		return 2;
	case DRM_FORMAT_XRGB8888:
		return 4;
	default:
		return 0;
	}
}

static inline int gud_format_frame_layout(u32 width, u32 height,
					u32 pixel_format,
					size_t *bytes_per_line, size_t *length)
{
	unsigned int bpp = gud_format_bytes_per_pixel(pixel_format);
	size_t line_bytes;

	if (!bpp || !width || !height)
		return -EINVAL;
	if ((size_t)width > (size_t)-1 / bpp)
		return -EOVERFLOW;
	line_bytes = (size_t)width * bpp;
	if (!line_bytes || (size_t)height > (size_t)-1 / line_bytes)
		return -EOVERFLOW;
	*length = line_bytes * (size_t)height;
	if (!*length || *length > GUD_LAYOUT_U32_MAX)
		return -EOVERFLOW;
	*bytes_per_line = line_bytes;
	return 0;
}

static inline int gud_validate_framebuffer_range(size_t framebuffer_offset,
						 size_t length, size_t mapped_size)
{
	if (framebuffer_offset > mapped_size ||
	    length > mapped_size - framebuffer_offset)
		return -EINVAL;
	return 0;
}

#endif
