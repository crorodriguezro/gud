#include <linux/kernel.h>

#include "gud_internal.h"

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
	struct drm_display_mode *mode;

	dev_info(&gud->intf->dev, "GUD connector: get_modes enter\n");
	mode = drm_mode_create(connector->dev);
	if (!mode)
		return 0;
	dev_info(&gud->intf->dev, "GUD connector: mode created\n");

	mode->clock = 74250;
	mode->hdisplay = 1280;
	mode->hsync_start = 1390;
	mode->hsync_end = 1430;
	mode->htotal = 1650;
	mode->vdisplay = 720;
	mode->vsync_start = 725;
	mode->vsync_end = 730;
	mode->vtotal = 750;
	mode->flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);
	dev_info(&gud->intf->dev, "GUD connector: mode probed\n");

	dev_info(&gud->intf->dev, "GUD connector: get_modes complete\n");
	return 1;
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
