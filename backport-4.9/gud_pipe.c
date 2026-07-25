#include <linux/err.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "gud_internal.h"
#include "gud_protocol.h"

#define GUD_USB_TIMEOUT_MS 3000
#define GUD_BULK_EAGAIN_RETRIES 100
#define GUD_BULK_CHUNK_SIZE (64 * 1024)

struct gud_bulk_context {
	struct completion done;
	int status;
	int actual;
};

static void gud_bulk_complete(struct urb *urb)
{
	struct gud_bulk_context *context = urb->context;

	context->status = urb->status;
	context->actual = urb->actual_length;
	complete(&context->done);
}

/*
 * The USB bulk-message helper allocates and submits its own URB, so it cannot
 * use the DMA address returned by usb_alloc_coherent().  On the target xHCI
 * that causes core USB to remap the coherent allocation and reject it with
 * -EAGAIN.
 */
static int gud_usb_bulk_write(struct gud_device *gud, struct urb *urb,
			      void *buffer, dma_addr_t dma, int length,
			      int *actual)
{
	struct gud_bulk_context context;
	unsigned long timeout;
	int ret;

	init_completion(&context.done);
	context.status = 0;
	context.actual = 0;
	usb_fill_bulk_urb(urb, gud->usb,
			  usb_sndbulkpipe(gud->usb, gud->bulk_out_endpoint),
			  buffer, length, gud_bulk_complete, &context);
	urb->transfer_dma = dma;
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	ret = usb_submit_urb(urb, GFP_NOIO);
	if (ret)
		return ret;

	timeout = wait_for_completion_timeout(&context.done,
					     msecs_to_jiffies(GUD_USB_TIMEOUT_MS));
	if (!timeout) {
		usb_kill_urb(urb);
		return -ETIMEDOUT;
	}

	*actual = context.actual;
	return context.status;
}

static int gud_status_to_errno(u8 status)
{
	switch (status) {
	case GUD_STATUS_OK:
		return 0;
	case GUD_STATUS_BUSY:
		return -EBUSY;
	case GUD_STATUS_REQUEST_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case GUD_STATUS_PROTOCOL_ERROR:
		return -EPROTO;
	case GUD_STATUS_INVALID_PARAMETER:
		return -EINVAL;
	case GUD_STATUS_ERROR:
	default:
		return -EREMOTEIO;
	}
}

/* The caller holds gud->lock, which serializes USB disconnect and transfers. */
static int gud_usb_set(struct gud_device *gud, u8 request,
			       const void *data, u16 length)
{
	u8 status;
	u8 request_type = USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT;
	u8 ifnum = gud->intf->cur_altsetting->desc.bInterfaceNumber;
	int ret;

	if (gud->disconnected)
		return -ENODEV;

	ret = usb_control_msg(gud->usb, usb_sndctrlpipe(gud->usb, 0), request,
			      request_type, 0, ifnum, (void *)data, length,
			      USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(&gud->intf->dev, "GUD request 0x%02x failed: %d\n",
			request, ret);
		return ret;
	}
	if (ret != length) {
		dev_err(&gud->intf->dev,
			"GUD request 0x%02x short transfer: %d/%u\n",
			request, ret, length);
		return -EIO;
	}
	if (!(gud->flags & GUD_DISPLAY_FLAG_STATUS_ON_SET))
		return 0;

	ret = usb_control_msg(gud->usb, usb_rcvctrlpipe(gud->usb, 0),
			      GUD_REQ_GET_STATUS,
			      USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_IN,
			      0, ifnum, &status, sizeof(status), USB_CTRL_GET_TIMEOUT);
	if (ret != sizeof(status)) {
		dev_err(&gud->intf->dev, "GUD status request failed: %d\n", ret);
		return ret < 0 ? ret : -EIO;
	}
	if (gud_status_to_errno(status))
		dev_err(&gud->intf->dev, "GUD request 0x%02x status: 0x%02x\n",
			request, status);
	return gud_status_to_errno(status);
}

