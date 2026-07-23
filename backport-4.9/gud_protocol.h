#ifndef __GUD_PROTOCOL_H__
#define __GUD_PROTOCOL_H__

#include <linux/bitops.h>
#include <linux/types.h>

#define GUD_DISPLAY_MAGIC 0x1d50614d
#define GUD_PROTOCOL_VERSION 1
#define GUD_REQ_GET_DESCRIPTOR 0x01

struct gud_display_descriptor_req {
	__le32 magic;
	__u8 version;
	__le32 flags;
	__u8 compression;
	__le32 max_buffer_size;
	__le32 min_width;
	__le32 max_width;
	__le32 min_height;
	__le32 max_height;
} __packed;

#endif
