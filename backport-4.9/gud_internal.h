#ifndef __GUD_INTERNAL_H__
#define __GUD_INTERNAL_H__

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/usb.h>

#include "gud_compat_4_9.h"

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

struct gud_gem_object {
	struct drm_gem_object base;
	struct page **pages;
	void *vaddr;
};

#define to_gud_gem(gem) container_of(gem, struct gud_gem_object, base)

struct gud_gem_object *gud_gem_create(struct drm_device *dev, size_t size);
int gud_gem_get_pages(struct gud_gem_object *obj);
int gud_gem_vmap(struct gud_gem_object *obj, void **vaddr, size_t *size);
void gud_gem_free_object(struct drm_gem_object *gem);
int gud_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
			struct drm_mode_create_dumb *args);
int gud_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
			    u32 handle, u64 *offset);
int gud_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma);
extern const struct vm_operations_struct gud_gem_vm_ops;

int gud_get_display_descriptor(struct gud_device *gud);

#endif