static void gud_mode_to_request(struct gud_display_mode_req *req,
				const struct drm_display_mode *mode)
{
	req->clock = cpu_to_le32(mode->clock);
	req->hdisplay = cpu_to_le16(mode->hdisplay);
	req->hsync_start = cpu_to_le16(mode->hsync_start);
	req->hsync_end = cpu_to_le16(mode->hsync_end);
	req->htotal = cpu_to_le16(mode->htotal);
	req->vdisplay = cpu_to_le16(mode->vdisplay);
	req->vsync_start = cpu_to_le16(mode->vsync_start);
	req->vsync_end = cpu_to_le16(mode->vsync_end);
	req->vtotal = cpu_to_le16(mode->vtotal);
	req->flags = cpu_to_le32(mode->flags & GUD_DISPLAY_MODE_FLAG_USER_MASK);
}

static int gud_pipe_state_check(struct gud_device *gud,
				const struct drm_plane_state *plane_state,
				const struct drm_crtc_state *crtc_state)
{
	struct gud_state_req request;
	int ret;

	if (!plane_state->fb)
		return 0;

	memset(&request, 0, sizeof(request));
	gud_mode_to_request(&request.mode, &crtc_state->mode);
	request.format = GUD_PIXEL_FORMAT_RGB565;
	request.connector = 0;

	mutex_lock(&gud->lock);
	ret = gud_usb_set(gud, GUD_REQ_SET_STATE_CHECK, &request, sizeof(request));
	mutex_unlock(&gud->lock);
	if (ret)
		dev_err(&gud->intf->dev, "GUD state check failed: %d\n", ret);
	return ret;
}

static int gud_pipe_transfer(struct gud_device *gud,
			     const struct drm_plane_state *plane_state)
{
	struct gud_framebuffer *gfb = to_gud_framebuffer(plane_state->fb);
	struct gud_gem_object *obj = to_gud_gem(gfb->obj);
	struct gud_set_buffer_req request;
	void *vaddr;
	void *bulk_buffer;
	dma_addr_t bulk_dma;
	struct urb *bulk_urb;
	size_t map_size;
	size_t bytes_per_line;
	size_t chunk_size;
	size_t max_chunk_size;
	size_t length;
	size_t offset;
	u32 rows_per_chunk;
	int actual;
	int ret;

	length = (size_t)plane_state->fb->width * 2 * plane_state->fb->height;
	if (!length || length > U32_MAX || length > obj->base.size)
		return -EINVAL;
	if (!gud->max_buffer_size)
		return -EINVAL;

	ret = gud_gem_vmap(obj, &vaddr, &map_size);
	if (ret)
		return ret;
	if (length > map_size)
		return -EINVAL;
	/*
	 * vmap() memory is not necessarily DMA-addressable on this 4.9 USB host.
	 * The host sends one bulk URB per SET_BUFFER, so each DMA-coherent
	 * bounce buffer contains complete rows for exactly one buffer rectangle.
	 */
	bytes_per_line = (size_t)plane_state->fb->width * 2;
	max_chunk_size = min_t(size_t, gud->max_buffer_size,
				   GUD_BULK_CHUNK_SIZE);
	rows_per_chunk = max_chunk_size / bytes_per_line;
	if (!rows_per_chunk)
		return -EINVAL;
	rows_per_chunk = min_t(u32, rows_per_chunk, plane_state->fb->height);
	chunk_size = (size_t)rows_per_chunk * bytes_per_line;
	bulk_buffer = usb_alloc_coherent(gud->usb, chunk_size, GFP_KERNEL,
				 &bulk_dma);
	if (!bulk_buffer)
		return -ENOMEM;
	bulk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!bulk_urb) {
		usb_free_coherent(gud->usb, chunk_size, bulk_buffer, bulk_dma);
		return -ENOMEM;
	}

	mutex_lock(&gud->lock);
	for (offset = 0; offset < length; ) {
		u32 rows = min_t(u32, rows_per_chunk,
				     (length - offset) / bytes_per_line);
		int chunk = rows * bytes_per_line;
		int retries;

		if (gud->disconnected) {
			ret = -ENODEV;
			break;
		}
		memset(&request, 0, sizeof(request));
		request.y = cpu_to_le32(offset / bytes_per_line);
		request.width = cpu_to_le32(plane_state->fb->width);
		request.height = cpu_to_le32(rows);
		request.length = cpu_to_le32(chunk);
		memcpy(bulk_buffer, (u8 *)vaddr + offset, chunk);
		ret = gud_usb_set(gud, GUD_REQ_SET_BUFFER, &request, sizeof(request));
		if (ret)
			break;
		for (retries = 0; ; retries++) {
			ret = gud_usb_bulk_write(gud, bulk_urb, bulk_buffer, bulk_dma,
						 chunk, &actual);
			if (ret != -EAGAIN || retries == GUD_BULK_EAGAIN_RETRIES)
				break;
			msleep(10);
		}
		if (ret)
			dev_err(&gud->intf->dev,
				"GUD bulk transfer failed after %d retries: %d\n",
				retries, ret);
		if (ret)
			break;
		if (actual != chunk) {
			ret = -EIO;
			break;
		}
		offset += chunk;
	}
	mutex_unlock(&gud->lock);
	usb_free_urb(bulk_urb);
	usb_free_coherent(gud->usb, chunk_size, bulk_buffer, bulk_dma);
	return ret;
}

