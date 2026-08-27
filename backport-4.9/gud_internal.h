#ifndef __GUD_INTERNAL_H__
#define __GUD_INTERNAL_H__

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/workqueue.h>

#include "gud_compat_4_9.h"

struct gud_device {
	struct usb_device *usb;
	struct usb_interface *intf;
	u8 bulk_out_endpoint;
	u8 protocol_version;
#ifdef GUD_XDISP_FULL_UPDATE
	u8 compression;
	void *xdisp_lz4_workmem;
	size_t xdisp_lz4_workmem_size;
	void *xdisp_lz4_scratch;
	size_t xdisp_lz4_scratch_size;
	size_t xdisp_max_source_length;
	void *xdisp_bulk_buffer;
	dma_addr_t xdisp_bulk_dma;
	struct urb *xdisp_bulk_urb;
	size_t xdisp_bulk_buffer_size;
#endif
	u32 flags;
	u32 max_buffer_size;
	u32 min_width;
	u32 max_width;
	u32 min_height;
	u32 max_height;
	/* Serialized by lock; bounded diagnostics for SET_BUFFER/bulk pairs. */
	u32 bulk_trace_count;
	u64 bulk_trace_sequence;
	bool bulk_trace_update_emitted;
	/* Serialized by lock; machine-readable XDISP timing identifiers. */
	u64 xdisp_frame_sequence;
	u64 xdisp_payload_sequence;
	bool disconnected;
#ifdef GUD_XDISP_PM_TEST
	/* Protected by lock; only the PM-test variant may reject I/O while asleep. */
	bool pm_suspended;
#endif
	struct mutex lock;
#ifdef GUD_XDISP_FULL_UPDATE
	struct work_struct async_work;
	struct mutex damage_lock;
	struct drm_framebuffer *async_fb;
	struct drm_rect async_damage;
	void *shadow_buf;
	size_t shadow_buf_size;
	bool async_stopping;
	u64 async_submit_sequence;
	u64 async_worker_sequence;
#endif
	struct drm_device *drm;
	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;
};

struct gud_gem_object {
	struct drm_gem_object base;
	struct page **pages;
	void *vaddr;
};

struct gud_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
};

#define to_gud_gem(gem) container_of(gem, struct gud_gem_object, base)
#define to_gud_framebuffer(fb) \
	container_of(fb, struct gud_framebuffer, base)

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
int gud_connector_init(struct gud_device *gud);
int gud_pipe_init(struct gud_device *gud);
int gud_drm_init(struct gud_device *gud);
void gud_drm_fini(struct gud_device *gud);
#ifdef GUD_XDISP_FULL_UPDATE
int gud_xdisp_buffers_init(struct gud_device *gud);
void gud_xdisp_buffers_fini(struct gud_device *gud);
void gud_async_init(struct gud_device *gud);
void gud_async_cancel(struct gud_device *gud);
void gud_async_stop(struct gud_device *gud);
#endif

#ifdef GUD_XDISP_PM_TEST
static inline int gud_pm_submission_allowed(const struct gud_device *gud)
{
	return gud->pm_suspended ? -EHOSTUNREACH : 0;
}
#else
static inline int gud_pm_submission_allowed(const struct gud_device *gud)
{
	return 0;
}
#endif

#endif
