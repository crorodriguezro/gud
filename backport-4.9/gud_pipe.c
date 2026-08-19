#include <linux/err.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/slab.h>
#ifdef GUD_XDISP_LZ4_12800
#include <linux/vmalloc.h>
#endif

#include "gud_internal.h"
#include "gud_frame_layout.h"
#include "gud_protocol.h"

#define GUD_USB_TIMEOUT_MS 3000
#define GUD_BULK_EAGAIN_RETRIES 100
#define GUD_BULK_CHUNK_SIZE (64 * 1024)
#define XDISP_PROBE_PRE_BULK_PAUSE_MAX_MS 90000

static unsigned int gud_bulk_timeout_ms = GUD_USB_TIMEOUT_MS;
module_param_named(bulk_timeout_ms, gud_bulk_timeout_ms, uint, 0644);
MODULE_PARM_DESC(bulk_timeout_ms,
	"GUD bulk OUT timeout in milliseconds (default: 3000)");

/* Zero is the normal quiet path. Set this to trace only the first N payloads. */
static unsigned int gud_bulk_trace_limit;
module_param_named(bulk_trace_limit, gud_bulk_trace_limit, uint, 0644);
MODULE_PARM_DESC(bulk_trace_limit,
	"Trace the first N GUD SET_BUFFER/bulk OUT transactions (default: 0)");

#ifdef GUD_XDISP_LZ4_12800
/* Benchmark-only comparison policy. The hard USB submission cap remains
 * GUD_XDISP_PAYLOAD_LIMIT in both modes. */
static bool xdisp_target_policy;
module_param_named(xdisp_target_policy, xdisp_target_policy, bool, 0644);
MODULE_PARM_DESC(xdisp_target_policy,
	"Use a 95%-of-cap target and recent-ratio row prediction for XDISP");

static bool xdisp_frame_stats;
module_param_named(xdisp_frame_stats, xdisp_frame_stats, bool, 0644);
MODULE_PARM_DESC(xdisp_frame_stats,
	"Emit non-rate-limited per-frame XDISP planner counters for benchmarking");

static bool xdisp_payload_timing;
module_param_named(xdisp_payload_timing, xdisp_payload_timing, bool, 0644);
MODULE_PARM_DESC(xdisp_payload_timing,
	"Emit machine-readable timing for every XDISP SET_BUFFER/bulk payload");

static bool xdisp_ratio_cache;
module_param_named(xdisp_ratio_cache, xdisp_ratio_cache, bool, 0644);
MODULE_PARM_DESC(xdisp_ratio_cache,
	"Seed XDISP rectangles from recent verified compression ratios");

static bool xdisp_bounded_discovery;
module_param_named(xdisp_bounded_discovery, xdisp_bounded_discovery, bool,
			   0644);
MODULE_PARM_DESC(xdisp_bounded_discovery,
	"Use upstream LZ4 bounded-output discovery with complete-row validation");

static bool xdisp_predictive_bounded;
module_param_named(xdisp_predictive_bounded, xdisp_predictive_bounded, bool,
			   0644);
MODULE_PARM_DESC(xdisp_predictive_bounded,
	"Test-only: predict bounded LZ4 rows, then fall back to verified discovery");

static unsigned int xdisp_probe_payload_length;
module_param_named(xdisp_probe_payload_length, xdisp_probe_payload_length,
			   uint, 0644);
MODULE_PARM_DESC(xdisp_probe_payload_length,
	"Test-only single-payload LZ4 bulk length (1..12800; zero disables)");

static bool xdisp_probe_zero_packet;
module_param_named(xdisp_probe_zero_packet, xdisp_probe_zero_packet, bool, 0644);
MODULE_PARM_DESC(xdisp_probe_zero_packet,
	"Test-only add URB_ZERO_PACKET to an aligned single-payload probe");

static unsigned int xdisp_probe_pre_bulk_pause_ms;
module_param_named(xdisp_probe_pre_bulk_pause_ms,
			   xdisp_probe_pre_bulk_pause_ms, uint, 0644);
MODULE_PARM_DESC(xdisp_probe_pre_bulk_pause_ms,
	"Test-only pause after successful SET_BUFFER before bulk submit (max 90000 ms)");
#endif

struct gud_bulk_context {
	struct completion done;
	int status;
	int actual;
	u64 completion_ns;
};

struct gud_bulk_timing {
	u64 submit_start_ns;
	u64 submit_end_ns;
	u64 wait_start_ns;
	u64 wait_end_ns;
	u64 completion_ns;
	int submit_result;
	int completion_wait_result;
};

static void gud_bulk_complete(struct urb *urb)
{
	struct gud_bulk_context *context = urb->context;

	context->status = urb->status;
	context->actual = urb->actual_length;
	context->completion_ns = ktime_get_ns();
	complete(&context->done);
}

/* The caller holds gud->lock, so this counter is intentionally non-atomic. */
static u64 gud_bulk_trace_begin(struct gud_device *gud)
{
	if (!gud_bulk_trace_limit ||
	    gud->bulk_trace_count >= gud_bulk_trace_limit)
		return 0;

	gud->bulk_trace_count++;
	return ++gud->bulk_trace_sequence;
}

