#include <stdlib.h>
#include <string.h>

#include "common.h"

bool rgb565_buffer_alloc(rgb565_buffer *buf, uint32_t width, uint32_t height)
{
	size_t bytes = rgb565_byte_size(width, height);

	buf->width = width;
	buf->height = height;
	buf->pixels = NULL;
	if (bytes == 0)
		return true;
	buf->pixels = malloc(bytes);
	if (!buf->pixels)
		return false;
	memset(buf->pixels, 0, bytes);
	return true;
}

void rgb565_buffer_free(rgb565_buffer *buf)
{
	if (!buf)
		return;
	free(buf->pixels);
	buf->pixels = NULL;
	buf->width = 0;
	buf->height = 0;
}
