#include <linux/err.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/slab.h>
#ifdef GUD_XDISP_FULL_UPDATE
#include <linux/vmalloc.h>
#endif

#include "gud_internal.h"
#include "gud_frame_layout.h"
#include "gud_protocol.h"
#include "gud_transfer_retry.h"

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

#ifdef GUD_XDISP_FULL_UPDATE
static bool gud_async_flush;
module_param_named(async_flush, gud_async_flush, bool, 0644);
MODULE_PARM_DESC(async_flush,
	"Enable asynchronous flushing [default=0]");

static bool xdisp_async_trace;
module_param_named(xdisp_async_trace, xdisp_async_trace, bool, 0644);
MODULE_PARM_DESC(xdisp_async_trace,
	"Emit upstream-style async queue and worker timing records");

static bool xdisp_frame_stats;
module_param_named(xdisp_frame_stats, xdisp_frame_stats, bool, 0644);
MODULE_PARM_DESC(xdisp_frame_stats,
	"Emit non-rate-limited per-frame XDISP transfer counters for benchmarking");

static bool xdisp_payload_timing;
module_param_named(xdisp_payload_timing, xdisp_payload_timing, bool, 0644);
MODULE_PARM_DESC(xdisp_payload_timing,
	"Emit machine-readable timing for every XDISP SET_BUFFER/bulk payload");

static bool xdisp_row_crc;
module_param_named(xdisp_row_crc, xdisp_row_crc, bool, 0644);
MODULE_PARM_DESC(xdisp_row_crc,
	"Test-only: emit CRC32 for each logical framebuffer and copied payload row");

static unsigned int xdisp_probe_payload_length;
module_param_named(xdisp_probe_payload_length, xdisp_probe_payload_length,
			   uint, 0644);
MODULE_PARM_DESC(xdisp_probe_payload_length,
	"Test-only single-payload raw bulk length (1..4194304; zero disables)");

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

static u32 gud_row_crc32(const void *data, size_t length)
{
	const u8 *bytes = data;
	u32 crc = ~0U;
	size_t index;
	unsigned int bit;

	for (index = 0; index < length; index++) {
		crc ^= bytes[index];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^
			      (0xedb88320U & (0U - (crc & 1U)));
	}

	return ~crc;
}

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
#ifdef GUD_XDISP_FULL_UPDATE
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

#ifdef GUD_XDISP_FULL_UPDATE
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

	if (!gud->max_buffer_size ||
	    gud->max_buffer_size > GUD_XDISP_MAX_PAYLOAD_LIMIT)
		return -EINVAL;
	if (bytes_per_line > gud->max_buffer_size)
		return -E2BIG;

	max_source_length = min_t(size_t, gud->max_buffer_size,
				  (size_t)gud->max_width * gud->max_height * 4U);
	/*
	 * Keep the descriptor's byte budget intact.  Rounding it to a row at
	 * max_width incorrectly limits narrower modes: 12,800 bytes rounded to
	 * a 1,920-pixel XRGB8888 row becomes 7,680 bytes, which permits only one
	 * 1,280-pixel row instead of the safe two-row 10,240-byte rectangle.
	 * The active-mode planner below performs the required complete-row floor.
	 */
	if (max_source_length < bytes_per_line)
		return -EINVAL;

	/* Upstream uses one compression buffer with the negotiated bulk length. */
	scratch_size = max_source_length;
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
		gud->usb, max_source_length, GFP_KERNEL,
		&gud->xdisp_bulk_dma);
	if (!gud->xdisp_bulk_buffer)
		goto err_scratch;

	gud->xdisp_bulk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!gud->xdisp_bulk_urb)
		goto err_bulk;

	gud->xdisp_max_source_length = max_source_length;
	gud->xdisp_bulk_buffer_size = max_source_length;
	return 0;