static void gud_trace_set_buffer(struct gud_device *gud, u64 trace,
				 const struct gud_set_buffer_req *request,
				 int transfer_length)
{
	u32 length;
	u32 compressed_length;
	u32 expected_length;

	if (!trace)
		return;

	length = le32_to_cpu(request->length);
	compressed_length = le32_to_cpu(request->compressed_length);
	expected_length = compressed_length ? compressed_length : length;
	dev_info(&gud->intf->dev,
		 "GUD trace=%llu SET_BUFFER x=%u y=%u w=%u h=%u length=%u compression=%u compressed_length=%u expected=%u trlen=%d max_buffer_size=%u\n",
		 (unsigned long long)trace, le32_to_cpu(request->x),
		 le32_to_cpu(request->y), le32_to_cpu(request->width),
		 le32_to_cpu(request->height), length, request->compression,
		 compressed_length, expected_length, transfer_length,
		 gud->max_buffer_size);
}

/*
 * The USB bulk-message helper allocates and submits its own URB, so it cannot
 * use the DMA address returned by usb_alloc_coherent().  On the target xHCI
 * that causes core USB to remap the coherent allocation and reject it with
 * -EAGAIN.
 */
static int gud_usb_bulk_write(struct gud_device *gud, struct urb *urb,
			      void *buffer, dma_addr_t dma, int length,
			      int *actual, u64 trace, int attempt,
			      struct gud_bulk_timing *timing)
{
	struct gud_bulk_context context;
	unsigned long timeout;
	u64 elapsed_ns;
	u64 start_ns;
	int ret;

	init_completion(&context.done);
	context.status = 0;
	context.actual = 0;
	context.completion_ns = 0;
	usb_fill_bulk_urb(urb, gud->usb,
			  usb_sndbulkpipe(gud->usb, gud->bulk_out_endpoint),
			  buffer, length, gud_bulk_complete, &context);
	urb->transfer_dma = dma;
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
#ifdef GUD_XDISP_LZ4_12800
	if (xdisp_probe_payload_length && xdisp_probe_zero_packet &&
	    length == xdisp_probe_payload_length &&
	    !(length % usb_maxpacket(gud->usb,
		usb_sndbulkpipe(gud->usb, gud->bulk_out_endpoint), 1)))
		urb->transfer_flags |= URB_ZERO_PACKET;
#endif

	start_ns = ktime_get_ns();
	if (timing)
		timing->submit_start_ns = start_ns;
	if (trace)
		dev_info(&gud->intf->dev,
			 "GUD trace=%llu bulk attempt=%d submit length=%d endpoint=0x%02x timeout_ms=%u\n",
			 (unsigned long long)trace, attempt, length,
			 gud->bulk_out_endpoint, gud_bulk_timeout_ms);
	ret = usb_submit_urb(urb, GFP_NOIO);
	if (timing)
		timing->submit_result = ret;
	if (timing)
		timing->submit_end_ns = ktime_get_ns();
	if (ret) {
		if (timing)
			timing->wait_start_ns = timing->submit_end_ns;
		if (timing)
			timing->wait_end_ns = timing->submit_end_ns;
		if (timing)
			timing->completion_wait_result = ret;
		elapsed_ns = ktime_get_ns() - start_ns;
		if (trace)
			dev_info(&gud->intf->dev,
				 "GUD trace=%llu bulk attempt=%d result=%d actual=0 elapsed_us=%llu\n",
				 (unsigned long long)trace, attempt, ret,
				 (unsigned long long)(elapsed_ns / 1000));
		return ret;
	}

	if (timing)
		timing->wait_start_ns = ktime_get_ns();
	timeout = wait_for_completion_timeout(&context.done,
					     msecs_to_jiffies(gud_bulk_timeout_ms));
	if (timing)
		timing->wait_end_ns = ktime_get_ns();
	if (!timeout) {
		usb_kill_urb(urb);
		*actual = context.actual;
		elapsed_ns = ktime_get_ns() - start_ns;
		if (trace)
			dev_info(&gud->intf->dev,
				 "GUD trace=%llu bulk attempt=%d result=%d actual=%d elapsed_us=%llu\n",
				 (unsigned long long)trace, attempt, -ETIMEDOUT,
				 *actual, (unsigned long long)(elapsed_ns / 1000));
		if (timing)
			timing->completion_wait_result = -ETIMEDOUT;
		return -ETIMEDOUT;
	}

