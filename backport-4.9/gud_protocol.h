#ifndef __GUD_PROTOCOL_H__
#define __GUD_PROTOCOL_H__

#include <linux/bitops.h>
#include <linux/types.h>

#define GUD_DISPLAY_MAGIC 0x1d50614d
#define GUD_PROTOCOL_VERSION 1
#define GUD_REQ_GET_STATUS 0x00
#define GUD_REQ_GET_DESCRIPTOR 0x01
#define GUD_REQ_SET_BUFFER 0x60
#define GUD_REQ_SET_STATE_CHECK 0x61
#define GUD_REQ_SET_STATE_COMMIT 0x62
#define GUD_REQ_SET_CONTROLLER_ENABLE 0x63
#define GUD_REQ_SET_DISPLAY_ENABLE 0x64

#define GUD_DISPLAY_FLAG_STATUS_ON_SET BIT(0)

#define GUD_STATUS_OK 0x00
#define GUD_STATUS_BUSY 0x01
#define GUD_STATUS_REQUEST_NOT_SUPPORTED 0x02
#define GUD_STATUS_PROTOCOL_ERROR 0x03
#define GUD_STATUS_INVALID_PARAMETER 0x04
#define GUD_STATUS_ERROR 0x05

#define GUD_PIXEL_FORMAT_RGB565 0x40

#define GUD_DISPLAY_MODE_FLAG_USER_MASK 0x000033ff

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

struct gud_display_mode_req {
	__le32 clock;
	__le16 hdisplay;
	__le16 hsync_start;
	__le16 hsync_end;
	__le16 htotal;
	__le16 vdisplay;
	__le16 vsync_start;
	__le16 vsync_end;
	__le16 vtotal;
	__le32 flags;
} __packed;

struct gud_set_buffer_req {
	__le32 x;
	__le32 y;
	__le32 width;
	__le32 height;
	__le32 length;
	__u8 compression;
	__le32 compressed_length;
} __packed;

struct gud_state_req {
	struct gud_display_mode_req mode;
	__u8 format;
	__u8 connector;
} __packed;

#endif
