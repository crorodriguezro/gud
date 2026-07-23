#ifndef __GUD_INTERNAL_H__
#define __GUD_INTERNAL_H__

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/usb.h>

struct gud_device {
	struct usb_device *usb;
	struct usb_interface *intf;
	u8 bulk_out_endpoint;
	u8 protocol_version;
	u32 max_buffer_size;
	u32 min_width;
	u32 max_width;
	u32 min_height;
	u32 max_height;
	bool disconnected;
	struct mutex lock;
};

int gud_get_display_descriptor(struct gud_device *gud);

#endif