	*actual = context.actual;
	if (timing)
		timing->completion_ns = context.completion_ns;
	if (timing)
		timing->completion_wait_result = context.status;
	elapsed_ns = ktime_get_ns() - start_ns;
	if (trace)
		dev_info(&gud->intf->dev,
			 "GUD trace=%llu bulk attempt=%d result=%d actual=%d elapsed_us=%llu\n",
			 (unsigned long long)trace, attempt, context.status,
			 *actual, (unsigned long long)(elapsed_ns / 1000));
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

static unsigned int gud_bytes_per_pixel(u32 pixel_format)
{
	return gud_format_bytes_per_pixel(pixel_format);
}

static u8 gud_to_protocol_format(u32 pixel_format)
{
	switch (pixel_format) {
	case DRM_FORMAT_RGB565:
		return GUD_PIXEL_FORMAT_RGB565;
	case DRM_FORMAT_XRGB8888:
		return GUD_PIXEL_FORMAT_XRGB8888;
	default:
		return 0;
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
	ret = gud_pm_submission_allowed(gud);
	if (ret)
		return ret;

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
	u32 pixel_format;
	int ret;

	if (!plane_state->fb)
		return 0;

	pixel_format = plane_state->fb->pixel_format;
	if (!gud_bytes_per_pixel(pixel_format))
		return -EINVAL;

	memset(&request, 0, sizeof(request));
	gud_mode_to_request(&request.mode, &crtc_state->mode);
	request.format = gud_to_protocol_format(pixel_format);
	request.connector = 0;

	mutex_lock(&gud->lock);
	ret = gud_usb_set(gud, GUD_REQ_SET_STATE_CHECK, &request, sizeof(request));
	mutex_unlock(&gud->lock);
	if (ret)
		dev_err(&gud->intf->dev, "GUD state check failed: %d\n", ret);
	return ret;
}

#ifdef GUD_XDISP_LZ4_12800
int gud_xdisp_buffers_init(struct gud_device *gud)
{
	/*
	 * Worst-case scratch capacity uses *4 (XRGB8888) for the largest
	 * format.  This is intentional: the scratch buffer is allocated once
	 * and reused for both RGB565 and XRGB8888, so sizing for the larger
	 * format guarantees capacity for either.  Per-transfer sizing is
	 * format-aware via gud_bytes_per_pixel() in gud_pipe_transfer_xdisp().
	 */
	size_t bytes_per_line = (size_t)gud->max_width * 4U;
	size_t max_source_length;
	size_t scratch_size;
	size_t workmem_size;

	if (bytes_per_line > GUD_XDISP_PAYLOAD_LIMIT)
		return -E2BIG;

	max_source_length = min_t(size_t, gud->max_buffer_size,
				  (size_t)gud->max_width * gud->max_height * 4U);
	max_source_length -= max_source_length % bytes_per_line;
	if (!max_source_length)
		return -EINVAL;

	scratch_size = gud_xdisp_lz4_compress_bound(max_source_length);
	if (!scratch_size)
		return -EOVERFLOW;
	workmem_size = gud_xdisp_lz4_upstream_workmem_size();
	if (!workmem_size)
		return -EOVERFLOW;

	gud->xdisp_lz4_workmem = kmalloc(workmem_size, GFP_KERNEL);
	if (!gud->xdisp_lz4_workmem)
		return -ENOMEM;
	gud->xdisp_lz4_workmem_size = workmem_size;

	gud->xdisp_lz4_scratch = vmalloc(scratch_size);
	if (!gud->xdisp_lz4_scratch)
		goto err_workmem;
	gud->xdisp_lz4_scratch_size = scratch_size;

	gud->xdisp_bulk_buffer = usb_alloc_coherent(
		gud->usb, GUD_XDISP_PAYLOAD_LIMIT, GFP_KERNEL,
		&gud->xdisp_bulk_dma);
	if (!gud->xdisp_bulk_buffer)
		goto err_scratch;

	gud->xdisp_bulk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!gud->xdisp_bulk_urb)
		goto err_bulk;

	gud->xdisp_max_source_length = max_source_length;
	return 0;

err_bulk:
	usb_free_coherent(gud->usb, GUD_XDISP_PAYLOAD_LIMIT,
			  gud->xdisp_bulk_buffer, gud->xdisp_bulk_dma);
	gud->xdisp_bulk_buffer = NULL;
err_scratch:
	vfree(gud->xdisp_lz4_scratch);
	gud->xdisp_lz4_scratch = NULL;
	gud->xdisp_lz4_scratch_size = 0;
err_workmem:
	kfree(gud->xdisp_lz4_workmem);
	gud->xdisp_lz4_workmem = NULL;
	gud->xdisp_lz4_workmem_size = 0;
	gud->xdisp_max_source_length = 0;
	return -ENOMEM;
}

void gud_xdisp_buffers_fini(struct gud_device *gud)
{
	usb_free_urb(gud->xdisp_bulk_urb);
	gud->xdisp_bulk_urb = NULL;
	if (gud->xdisp_bulk_buffer) {
		usb_free_coherent(gud->usb, GUD_XDISP_PAYLOAD_LIMIT,
				  gud->xdisp_bulk_buffer,
				  gud->xdisp_bulk_dma);
		gud->xdisp_bulk_buffer = NULL;
	}
	vfree(gud->xdisp_lz4_scratch);
	gud->xdisp_lz4_scratch = NULL;
	gud->xdisp_lz4_scratch_size = 0;
	gud->xdisp_max_source_length = 0;
	kfree(gud->xdisp_lz4_workmem);
	gud->xdisp_lz4_workmem = NULL;
	gud->xdisp_lz4_workmem_size = 0;
}

static size_t gud_xdisp_lz4_extension_size(size_t length, size_t base)
{
	return length < base ? 0 : 1 + (length - base) / 255;
}

/*
 * Generate one valid LZ4 sequence that expands to raw_length. The literal run
 * absorbs the requested wire length and the final offset-one match completes
 * the rectangle without adding bytes outside the GUD compressed payload.
 */
static int gud_xdisp_build_probe_payload(u8 *payload, size_t payload_length,
					 size_t raw_length)
{
	size_t literals;

	if (!payload || payload_length > GUD_XDISP_PAYLOAD_LIMIT ||
	    raw_length <= payload_length)
		return -EINVAL;
	for (literals = 4; literals <= raw_length - 4; literals++) {
		size_t match_length;
		size_t encoded = 1 + literals + 2 +
			gud_xdisp_lz4_extension_size(literals, 15);
		size_t value;
		size_t at = 0;

		/* LZ4 requires a final literal-only sequence of at least five bytes. */
		if (raw_length < literals + 4 + 5)
			continue;
		match_length = raw_length - literals - 5;
		encoded += gud_xdisp_lz4_extension_size(match_length, 19) + 1 + 5;

		if (encoded != payload_length)
			continue;
		payload[at++] = (min_t(size_t, literals, 15) << 4) |
			min_t(size_t, match_length - 4, 15);
		if (literals >= 15) {
			for (value = literals - 15; value >= 255; value -= 255)
				payload[at++] = 255;
			payload[at++] = value;
		}
		memset(payload + at, 0, literals);
		at += literals;
		payload[at++] = 1;
		payload[at++] = 0;
		if (match_length >= 19) {
			for (value = match_length - 19; value >= 255; value -= 255)
				payload[at++] = 255;
			payload[at++] = value;
		}
		payload[at++] = 5 << 4;
		memset(payload + at, 0, 5);
		at += 5;
		return at == payload_length ? 0 : -EINVAL;
	}
	return -EINVAL;
}

static int gud_pipe_transfer_xdisp_probe(struct gud_device *gud,
					 const struct drm_plane_state *plane_state)
{
	struct gud_set_buffer_req request = { 0 };
	struct gud_bulk_timing timing = { 0 };
	u32 bpp = gud_format_bytes_per_pixel(plane_state->fb->pixel_format);
	u32 width = 640;
	u32 height;
	size_t raw_length;
	int actual = 0;
	int set_buffer_result;
	int bulk_result = 0;

	if (!bpp || xdisp_probe_payload_length > GUD_XDISP_PAYLOAD_LIMIT)
		return -EINVAL;
	if (xdisp_probe_pre_bulk_pause_ms > XDISP_PROBE_PRE_BULK_PAUSE_MAX_MS)
		return -EINVAL;
	/* E1-T02 advertises no compression, so its exact 12,800-byte
	 * payload is an uncompressed 640x5 XRGB8888 rectangle. */
	raw_length = xdisp_probe_payload_length;
	if (raw_length % ((size_t)width * bpp))
		return -EINVAL;
	height = raw_length / ((size_t)width * bpp);
	if (!height || height > plane_state->fb->height || width > plane_state->fb->width)
		return -EINVAL;
	memset(gud->xdisp_bulk_buffer, 0, raw_length);
	request.width = cpu_to_le32(width);
	request.height = cpu_to_le32(height);
	request.length = cpu_to_le32(raw_length);
	request.compression = 0;
	request.compressed_length = 0;

	dev_info(&gud->intf->dev,
		 "XDISP_PROBE start payload_bytes=%u raw_bytes=%zu rect=%ux%u format=0x%08x zero_packet=%u\n",
		 xdisp_probe_payload_length, raw_length, width, height,
		 plane_state->fb->pixel_format, xdisp_probe_zero_packet);
	set_buffer_result = gud_usb_set(gud, GUD_REQ_SET_BUFFER, &request,
					sizeof(request));
	if (!set_buffer_result) {
		if (xdisp_probe_pre_bulk_pause_ms) {
			dev_info(&gud->intf->dev,
				 "XDISP_PROBE pre-bulk pause ready pause_ms=%u; disconnect now\n",
				 xdisp_probe_pre_bulk_pause_ms);
			msleep(xdisp_probe_pre_bulk_pause_ms);
		}
		bulk_result = gud_usb_bulk_write(gud, gud->xdisp_bulk_urb,
			gud->xdisp_bulk_buffer, gud->xdisp_bulk_dma,
			xdisp_probe_payload_length, &actual, 1, 0, &timing);
	}
	dev_info(&gud->intf->dev,
		 "XDISP_PROBE complete set_buffer_result=%d bulk_submit_start_ns=%llu bulk_submit_end_ns=%llu usb_submit_urb_result=%d bulk_completion_callback_ns=%llu urb_status=%d urb_actual_length=%d completion_wait_result=%d\n",
		 set_buffer_result, (unsigned long long)timing.submit_start_ns,
		 (unsigned long long)timing.submit_end_ns,
		 timing.submit_result, (unsigned long long)timing.completion_ns,
		 bulk_result, actual, timing.completion_wait_result);
	return set_buffer_result ? set_buffer_result :
		(bulk_result ? bulk_result : actual == xdisp_probe_payload_length ? 0 : -EIO);
}

static int gud_pipe_transfer_xdisp(struct gud_device *gud,
				   const struct drm_plane_state *plane_state)
{
	struct gud_framebuffer *gfb = to_gud_framebuffer(plane_state->fb);
	struct gud_gem_object *obj = to_gud_gem(gfb->obj);
	struct gud_set_buffer_req request;
	struct gud_xdisp_bounded_frame bounded_frame = { 0 };
	size_t bytes_per_line;
	size_t framebuffer_offset;
	size_t length;
	size_t map_size;
	size_t offset = 0;
	size_t total_payload = 0;
	void *vaddr;
	u32 bpp;
	u32 compressed_rects = 0;
	u32 max_payload = 0;
	u32 max_rows;
	u32 predictive_rows = 0;
	u32 row_hint;
	u32 raw_rects = 0;
	u32 raw_backoff_rects = 0;
	u32 rectangles = 0;
	u32 predictive_hits = 0;
	u32 predictive_fallbacks = 0;
	u64 compression_attempts = 0;
	u64 rejected_compression_attempts = 0;
	u64 compression_source_bytes = 0;
	u64 planner_ns = 0;
	u64 copy_ns = 0;
	u64 set_buffer_ns = 0;
	u64 bulk_wait_ns = 0;
	u64 frame_start_ns;
	u64 frame_seq;
	size_t plan_payload_limit;
	size_t reported_target;
	bool bounded_policy;
	int ret;

	if (xdisp_probe_payload_length)
		return gud_pipe_transfer_xdisp_probe(gud, plane_state);

	bpp = gud_format_bytes_per_pixel(plane_state->fb->pixel_format);
	if (!gud->max_buffer_size)
		return -EINVAL;
	ret = gud_format_frame_layout(plane_state->fb->width,
				  plane_state->fb->height,
				  plane_state->fb->pixel_format,
				  &bytes_per_line, &length);
	if (ret)
		return ret;
	if (bytes_per_line > GUD_XDISP_PAYLOAD_LIMIT)
		return -E2BIG;

	ret = gud_gem_vmap(obj, &vaddr, &map_size);
	if (ret)
		return ret;
	framebuffer_offset = plane_state->fb->offsets[0];
	ret = gud_validate_framebuffer_range(framebuffer_offset, length, map_size);
	if (ret)
		return ret;
	vaddr = (u8 *)vaddr + framebuffer_offset;

	max_rows = min_t(size_t, gud->xdisp_max_source_length, length) /
		   bytes_per_line;
	if (!max_rows)
		return -EINVAL;
	bounded_policy = xdisp_bounded_discovery || xdisp_predictive_bounded;
	plan_payload_limit = GUD_XDISP_PAYLOAD_LIMIT;
	if (bounded_policy)
		plan_payload_limit = GUD_XDISP_PAYLOAD_LIMIT;
	else if (xdisp_target_policy)
		plan_payload_limit = GUD_XDISP_PAYLOAD_LIMIT *
			GUD_XDISP_TARGET_PAYLOAD_PERCENT / 100U;
	reported_target = bounded_policy ?
		GUD_XDISP_DISCOVERY_TARGET_PAYLOAD : plan_payload_limit;
	frame_start_ns = ktime_get_ns();

	mutex_lock(&gud->lock);
	frame_seq = ++gud->xdisp_frame_sequence;
	row_hint = max_rows;
	if (xdisp_ratio_cache && gud->xdisp_ratio_valid &&
	    gud->xdisp_ratio_width == plane_state->fb->width &&
	    gud->xdisp_ratio_bytes_per_line == bytes_per_line) {
		struct gud_xdisp_chunk cached = {
			.source_length = gud->xdisp_ratio_source_bytes,
			.payload_length = gud->xdisp_ratio_payload_bytes,
		};

		row_hint = gud_xdisp_next_row_hint_target(
			&cached, bytes_per_line, max_rows,
			GUD_XDISP_PAYLOAD_LIMIT *
			GUD_XDISP_TARGET_PAYLOAD_PERCENT / 100U);
	}
	if (xdisp_predictive_bounded && gud->xdisp_predictive_valid &&
	    gud->xdisp_predictive_width == plane_state->fb->width &&
	    gud->xdisp_predictive_bytes_per_line == bytes_per_line) {
		struct gud_xdisp_chunk cached = {
			.source_length = gud->xdisp_predictive_source_bytes,
			.payload_length = gud->xdisp_predictive_payload_bytes,
		};

		predictive_rows = gud_xdisp_next_row_hint_target(
			&cached, bytes_per_line, max_rows,
			GUD_XDISP_DISCOVERY_TARGET_PAYLOAD);
	}
	while (offset < length) {
		struct gud_xdisp_chunk chunk;
		const void *payload;
		u32 remaining_rows = (length - offset) / bytes_per_line;
		int actual = 0;
		int retries;
		int set_buffer_ret;
		int transfer_length;
		u64 phase_start_ns;
		u64 trace;
		u64 payload_seq;
		u64 payload_start_ns = ktime_get_ns();
		u64 planner_start_ns = 0;
		u64 planner_end_ns = 0;
		u64 copy_start_ns;
		u64 copy_end_ns;
		u64 set_buffer_start_ns;
		u64 set_buffer_end_ns;
		struct gud_bulk_timing bulk_timing = { 0 };

		if (gud->disconnected) {
			ret = -ENODEV;
			break;
		}
		ret = gud_pm_submission_allowed(gud);
		if (ret)
			break;

		if (gud->compression & GUD_COMPRESSION_LZ4) {
			phase_start_ns = ktime_get_ns();
			planner_start_ns = phase_start_ns;
			if (xdisp_predictive_bounded)
				ret = gud_xdisp_plan_chunk_predictive_frame(
					(u8 *)vaddr + offset, remaining_rows,
					bytes_per_line, max_rows,
					predictive_rows, plan_payload_limit,
					gud->xdisp_lz4_workmem,
					gud->xdisp_lz4_scratch,
					gud->xdisp_lz4_scratch_size,
					&bounded_frame, &chunk);
			else if (xdisp_bounded_discovery)
				ret = gud_xdisp_plan_chunk_bounded_frame(
					(u8 *)vaddr + offset, remaining_rows,
					bytes_per_line, max_rows,
					plan_payload_limit,
					gud->xdisp_lz4_workmem,
					gud->xdisp_lz4_scratch,
					gud->xdisp_lz4_scratch_size,
					&bounded_frame, &chunk);
			else
				ret = gud_xdisp_plan_chunk(
					(u8 *)vaddr + offset, remaining_rows,
					bytes_per_line, min(row_hint, max_rows),
					plan_payload_limit,
					gud->xdisp_lz4_workmem,
					gud->xdisp_lz4_scratch,
					gud->xdisp_lz4_scratch_size, &chunk);
			planner_end_ns = ktime_get_ns();
			planner_ns += planner_end_ns - phase_start_ns;
			if (ret)
				break;
		} else {
			planner_start_ns = ktime_get_ns();
			chunk.rows = gud_xdisp_raw_rows(remaining_rows,
							 bytes_per_line, max_rows);
			chunk.source_length =
				(size_t)chunk.rows * bytes_per_line;
			chunk.payload_length = chunk.source_length;
			chunk.compressed = false;
			chunk.compression_attempts = 0;
			chunk.rejected_compression_attempts = 0;
			chunk.compression_source_bytes = 0;
			chunk.predictive_hit = false;
			chunk.predictive_fallback = false;
			planner_end_ns = ktime_get_ns();
		}

		/*
		 * This check is intentionally adjacent to the only SET_BUFFER /
		 * bulk submission path.  A larger host URB is never submitted,
		 * even if a future planner regression returns a bad result.
		 */
		if (!chunk.rows || !chunk.source_length ||
		    !chunk.payload_length ||
		    chunk.payload_length > GUD_XDISP_PAYLOAD_LIMIT) {
			ret = -EOVERFLOW;
			break;
		}
		transfer_length = chunk.payload_length;

		payload = chunk.compressed ? gud->xdisp_lz4_scratch :
			  (u8 *)vaddr + offset;
		copy_start_ns = ktime_get_ns();
		memcpy(gud->xdisp_bulk_buffer, payload,
		       chunk.payload_length);
		copy_end_ns = ktime_get_ns();
		copy_ns += copy_end_ns - copy_start_ns;

		memset(&request, 0, sizeof(request));
		request.y = cpu_to_le32(offset / bytes_per_line);
		request.width = cpu_to_le32(plane_state->fb->width);
		request.height = cpu_to_le32(chunk.rows);
		request.length = cpu_to_le32(chunk.source_length);
		if (chunk.compressed) {
			request.compression = GUD_COMPRESSION_LZ4;
			request.compressed_length =
				cpu_to_le32(chunk.payload_length);
		}

		trace = gud_bulk_trace_begin(gud);
		payload_seq = ++gud->xdisp_payload_sequence;
		gud_trace_set_buffer(gud, trace, &request, transfer_length);
		phase_start_ns = ktime_get_ns();
		set_buffer_start_ns = phase_start_ns;
		ret = gud_usb_set(gud, GUD_REQ_SET_BUFFER, &request,
				  sizeof(request));
		set_buffer_ret = ret;
		set_buffer_end_ns = ktime_get_ns();
		set_buffer_ns += set_buffer_end_ns - phase_start_ns;
		if (trace)
			dev_info(&gud->intf->dev,
				 "GUD trace=%llu SET_BUFFER result=%d elapsed_us=%llu\n",
				 (unsigned long long)trace, ret,
				 (unsigned long long)((set_buffer_end_ns - set_buffer_start_ns) / 1000));
		if (!ret) {
			phase_start_ns = ktime_get_ns();
			for (retries = 0; ; retries++) {
				ret = gud_usb_bulk_write(
					gud, gud->xdisp_bulk_urb,
					gud->xdisp_bulk_buffer,
					gud->xdisp_bulk_dma,
					transfer_length, &actual, trace, retries, &bulk_timing);
				if (ret != -EAGAIN ||
				    retries == GUD_BULK_EAGAIN_RETRIES)
					break;
				msleep(10);
			}
			bulk_wait_ns += ktime_get_ns() - phase_start_ns;
		} else {
			retries = 0;
			actual = 0;
		}
		if (xdisp_payload_timing)
			dev_info(&gud->intf->dev,
				 "xdisp_payload_timing frame_seq=%llu rectangle_seq=%u payload_seq=%llu payload_bytes=%d planner_start_ns=%llu planner_end_ns=%llu compression_start_ns=%llu compression_end_ns=%llu copy_start_ns=%llu copy_end_ns=%llu set_buffer_ioctl_start_ns=%llu set_buffer_ioctl_end_ns=%llu bulk_submit_start_ns=%llu bulk_submit_end_ns=%llu bulk_completion_wait_start_ns=%llu bulk_completion_wait_end_ns=%llu bulk_completion_callback_ns=%llu atomic_commit_start_ns=0 atomic_commit_end_ns=0 planner_us=%llu compression_us=%llu copy_us=%llu set_buffer_us=%llu bulk_submit_us=%llu bulk_wait_us=%llu commit_us=0 total_us=%llu set_buffer_result=%d bulk_result=%d actual_bytes=%d\n",
				 (unsigned long long)frame_seq, rectangles + 1,
				 (unsigned long long)payload_seq, transfer_length,
				 (unsigned long long)planner_start_ns,
				 (unsigned long long)planner_end_ns,
				 (unsigned long long)planner_start_ns,
				 (unsigned long long)planner_end_ns,
				 (unsigned long long)copy_start_ns,
				 (unsigned long long)copy_end_ns,
				 (unsigned long long)set_buffer_start_ns,
				 (unsigned long long)set_buffer_end_ns,
				 (unsigned long long)bulk_timing.submit_start_ns,
				 (unsigned long long)bulk_timing.submit_end_ns,
				 (unsigned long long)bulk_timing.wait_start_ns,
				 (unsigned long long)bulk_timing.wait_end_ns,
				 (unsigned long long)bulk_timing.completion_ns,
				 (unsigned long long)((planner_end_ns - planner_start_ns) / 1000),
				 (unsigned long long)((planner_end_ns - planner_start_ns) / 1000),
				 (unsigned long long)((copy_end_ns - copy_start_ns) / 1000),
				 (unsigned long long)((set_buffer_end_ns - set_buffer_start_ns) / 1000),
				 (unsigned long long)((bulk_timing.submit_end_ns - bulk_timing.submit_start_ns) / 1000),
				 (unsigned long long)((bulk_timing.wait_end_ns - bulk_timing.wait_start_ns) / 1000),
				 (unsigned long long)((ktime_get_ns() - payload_start_ns) / 1000),
				 set_buffer_ret, ret, actual);
		if (ret) {
			dev_err(&gud->intf->dev,
				"GUD bulk transfer failed after %d retries: %d\n",
				retries, ret);
			break;
		}
		if (actual != transfer_length) {
			ret = -EIO;
			break;
		}

		offset += chunk.source_length;
		total_payload += chunk.payload_length;
		max_payload = max_t(u32, max_payload,
				    chunk.payload_length);
		rectangles++;
		if (chunk.compressed)
			compressed_rects++;
		else
			raw_rects++;
		if (bounded_policy && bounded_frame.raw_backoff &&
		    !chunk.compression_attempts)
			raw_backoff_rects++;
		if (chunk.predictive_hit)
			predictive_hits++;
		if (chunk.predictive_fallback)
			predictive_fallbacks++;
		compression_attempts += chunk.compression_attempts;
		rejected_compression_attempts +=
			chunk.rejected_compression_attempts;
		compression_source_bytes +=
			chunk.compression_source_bytes;
		if (xdisp_ratio_cache && chunk.compressed) {
			if (!gud->xdisp_ratio_valid ||
			    gud->xdisp_ratio_width != plane_state->fb->width ||
			    gud->xdisp_ratio_bytes_per_line != bytes_per_line) {
				gud->xdisp_ratio_width = plane_state->fb->width;
				gud->xdisp_ratio_bytes_per_line = bytes_per_line;
				gud->xdisp_ratio_source_bytes = chunk.source_length;
				gud->xdisp_ratio_payload_bytes = chunk.payload_length;
				gud->xdisp_ratio_valid = true;
			} else {
				gud->xdisp_ratio_source_bytes =
					(3 * gud->xdisp_ratio_source_bytes +
					 chunk.source_length) / 4;
				gud->xdisp_ratio_payload_bytes =
					(3 * gud->xdisp_ratio_payload_bytes +
					 chunk.payload_length) / 4;
			}
			row_hint = gud_xdisp_next_row_hint_target(
				&chunk, bytes_per_line, max_rows,
				GUD_XDISP_PAYLOAD_LIMIT *
				GUD_XDISP_TARGET_PAYLOAD_PERCENT / 100U);
		} else if (xdisp_predictive_bounded && chunk.compressed) {
			if (!gud->xdisp_predictive_valid ||
			    gud->xdisp_predictive_width != plane_state->fb->width ||
			    gud->xdisp_predictive_bytes_per_line != bytes_per_line) {
				gud->xdisp_predictive_width = plane_state->fb->width;
				gud->xdisp_predictive_bytes_per_line = bytes_per_line;
				gud->xdisp_predictive_source_bytes = chunk.source_length;
				gud->xdisp_predictive_payload_bytes = chunk.payload_length;
				gud->xdisp_predictive_valid = true;
			} else {
				gud->xdisp_predictive_source_bytes =
					(3 * gud->xdisp_predictive_source_bytes +
					 chunk.source_length) / 4;
				gud->xdisp_predictive_payload_bytes =
					(3 * gud->xdisp_predictive_payload_bytes +
					 chunk.payload_length) / 4;
			}
			predictive_rows = gud_xdisp_next_row_hint_target(
				&chunk, bytes_per_line, max_rows,
				GUD_XDISP_DISCOVERY_TARGET_PAYLOAD);
		} else if (xdisp_target_policy)
			row_hint = gud_xdisp_next_row_hint_target(
				&chunk, bytes_per_line, max_rows,
				plan_payload_limit);
		else
			row_hint = gud_xdisp_next_row_hint(chunk.rows, max_rows);
	}
	mutex_unlock(&gud->lock);

	if (!ret && xdisp_frame_stats)
		dev_info(
			&gud->intf->dev,
			"XDISP frame format=%s bpp=%u policy=%s source=%zu payload=%zu rectangles=%u compressed=%u raw=%u raw_backoff_rectangles=%u predictive_hits=%u predictive_fallbacks=%u max_payload=%u cap=%u target=%zu compress_attempts=%llu compress_rejected=%llu compress_source_bytes=%llu planner_us=%llu copy_us=%llu set_buffer_us=%llu bulk_wait_us=%llu transfer_us=%llu\n",
			bpp == 2 ? "rgb565" : bpp == 4 ? "xrgb8888" : "unknown",
			bpp,
			xdisp_predictive_bounded ? "predictive-bounded-lz4" :
			xdisp_bounded_discovery ? "bounded-lz4" :
			xdisp_ratio_cache ? "ratio-cache" :
			xdisp_target_policy ? "target95" : "doubling",
			length, total_payload, rectangles, compressed_rects,
			raw_rects, raw_backoff_rects, predictive_hits,
			predictive_fallbacks, max_payload,
			GUD_XDISP_PAYLOAD_LIMIT,
			reported_target,
			(unsigned long long)compression_attempts,
			(unsigned long long)rejected_compression_attempts,
			(unsigned long long)compression_source_bytes,
			(unsigned long long)(planner_ns / 1000),
			(unsigned long long)(copy_ns / 1000),
			(unsigned long long)(set_buffer_ns / 1000),
			(unsigned long long)(bulk_wait_ns / 1000),
			(unsigned long long)((ktime_get_ns() - frame_start_ns) / 1000));
	else if (!ret)
		dev_info_ratelimited(
			&gud->intf->dev,
			"XDISP frame format=%s bpp=%u source=%zu payload=%zu rectangles=%u compressed=%u raw=%u raw_backoff_rectangles=%u max_payload=%u cap=%u compress_attempts=%llu compress_rejected=%llu compress_source_bytes=%llu\n",
			bpp == 2 ? "rgb565" : bpp == 4 ? "xrgb8888" : "unknown",
			bpp,
			length, total_payload, rectangles, compressed_rects,
			raw_rects, raw_backoff_rects, max_payload,
			GUD_XDISP_PAYLOAD_LIMIT,
			(unsigned long long)compression_attempts,
			(unsigned long long)rejected_compression_attempts,
			(unsigned long long)compression_source_bytes);

	return ret;
}
#endif

static int gud_pipe_transfer(struct gud_device *gud,
			     const struct drm_plane_state *plane_state)
{
#ifdef GUD_XDISP_LZ4_12800
	return gud_pipe_transfer_xdisp(gud, plane_state);
#else
	struct gud_framebuffer *gfb = to_gud_framebuffer(plane_state->fb);
	struct gud_gem_object *obj = to_gud_gem(gfb->obj);
	struct gud_set_buffer_req request;
	void *vaddr;
	void *bulk_buffer;
	dma_addr_t bulk_dma;
	struct urb *bulk_urb;
	size_t map_size;
	size_t framebuffer_offset;
	size_t bytes_per_line;
	size_t chunk_size;
	size_t max_chunk_size;
	size_t length;
	size_t offset;
	u32 rows_per_chunk;
	int actual;
	int ret;

	ret = gud_format_frame_layout(plane_state->fb->width,
				  plane_state->fb->height,
				  plane_state->fb->pixel_format,
				  &bytes_per_line, &length);
	if (ret)
		return ret;
	if (!gud->max_buffer_size)
		return -EINVAL;

	ret = gud_gem_vmap(obj, &vaddr, &map_size);
	if (ret)
		return ret;
	framebuffer_offset = plane_state->fb->offsets[0];
	ret = gud_validate_framebuffer_range(framebuffer_offset, length, map_size);
	if (ret)
		return ret;
	vaddr = (u8 *)vaddr + framebuffer_offset;
	/*
	 * vmap() memory is not necessarily DMA-addressable on this 4.9 USB host.
	 * The host sends one bulk URB per SET_BUFFER, so each DMA-coherent
	 * bounce buffer contains complete rows for exactly one buffer rectangle.
	 */
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
		u64 trace;

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
		trace = gud_bulk_trace_begin(gud);
		gud_trace_set_buffer(gud, trace, &request, chunk);
		ret = gud_usb_set(gud, GUD_REQ_SET_BUFFER, &request, sizeof(request));
		if (trace)
			dev_info(&gud->intf->dev,
				 "GUD trace=%llu SET_BUFFER result=%d\n",
				 (unsigned long long)trace, ret);
		if (ret)
			break;
		for (retries = 0; ; retries++) {
			ret = gud_usb_bulk_write(gud, bulk_urb, bulk_buffer, bulk_dma,
						 chunk, &actual, trace, retries, NULL);
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
#endif
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
	if (mode_cmd->pixel_format != DRM_FORMAT_RGB565 &&
	    mode_cmd->pixel_format != DRM_FORMAT_XRGB8888)
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

	line_bytes = (u64)mode_cmd->width * gud_bytes_per_pixel(mode_cmd->pixel_format);
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
	    plane_state->fb->pixel_format != DRM_FORMAT_RGB565 &&
	    plane_state->fb->pixel_format != DRM_FORMAT_XRGB8888)
		return -EINVAL;
	/*
	 * Full-frame Ticket 5 uploads deliberately do not repack padded rows.
	 *
	 * Pitch assumption: pitches[0] must equal width * bytes_per_pixel
	 * for both RGB565 (bpp=2) and XRGB8888 (bpp=4).  This is guaranteed
	 * for 1280-wide dumb allocations because gud_gem_dumb_create()
	 * ALIGNs pitch to 4, and 1280*2=2560 and 1280*4=5120 are both
	 * already 4-aligned.  Do not silently handle padded rows differently
	 * between formats.
	 */
	if (plane_state->fb &&
	    plane_state->fb->pitches[0] !=
	    plane_state->fb->width * gud_bytes_per_pixel(plane_state->fb->pixel_format))
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
	DRM_FORMAT_XRGB8888,
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
	#ifdef GUD_XDISP_LZ4_12800
	gud->drm->mode_config.min_width = gud->min_width;
	gud->drm->mode_config.max_width = gud->max_width;
	gud->drm->mode_config.min_height = gud->min_height;
	gud->drm->mode_config.max_height = gud->max_height;
	#else
	gud->drm->mode_config.min_width = 1280;
	gud->drm->mode_config.max_width = 1280;
	gud->drm->mode_config.min_height = 720;
	gud->drm->mode_config.max_height = 720;
	#endif
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
