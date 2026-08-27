#include <linux/kernel.h>
#include <linux/slab.h>

#include "gud_internal.h"
#include "gud_protocol.h"

static enum drm_connector_status
gud_connector_detect(struct drm_connector *connector, bool force)
{
	struct gud_device *gud = container_of(connector, struct gud_device, connector);
	enum drm_connector_status status;

	(void)force;

	dev_info(&gud->intf->dev, "GUD connector: detect enter\n");
	mutex_lock(&gud->lock);
	status = gud->disconnected ? connector_status_disconnected :
				     connector_status_connected;
	mutex_unlock(&gud->lock);
	dev_info(&gud->intf->dev, "GUD connector: detect status=%d\n", status);

	return status;
}

static int gud_connector_get_modes(struct drm_connector *connector)
{
	struct gud_device *gud = container_of(connector, struct gud_device, connector);
	struct gud_display_mode_req *req_modes;
	u8 request_type = USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_IN;
	u8 ifnum = gud->intf->cur_altsetting->desc.bInterfaceNumber;
	unsigned int i, num_modes;
	int ret;

	dev_info(&gud->intf->dev, "GUD connector: get_modes enter\n");
	req_modes = kcalloc(GUD_CONNECTOR_MAX_NUM_MODES, sizeof(*req_modes),
			    GFP_KERNEL);
	if (!req_modes)
		return 0;

	mutex_lock(&gud->lock);
	if (gud->disconnected) {
		ret = -ENODEV;
	} else {
		ret = usb_control_msg(gud->usb, usb_rcvctrlpipe(gud->usb, 0),
				      GUD_REQ_GET_CONNECTOR_MODES, request_type,
				      0, ifnum, req_modes,
				      GUD_CONNECTOR_MAX_NUM_MODES * sizeof(*req_modes),
				      USB_CTRL_GET_TIMEOUT);
	}
	mutex_unlock(&gud->lock);
	if (ret <= 0)
		goto out;
	if (ret % sizeof(*req_modes)) {
		dev_err(&gud->intf->dev, "invalid GUD mode array size: %d\n", ret);
		ret = 0;
		goto out;
	}

	num_modes = ret / sizeof(*req_modes);
	for (i = 0; i < num_modes; i++) {
		struct gud_display_mode_req *req = &req_modes[i];
		struct drm_display_mode *mode;
		u32 flags = le32_to_cpu(req->flags);

		mode = drm_mode_create(connector->dev);
		if (!mode)
			break;
		mode->clock = le32_to_cpu(req->clock);
		mode->hdisplay = le16_to_cpu(req->hdisplay);
		mode->hsync_start = le16_to_cpu(req->hsync_start);
		mode->hsync_end = le16_to_cpu(req->hsync_end);
		mode->htotal = le16_to_cpu(req->htotal);
		mode->vdisplay = le16_to_cpu(req->vdisplay);
		mode->vsync_start = le16_to_cpu(req->vsync_start);
		mode->vsync_end = le16_to_cpu(req->vsync_end);
		mode->vtotal = le16_to_cpu(req->vtotal);
		mode->flags = flags & GUD_DISPLAY_MODE_FLAG_USER_MASK;
		mode->type = DRM_MODE_TYPE_DRIVER;
		if (flags & GUD_DISPLAY_MODE_FLAG_PREFERRED)
			mode->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	ret = i;
out:
	kfree(req_modes);
	dev_info(&gud->intf->dev, "GUD connector: get_modes complete count=%d\n",
		 ret < 0 ? 0 : ret);
	return ret < 0 ? 0 : ret;
}

static const struct drm_connector_helper_funcs gud_connector_helper_funcs = {
	.get_modes = gud_connector_get_modes,
};

static void gud_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs gud_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.reset = drm_atomic_helper_connector_reset,
	.detect = gud_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = gud_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

int gud_connector_init(struct gud_device *gud)
{
	int ret;

	ret = drm_connector_init(gud->drm, &gud->connector, &gud_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret)
		return ret;

	drm_connector_helper_add(&gud->connector, &gud_connector_helper_funcs);
	gud->connector.polled = 0;

	return 0;
}