static void gud_fb_destroy(struct drm_framebuffer *fb)
{
	struct gud_framebuffer *gfb = to_gud_framebuffer(fb);

	drm_framebuffer_cleanup(fb);
	drm_gem_object_unreference_unlocked(gfb->obj);
	kfree(gfb);
}

static int gud_fb_create_handle(struct drm_framebuffer *fb,
				struct drm_file *file, unsigned int *handle)
{
	return drm_gem_handle_create(file, to_gud_framebuffer(fb)->obj, handle);
}

static const struct drm_framebuffer_funcs gud_fb_funcs = {
	.destroy = gud_fb_destroy,
	.create_handle = gud_fb_create_handle,
};

static struct drm_framebuffer *gud_fb_create(struct drm_device *dev,
					     struct drm_file *file,
					     const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct gud_framebuffer *gfb;
	u64 line_bytes;
	u64 min_size;
	u64 rows_bytes = 0;
	int ret;

	if (!mode_cmd->width || !mode_cmd->height)
		return ERR_PTR(-EINVAL);
	if (!mode_cmd->handles[0])
		return ERR_PTR(-EINVAL);
	if (mode_cmd->pixel_format != DRM_FORMAT_RGB565)
		return ERR_PTR(-EINVAL);
	if (mode_cmd->handles[1] || mode_cmd->handles[2] || mode_cmd->handles[3])
		return ERR_PTR(-EINVAL);
	if (mode_cmd->pitches[1] || mode_cmd->pitches[2] || mode_cmd->pitches[3])
		return ERR_PTR(-EINVAL);
	if (mode_cmd->offsets[1] || mode_cmd->offsets[2] || mode_cmd->offsets[3])
		return ERR_PTR(-EINVAL);
	if (mode_cmd->modifier[0] &&
	    mode_cmd->modifier[0] != DRM_FORMAT_MOD_LINEAR)
		return ERR_PTR(-EINVAL);
	if (mode_cmd->modifier[1] || mode_cmd->modifier[2] || mode_cmd->modifier[3])
		return ERR_PTR(-EINVAL);

	line_bytes = (u64)mode_cmd->width * 2;
	if (line_bytes > U32_MAX)
		return ERR_PTR(-EINVAL);
	if (mode_cmd->pitches[0] < line_bytes)
		return ERR_PTR(-EINVAL);
	if (mode_cmd->height > 1) {
		rows_bytes = (u64)(mode_cmd->height - 1) * mode_cmd->pitches[0];
		if (mode_cmd->pitches[0] &&
		    rows_bytes / mode_cmd->pitches[0] != mode_cmd->height - 1)
			return ERR_PTR(-EINVAL);
	}

	obj = drm_gem_object_lookup(file, mode_cmd->handles[0]);
	if (!obj)
		return ERR_PTR(-ENOENT);
	min_size = (u64)mode_cmd->offsets[0] + rows_bytes;
	if (min_size < mode_cmd->offsets[0] || min_size + line_bytes < min_size ||
	    min_size + line_bytes > obj->size) {
		drm_gem_object_unreference_unlocked(obj);
		return ERR_PTR(-EINVAL);
	}

	gfb = kzalloc(sizeof(*gfb), GFP_KERNEL);
	if (!gfb) {
		drm_gem_object_unreference_unlocked(obj);
		return ERR_PTR(-ENOMEM);
	}

	gfb->obj = obj;
	drm_helper_mode_fill_fb_struct(&gfb->base, mode_cmd);

	ret = drm_framebuffer_init(dev, &gfb->base, &gud_fb_funcs);
	if (ret) {
		drm_gem_object_unreference_unlocked(obj);
		kfree(gfb);
		return ERR_PTR(ret);
	}

	return &gfb->base;
}