err_bulk:
	usb_free_coherent(gud->usb, max_source_length,
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
		usb_free_coherent(gud->usb, gud->xdisp_bulk_buffer_size,
				  gud->xdisp_bulk_buffer,
				  gud->xdisp_bulk_dma);
		gud->xdisp_bulk_buffer = NULL;
		gud->xdisp_bulk_buffer_size = 0;
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

	if (!payload || payload_length > GUD_XDISP_MAX_PAYLOAD_LIMIT ||
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
					 const struct drm_framebuffer *fb)
{
	struct gud_set_buffer_req request = { 0 };
	struct gud_bulk_timing timing = { 0 };
	u32 bpp = gud_format_bytes_per_pixel(fb->pixel_format);
	u32 width = 640;
	u32 height;
	size_t raw_length;
	int actual = 0;
	int set_buffer_result;
	int bulk_result = 0;

	if (!bpp ||
	    xdisp_probe_payload_length > gud->xdisp_bulk_buffer_size)
		return -EINVAL;
	if (xdisp_probe_pre_bulk_pause_ms > XDISP_PROBE_PRE_BULK_PAUSE_MAX_MS)
		return -EINVAL;
	/* Raw probes use complete full-width rows from the active 1280x720 mode. */
	raw_length = xdisp_probe_payload_length;
	width = fb->width;
	if (raw_length % ((size_t)width * bpp))
		return -EINVAL;
	height = raw_length / ((size_t)width * bpp);
	if (!height || height > fb->height || width > fb->width)
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
		 fb->pixel_format, xdisp_probe_zero_packet);
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

static int gud_pipe_transfer_xdisp_buffer(struct gud_device *gud,
					  struct drm_framebuffer *fb,
					  void *vaddr)
{
	size_t bytes_per_line;
	size_t length;
	size_t offset = 0;
	size_t total_payload = 0;
	u32 bpp;
	u32 compressed_rects = 0;
	u32 max_payload = 0;
	u32 max_rows;
	u32 raw_rects = 0;
	u32 rectangles = 0;
	u64 compression_attempts = 0;
	u64 compression_source_bytes = 0;
	u64 compression_ns = 0;
	u64 copy_ns = 0;
	u64 set_buffer_ns = 0;
	u64 bulk_wait_ns = 0;
	u64 frame_start_ns;
	u64 frame_seq;
	int ret;

	if (xdisp_probe_payload_length)
		return gud_pipe_transfer_xdisp_probe(gud, fb);

	bpp = gud_format_bytes_per_pixel(fb->pixel_format);
	if (!gud->max_buffer_size)
		return -EINVAL;
	ret = gud_format_frame_layout(fb->width, fb->height, fb->pixel_format,
				  &bytes_per_line, &length);
	if (ret)
		return ret;
	if (bytes_per_line > gud->xdisp_max_source_length)
		return -E2BIG;

	max_rows = min_t(size_t, gud->xdisp_max_source_length, length) /
		   bytes_per_line;
	if (!max_rows)
		return -EINVAL;
	frame_start_ns = ktime_get_ns();

	mutex_lock(&gud->lock);
	frame_seq = ++gud->xdisp_frame_sequence;
	while (offset < length) {
		struct gud_set_buffer_req request;
		const void *payload;
		u32 remaining_rows = (length - offset) / bytes_per_line;
		u32 rows = min(remaining_rows, max_rows);
		size_t source_length = (size_t)rows * bytes_per_line;
		size_t payload_length = source_length;
		size_t compressed_length = 0;
		bool compressed = false;
		int actual = 0;
		int retries;
		unsigned int set_buffer_retries;
		int set_buffer_ret;
		int transfer_length;
		u64 phase_start_ns;
		u64 trace;
		u64 payload_seq;
		u64 payload_start_ns = ktime_get_ns();
		u64 compression_start_ns;
		u64 compression_end_ns;
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

		compression_start_ns = ktime_get_ns();
		if (gud->compression & GUD_COMPRESSION_LZ4) {
			compression_attempts++;
			compression_source_bytes += source_length;
			compressed_length = gud_xdisp_lz4_compress_limited(
				(u8 *)vaddr + offset, source_length,
				gud->xdisp_lz4_scratch, source_length,
				gud->xdisp_lz4_workmem);
			if (compressed_length && compressed_length < source_length) {
				compressed = true;
				payload_length = compressed_length;
			}
		}
		compression_end_ns = ktime_get_ns();
		compression_ns += compression_end_ns - compression_start_ns;

		if (!rows || !source_length || !payload_length ||
		    source_length > gud->xdisp_max_source_length ||
		    payload_length > gud->xdisp_bulk_buffer_size) {
			ret = -EOVERFLOW;
			break;
		}
		transfer_length = payload_length;

		payload = compressed ? gud->xdisp_lz4_scratch :
			  (u8 *)vaddr + offset;
		copy_start_ns = ktime_get_ns();
		memcpy(gud->xdisp_bulk_buffer, payload, payload_length);
		copy_end_ns = ktime_get_ns();
		copy_ns += copy_end_ns - copy_start_ns;
		if (xdisp_row_crc) {
			u32 row;

			for (row = 0; row < rows; row++) {
				const u8 *source_row =
					(u8 *)vaddr + offset +
					(size_t)row * bytes_per_line;
				u32 fb_crc = gud_row_crc32(source_row,
							 bytes_per_line);

				if (compressed) {
					dev_info(
						&gud->intf->dev,
						"XDISP_ROW_CRC frame_seq=%llu row=%zu transfer_mode=lz4 fb_crc=%08x logical_payload_crc=%08x compressed_payload_crc=%08x compressed_length=%zu\n",
						(unsigned long long)frame_seq,
						offset / bytes_per_line + row,
						fb_crc, fb_crc,
						gud_row_crc32(
							gud->xdisp_bulk_buffer,
							payload_length),
						payload_length);
					break;
				}
				dev_info(
					&gud->intf->dev,
					"XDISP_ROW_CRC frame_seq=%llu row=%zu transfer_mode=raw fb_crc=%08x logical_payload_crc=%08x compressed_payload_crc=00000000 compressed_length=0\n",
					(unsigned long long)frame_seq,
					offset / bytes_per_line + row, fb_crc,
					gud_row_crc32(
						(u8 *)gud->xdisp_bulk_buffer +
						(size_t)row * bytes_per_line,
						bytes_per_line));
			}
		}

		memset(&request, 0, sizeof(request));
		request.y = cpu_to_le32(offset / bytes_per_line);
		request.width = cpu_to_le32(fb->width);
		request.height = cpu_to_le32(rows);
		request.length = cpu_to_le32(source_length);
		if (compressed) {
			request.compression = GUD_COMPRESSION_LZ4;
			request.compressed_length = cpu_to_le32(payload_length);
		}

		trace = gud_bulk_trace_begin(gud);
		payload_seq = ++gud->xdisp_payload_sequence;
		gud_trace_set_buffer(gud, trace, &request, transfer_length);
		phase_start_ns = ktime_get_ns();
		set_buffer_start_ns = phase_start_ns;
		for (set_buffer_retries = 0; ; set_buffer_retries++) {
			ret = gud_usb_set(gud, GUD_REQ_SET_BUFFER, &request,
					  sizeof(request));
			if (!gud_set_buffer_should_retry(ret,
							set_buffer_retries))
				break;
			if (trace)
				dev_info(&gud->intf->dev,
					 "GUD trace=%llu SET_BUFFER explicitly rejected busy; retry=%u/%u delay_us=%u-%u\n",
					 (unsigned long long)trace,
					 set_buffer_retries + 1,
					 GUD_SET_BUFFER_BUSY_RETRIES,
					 GUD_SET_BUFFER_BUSY_RETRY_DELAY_MIN_US,
					 GUD_SET_BUFFER_BUSY_RETRY_DELAY_MAX_US);
			usleep_range(GUD_SET_BUFFER_BUSY_RETRY_DELAY_MIN_US,
				     GUD_SET_BUFFER_BUSY_RETRY_DELAY_MAX_US);
		}
		set_buffer_ret = ret;
		set_buffer_end_ns = ktime_get_ns();
		set_buffer_ns += set_buffer_end_ns - phase_start_ns;
		if (trace)
			dev_info(&gud->intf->dev,
				 "GUD trace=%llu SET_BUFFER result=%d busy_retries=%u elapsed_us=%llu\n",
				 (unsigned long long)trace, ret,
				 set_buffer_retries,
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
				 (unsigned long long)compression_start_ns,
				 (unsigned long long)compression_end_ns,
				 (unsigned long long)compression_start_ns,
				 (unsigned long long)compression_end_ns,
				 (unsigned long long)copy_start_ns,
				 (unsigned long long)copy_end_ns,
				 (unsigned long long)set_buffer_start_ns,
				 (unsigned long long)set_buffer_end_ns,
				 (unsigned long long)bulk_timing.submit_start_ns,
				 (unsigned long long)bulk_timing.submit_end_ns,
				 (unsigned long long)bulk_timing.wait_start_ns,
				 (unsigned long long)bulk_timing.wait_end_ns,
				 (unsigned long long)bulk_timing.completion_ns,
				 (unsigned long long)((compression_end_ns - compression_start_ns) / 1000),
				 (unsigned long long)((compression_end_ns - compression_start_ns) / 1000),
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

		offset += source_length;
		total_payload += payload_length;
		max_payload = max_t(u32, max_payload, payload_length);
		rectangles++;
		if (compressed)
			compressed_rects++;
		else
			raw_rects++;
	}
	mutex_unlock(&gud->lock);

	if (!ret && xdisp_frame_stats)
		dev_info(
			&gud->intf->dev,
			"XDISP frame format=%s bpp=%u policy=upstream-full-update source=%zu payload=%zu rectangles=%u compressed=%u raw=%u max_payload=%u negotiated_capacity=%zu compress_attempts=%llu compress_source_bytes=%llu compression_us=%llu copy_us=%llu set_buffer_us=%llu bulk_wait_us=%llu transfer_us=%llu\n",
			bpp == 2 ? "rgb565" : bpp == 4 ? "xrgb8888" : "unknown",
			bpp,
			length, total_payload, rectangles, compressed_rects,
			raw_rects, max_payload, gud->xdisp_bulk_buffer_size,
			(unsigned long long)compression_attempts,
			(unsigned long long)compression_source_bytes,
			(unsigned long long)(compression_ns / 1000),
			(unsigned long long)(copy_ns / 1000),
			(unsigned long long)(set_buffer_ns / 1000),
			(unsigned long long)(bulk_wait_ns / 1000),
			(unsigned long long)((ktime_get_ns() - frame_start_ns) / 1000));
	else if (!ret)
		dev_info_ratelimited(
			&gud->intf->dev,
			"XDISP frame format=%s bpp=%u source=%zu payload=%zu rectangles=%u compressed=%u raw=%u max_payload=%u negotiated_capacity=%zu compress_attempts=%llu compress_source_bytes=%llu\n",
			bpp == 2 ? "rgb565" : bpp == 4 ? "xrgb8888" : "unknown",
			bpp,
			length, total_payload, rectangles, compressed_rects,
			raw_rects, max_payload, gud->xdisp_bulk_buffer_size,
			(unsigned long long)compression_attempts,
			(unsigned long long)compression_source_bytes);

	return ret;
}

static int gud_pipe_transfer_xdisp(struct gud_device *gud,
				   const struct drm_plane_state *plane_state)
{
	struct drm_framebuffer *fb = plane_state->fb;
	struct gud_framebuffer *gfb = to_gud_framebuffer(fb);
	struct gud_gem_object *obj = to_gud_gem(gfb->obj);
	size_t bytes_per_line;
	size_t framebuffer_offset;
	size_t length;
	size_t map_size;
	void *vaddr;
	int ret;

	ret = gud_format_frame_layout(fb->width, fb->height, fb->pixel_format,
				      &bytes_per_line, &length);
	if (ret)
		return ret;
	ret = gud_gem_vmap(obj, &vaddr, &map_size);
	if (ret)
		return ret;
	framebuffer_offset = fb->offsets[0];
	ret = gud_validate_framebuffer_range(framebuffer_offset, length, map_size);
	if (ret)
		return ret;

	return gud_pipe_transfer_xdisp_buffer(gud, fb,
					      (u8 *)vaddr + framebuffer_offset);
}

static void gud_async_clear_damage(struct gud_device *gud)
{
	gud->async_damage.x1 = INT_MAX;
	gud->async_damage.y1 = INT_MAX;
	gud->async_damage.x2 = 0;
	gud->async_damage.y2 = 0;
}

static int gud_pipe_prepare_update(struct gud_device *gud, bool display_enable)
{
	u8 enable = 1;
	u8 display = display_enable ? 1 : 0;
	int ret;

	mutex_lock(&gud->lock);
	if (gud->disconnected) {
		ret = -ENODEV;
		goto out_unlock;
	}
	ret = gud_usb_set(gud, GUD_REQ_SET_CONTROLLER_ENABLE,
			  &enable, sizeof(enable));
	if (!ret)
		ret = gud_usb_set(gud, GUD_REQ_SET_STATE_COMMIT, NULL, 0);
	if (!ret)
		ret = gud_usb_set(gud, GUD_REQ_SET_DISPLAY_ENABLE,
				  &display, sizeof(display));

out_unlock:
	mutex_unlock(&gud->lock);
	return ret;
}

static void gud_flush_work(struct work_struct *work)
{
	struct gud_device *gud = container_of(work, struct gud_device,
					      async_work);
	struct drm_framebuffer *fb;
	struct drm_rect damage;
	void *shadow_buf;
	u64 start_ns;
	u64 worker_seq;
	int ret = 0;

	start_ns = ktime_get_ns();
	mutex_lock(&gud->damage_lock);
	fb = gud->async_fb;
	gud->async_fb = NULL;
	shadow_buf = gud->shadow_buf;
	damage = gud->async_damage;
	gud_async_clear_damage(gud);
	worker_seq = ++gud->async_worker_sequence;
	mutex_unlock(&gud->damage_lock);

	if (!fb)
		return;

	ret = gud_pipe_transfer_xdisp_buffer(gud, fb, shadow_buf);
	if (ret && ret != -ENODEV && ret != -ECONNRESET &&
	    ret != -ESHUTDOWN && ret != -EPROTO)
		dev_err_ratelimited(&gud->intf->dev,
				    "Failed to flush framebuffer asynchronously: error=%d\n",
				    ret);
	if (xdisp_async_trace)
		dev_info(&gud->intf->dev,
			 "xdisp_async_worker worker_seq=%llu fb_id=%u damage=%d,%d-%d,%d result=%d elapsed_us=%llu\n",
			 (unsigned long long)worker_seq, fb->base.id,
			 damage.x1, damage.y1, damage.x2, damage.y2, ret,
			 (unsigned long long)((ktime_get_ns() - start_ns) / 1000));

	drm_framebuffer_unreference(fb);
}

void gud_async_init(struct gud_device *gud)
{
	mutex_init(&gud->damage_lock);
	INIT_WORK(&gud->async_work, gud_flush_work);
	gud_async_clear_damage(gud);
}

void gud_async_cancel(struct gud_device *gud)
{
	struct drm_framebuffer *fb;

	cancel_work_sync(&gud->async_work);
	mutex_lock(&gud->damage_lock);
	fb = gud->async_fb;
	gud->async_fb = NULL;
	gud_async_clear_damage(gud);
	vfree(gud->shadow_buf);
	gud->shadow_buf = NULL;
	gud->shadow_buf_size = 0;
	mutex_unlock(&gud->damage_lock);

	if (fb)
		drm_framebuffer_unreference(fb);
}

void gud_async_stop(struct gud_device *gud)
{
	mutex_lock(&gud->damage_lock);
	gud->async_stopping = true;
	mutex_unlock(&gud->damage_lock);
	gud_async_cancel(gud);
}

static int gud_fb_queue_damage(struct gud_device *gud,
			       struct drm_framebuffer *fb)
{
	struct gud_framebuffer *gfb = to_gud_framebuffer(fb);
	struct gud_gem_object *obj = to_gud_gem(gfb->obj);
	struct drm_framebuffer *old_fb = NULL;
	size_t bytes_per_line;
	size_t framebuffer_offset;
	size_t length;
	size_t map_size;
	void *vaddr;
	bool queued;
	u64 copy_start_ns;
	u64 submit_seq;
	int ret;

	ret = gud_format_frame_layout(fb->width, fb->height, fb->pixel_format,
				      &bytes_per_line, &length);
	if (ret)
		return ret;
	ret = gud_gem_vmap(obj, &vaddr, &map_size);
	if (ret)
		return ret;
	framebuffer_offset = fb->offsets[0];
	ret = gud_validate_framebuffer_range(framebuffer_offset, length, map_size);
	if (ret)
		return ret;
	vaddr = (u8 *)vaddr + framebuffer_offset;

	copy_start_ns = ktime_get_ns();
	mutex_lock(&gud->damage_lock);
	if (gud->async_stopping) {
		ret = -ENODEV;
		goto out_unlock;
	}
	if (!gud->shadow_buf) {
		/* Linux 4.9 has no generic vcalloc(); vzalloc is equivalent here. */
		gud->shadow_buf = vzalloc(length);
		if (!gud->shadow_buf) {
			ret = -ENOMEM;
			goto out_unlock;
		}
		gud->shadow_buf_size = length;
	}
	if (gud->shadow_buf_size != length) {
		ret = -EINVAL;
		goto out_unlock;
	}
	memcpy(gud->shadow_buf, vaddr, length);

	if (fb != gud->async_fb) {
		old_fb = gud->async_fb;
		drm_framebuffer_reference(fb);
		gud->async_fb = fb;
	}
	gud->async_damage.x1 = min_t(int, gud->async_damage.x1, 0);
	gud->async_damage.y1 = min_t(int, gud->async_damage.y1, 0);
	gud->async_damage.x2 = max_t(int, gud->async_damage.x2, fb->width);
	gud->async_damage.y2 = max_t(int, gud->async_damage.y2, fb->height);
	submit_seq = ++gud->async_submit_sequence;
	ret = 0;

out_unlock:
	mutex_unlock(&gud->damage_lock);
	if (ret)
		return ret;

	queued = queue_work(system_long_wq, &gud->async_work);
	if (xdisp_async_trace)
		dev_info(&gud->intf->dev,
			 "xdisp_async_queue submit_seq=%llu fb_id=%u bytes=%zu queued=%u copy_us=%llu pending_slots=1\n",
			 (unsigned long long)submit_seq, fb->base.id, length, queued,
			 (unsigned long long)((ktime_get_ns() - copy_start_ns) / 1000));
	if (old_fb)
		drm_framebuffer_unreference(old_fb);

	return 0;
}
#endif

static int gud_pipe_transfer(struct gud_device *gud,
			     const struct drm_plane_state *plane_state)
{
#ifdef GUD_XDISP_FULL_UPDATE
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
	struct drm_framebuffer *old_fb = pipe->plane.state ?
		pipe->plane.state->fb : NULL;

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
	if (old_fb && plane_state->fb &&
	    old_fb->pixel_format != plane_state->fb->pixel_format)
		crtc_state->mode_changed = true;
	/* Match upstream: framebuffer-only flips need no USB state check. */
	if (old_fb && plane_state->fb && !crtc_state->mode_changed &&
	    !crtc_state->connectors_changed)
		return 0;

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

#ifdef GUD_XDISP_FULL_UPDATE
	/* Match upstream: mode disable synchronously quiesces pending flush work. */
	gud_async_cancel(gud);
#endif
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
#ifndef GUD_XDISP_FULL_UPDATE
	u8 enable = 1;
	u8 display_enable = crtc->state->active ? 1 : 0;
#endif
	int ret;

	(void)plane_state;

	if (new_state->fb) {
#ifdef GUD_XDISP_FULL_UPDATE
		ret = 0;
		/*
		 * Current upstream commits controller/mode state only on CRTC
		 * enable.  The 4.9 simple-pipe hooks expose that transition here,
		 * before the first queued framebuffer, rather than on every flip.
		 */
		if (crtc->state->mode_changed || crtc->state->active_changed)
			ret = gud_pipe_prepare_update(gud, crtc->state->active);
		if (!ret && gud_async_flush) {
			ret = gud_fb_queue_damage(gud, new_state->fb);
			if (ret != -ENOMEM)
				goto update_complete;
		}
		if (!ret)
			ret = gud_pipe_transfer(gud, new_state);
#else
		mutex_lock(&gud->lock);
		if (gud_bulk_trace_limit && !gud->bulk_trace_update_emitted) {
			struct drm_framebuffer *fb = new_state->fb;

			gud->bulk_trace_update_emitted = true;
			dev_info(&gud->intf->dev,
				 "GUD update fb=%ux%u format=0x%08x pitch=%u src_raw=%d,%d,%u,%u src_px=%d,%d,%u,%u crtc=%d,%d,%u,%u old_fb=%u\n",
				 fb->width, fb->height, fb->pixel_format,
				 fb->pitches[0], new_state->src_x,
				 new_state->src_y, new_state->src_w,
				 new_state->src_h, new_state->src_x >> 16,
				 new_state->src_y >> 16, new_state->src_w >> 16,
				 new_state->src_h >> 16, new_state->crtc_x,
				 new_state->crtc_y, new_state->crtc_w,
				 new_state->crtc_h,
				 plane_state && plane_state->fb ?
				 plane_state->fb->base.id : 0);
		}
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
#endif
		if (ret)
			dev_err(&gud->intf->dev, "GUD atomic update failed: %d\n", ret);
	}


#ifdef GUD_XDISP_FULL_UPDATE
update_complete:
#endif
	/* Async mode completes after shadow copy/queue; sync mode after USB I/O. */
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
	#ifdef GUD_XDISP_FULL_UPDATE
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