static int gud_pipe_check(struct drm_simple_display_pipe *pipe,
			  struct drm_plane_state *plane_state,
			  struct drm_crtc_state *crtc_state)
{
	struct gud_device *gud = container_of(pipe, struct gud_device, pipe);

	if (gud->disconnected)
		return -ENODEV;
	if (plane_state->fb &&
	    plane_state->fb->pixel_format != DRM_FORMAT_RGB565)
		return -EINVAL;
	/* Full-frame Ticket 5 uploads deliberately do not repack padded rows. */
	if (plane_state->fb &&
	    plane_state->fb->pitches[0] != plane_state->fb->width * 2)
		return -EINVAL;

	return gud_pipe_state_check(gud, plane_state, crtc_state);
}

static void gud_pipe_enable(struct drm_simple_display_pipe *pipe,
			    struct drm_crtc_state *crtc_state)
{
	(void)pipe;
	(void)crtc_state;
}

static void gud_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct gud_device *gud = container_of(pipe, struct gud_device, pipe);
	u8 disable = 0;

	mutex_lock(&gud->lock);
	if (!gud_usb_set(gud, GUD_REQ_SET_DISPLAY_ENABLE, &disable,
			 sizeof(disable)))
		gud_usb_set(gud, GUD_REQ_SET_CONTROLLER_ENABLE, &disable,
			    sizeof(disable));
	mutex_unlock(&gud->lock);
}

static void gud_pipe_update(struct drm_simple_display_pipe *pipe,
			    struct drm_plane_state *plane_state)
{
	struct gud_device *gud = container_of(pipe, struct gud_device, pipe);
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_plane_state *new_state = pipe->plane.state;
	unsigned long flags;
	u8 enable = 1;
	u8 display_enable = crtc->state->active ? 1 : 0;
	int ret;

	(void)plane_state;

	if (new_state->fb) {
		mutex_lock(&gud->lock);
		ret = gud_usb_set(gud, GUD_REQ_SET_STATE_COMMIT, NULL, 0);
		if (!ret)
			ret = gud_usb_set(gud, GUD_REQ_SET_CONTROLLER_ENABLE,
					  &enable, sizeof(enable));
		if (!ret)
			ret = gud_usb_set(gud, GUD_REQ_SET_DISPLAY_ENABLE,
					  &display_enable, sizeof(display_enable));
		mutex_unlock(&gud->lock);
		if (!ret)
			ret = gud_pipe_transfer(gud, new_state);
		if (ret)
			dev_err(&gud->intf->dev, "GUD atomic update failed: %d\n", ret);
	}

	/* A synchronous USB transfer is this pipe's commit-completion boundary. */
	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}

static const u32 gud_formats[] = {
	DRM_FORMAT_RGB565,
};

static const struct drm_simple_display_pipe_funcs gud_pipe_funcs = {
	.enable = gud_pipe_enable,
	.disable = gud_pipe_disable,
	.check = gud_pipe_check,
	.update = gud_pipe_update,
};

static const struct drm_mode_config_funcs gud_mode_config_funcs = {
	.fb_create = gud_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

int gud_pipe_init(struct gud_device *gud)
{
	int ret;

	drm_mode_config_init(gud->drm);
	gud->drm->mode_config.min_width = 1280;
	gud->drm->mode_config.max_width = 1280;
	gud->drm->mode_config.min_height = 720;
	gud->drm->mode_config.max_height = 720;
	gud->drm->mode_config.funcs = &gud_mode_config_funcs;

	ret = gud_connector_init(gud);
	if (ret)
		goto err_mode_config;

	ret = drm_simple_display_pipe_init(gud->drm, &gud->pipe, &gud_pipe_funcs,
					   gud_formats, ARRAY_SIZE(gud_formats),
					   &gud->connector);
	if (ret)
		goto err_mode_config;

	drm_mode_config_reset(gud->drm);
	return 0;

err_mode_config:
	drm_mode_config_cleanup(gud->drm);
	return ret;
}
