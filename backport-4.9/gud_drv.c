#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "gud_internal.h"
#include "gud_protocol.h"

static const struct file_operations gud_drm_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.mmap = gud_drm_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.unlocked_ioctl = drm_ioctl,
	.release = drm_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static int gud_drm_unload(struct drm_device *drm)
{
	struct gud_device *gud = drm->dev_private;

	drm_mode_config_cleanup(drm);
	if (gud) {
#ifdef GUD_XDISP_LZ4_12800
		gud_xdisp_buffers_fini(gud);
#endif
		usb_put_dev(gud->usb);
		kfree(gud);
	}

	return 0;
}

static struct drm_driver gud_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.gem_free_object_unlocked = gud_gem_free_object,
	.gem_vm_ops = &gud_gem_vm_ops,
	.dumb_create = gud_gem_dumb_create,
	.dumb_map_offset = gud_gem_dumb_map_offset,
	.dumb_destroy = drm_gem_dumb_destroy,
	.unload = gud_drm_unload,
	.fops = &gud_drm_fops,
	.name = "gud",
	.desc = "OnePlus 6 GUD DRM backport",
	.date = "20260723",
	.major = 1,
	.minor = 0,
};

int gud_drm_init(struct gud_device *gud)
{
	int ret;

	gud->drm = drm_dev_alloc(&gud_drm_driver, &gud->intf->dev);
	if (IS_ERR(gud->drm)) {
		ret = PTR_ERR(gud->drm);
		gud->drm = NULL;
		return ret;
	}
	gud->drm->dev_private = gud;

	ret = gud_pipe_init(gud);
	if (ret)
		goto err_unref;

	ret = drm_dev_register(gud->drm, 0);
	if (ret)
		goto err_mode_config;

	return 0;

err_mode_config:
	drm_mode_config_cleanup(gud->drm);
err_unref:
	drm_dev_unref(gud->drm);
	gud->drm = NULL;
	return ret;
}

void gud_drm_fini(struct gud_device *gud)
{
	struct drm_device *drm;

	if (!gud->drm)
		return;
	drm = gud->drm;

	if (drm->registered) {
		drm_dev_unregister(drm);
		drm_dev_unref(drm);
		return;
	}

	drm_mode_config_cleanup(drm);
	drm_dev_unref(drm);
	gud->drm = NULL;
}

int gud_get_display_descriptor(struct gud_device *gud)
{
	struct gud_display_descriptor_req desc;
	u8 request_type = USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_IN;
	u8 ifnum = gud->intf->cur_altsetting->desc.bInterfaceNumber;
	int ret;

	ret = usb_control_msg(gud->usb, usb_rcvctrlpipe(gud->usb, 0),
			      GUD_REQ_GET_DESCRIPTOR, request_type, 0, ifnum,
			      &desc, sizeof(desc), USB_CTRL_GET_TIMEOUT);
	if (ret < 0) {
		dev_err(&gud->intf->dev, "display descriptor request failed: %d\n", ret);
		return ret;
	}
	if (ret != sizeof(desc)) {
		dev_err(&gud->intf->dev, "short display descriptor: %d\n", ret);
		return -EIO;
	}
	if (le32_to_cpu(desc.magic) != GUD_DISPLAY_MAGIC) {
		dev_err(&gud->intf->dev, "invalid GUD display magic: 0x%08x\n",
			le32_to_cpu(desc.magic));
		return -ENODEV;
	}
	if (desc.version != GUD_PROTOCOL_VERSION) {
		dev_err(&gud->intf->dev, "unsupported GUD protocol version: %u\n",
			desc.version);
		return -EPROTONOSUPPORT;
	}

	gud->protocol_version = desc.version;
	gud->flags = le32_to_cpu(desc.flags);
#ifdef GUD_XDISP_LZ4_12800
	gud->compression = desc.compression & GUD_COMPRESSION_LZ4;
#endif
	gud->max_buffer_size = le32_to_cpu(desc.max_buffer_size);
	gud->min_width = le32_to_cpu(desc.min_width);
	gud->max_width = le32_to_cpu(desc.max_width);
	gud->min_height = le32_to_cpu(desc.min_height);
	gud->max_height = le32_to_cpu(desc.max_height);
	if (!gud->max_buffer_size || !gud->min_width || !gud->max_width ||
	    !gud->min_height || !gud->max_height ||
	    gud->min_width > gud->max_width ||
	    gud->min_height > gud->max_height) {
		dev_err(&gud->intf->dev, "invalid GUD display limits: buffer=%u width=%u-%u height=%u-%u\n",
			gud->max_buffer_size, gud->min_width, gud->max_width,
			gud->min_height, gud->max_height);
		return -EINVAL;
	}

	dev_info(&gud->intf->dev,
		 "GUD v%u bulk-out=0x%02x buffer=%u compression=0x%02x width=%u-%u height=%u-%u\n",
		 gud->protocol_version, gud->bulk_out_endpoint, gud->max_buffer_size,
		 desc.compression, gud->min_width, gud->max_width, gud->min_height,
		 gud->max_height);
#ifdef GUD_XDISP_LZ4_12800
	dev_info(&gud->intf->dev,
		 "XDISP diagnostic variant: compression=0x%02x actual bulk payload cap=%u\n",
		 gud->compression, GUD_XDISP_PAYLOAD_LIMIT);
#endif
	return 0;
}

static int gud_probe(struct usb_interface *intf,
		     const struct usb_device_id *id)
{
	struct usb_host_interface *alt = intf->cur_altsetting;
	struct usb_host_endpoint *bulk_out;
	struct gud_device *gud;
	int i, ret;

	gud = kzalloc(sizeof(*gud), GFP_KERNEL);
	if (!gud)
		return -ENOMEM;

	gud->usb = usb_get_dev(interface_to_usbdev(intf));
	gud->intf = intf;
	mutex_init(&gud->lock);

	bulk_out = NULL;
	for (i = 0; i < alt->desc.bNumEndpoints; i++) {
		if (usb_endpoint_is_bulk_out(&alt->endpoint[i].desc)) {
			bulk_out = &alt->endpoint[i];
			break;
		}
	}
	if (!bulk_out) {
		dev_err(&intf->dev, "no bulk-out endpoint\n");
		ret = -ENODEV;
		goto err_put_usb;
	}
	gud->bulk_out_endpoint = bulk_out->desc.bEndpointAddress;

	ret = gud_get_display_descriptor(gud);
	if (ret)
		goto err_put_usb;

#ifdef GUD_XDISP_LZ4_12800
	ret = gud_xdisp_buffers_init(gud);
	if (ret)
		goto err_put_usb;
#endif

	ret = gud_drm_init(gud);
	if (ret)
		goto err_free_xdisp;

	usb_set_intfdata(intf, gud);
#ifdef GUD_XDISP_PM_TEST
	dev_warn(&intf->dev,
		 "GUD_PM_TEST enabled: runtime suspend is accepted only while no transfer holds the driver lock\n");
#endif
	dev_info(&intf->dev, "GUD probe complete for %04x:%04x\n",
		le16_to_cpu(gud->usb->descriptor.idVendor),
		le16_to_cpu(gud->usb->descriptor.idProduct));
	return 0;

err_free_xdisp:
#ifdef GUD_XDISP_LZ4_12800
	gud_xdisp_buffers_fini(gud);
#endif
err_put_usb:
	usb_put_dev(gud->usb);
	kfree(gud);
	return ret;
}

#ifdef GUD_XDISP_PM_TEST
static int gud_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct gud_device *gud = usb_get_intfdata(intf);

	if (!gud)
		return 0;
	if (!mutex_trylock(&gud->lock)) {
		dev_warn(&intf->dev,
			 "GUD_PM_TEST suspend result=-EBUSY active_transfer=1 event=%d\n",
			 message.event);
		return -EBUSY;
	}
	if (gud->disconnected) {
		mutex_unlock(&gud->lock);
		return -ENODEV;
	}
	gud->pm_suspended = true;
	mutex_unlock(&gud->lock);
	dev_info(&intf->dev,
		 "GUD_PM_TEST suspend result=0 active_transfer=0 event=%d\n",
		 message.event);
	return 0;
}

static int gud_resume(struct usb_interface *intf)
{
	struct gud_device *gud = usb_get_intfdata(intf);

	if (!gud)
		return 0;
	mutex_lock(&gud->lock);
	gud->pm_suspended = false;
	mutex_unlock(&gud->lock);
	dev_info(&intf->dev, "GUD_PM_TEST resume result=0\n");
	return 0;
}

static int gud_reset_resume(struct usb_interface *intf)
{
	dev_warn(&intf->dev, "GUD_PM_TEST reset_resume observed\n");
	return gud_resume(intf);
}
#endif

static void gud_disconnect(struct usb_interface *intf)
{
	struct gud_device *gud = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (!gud)
		return;

	mutex_lock(&gud->lock);
	gud->disconnected = true;
	mutex_unlock(&gud->lock);

	dev_info(&intf->dev, "GUD disconnected\n");
	if (gud->drm)
		drm_unplug_dev(gud->drm);
	else {
		usb_put_dev(gud->usb);
		kfree(gud);
	}
}

static const struct usb_device_id gud_id_table[] = {
	{ USB_DEVICE(0x1d50, 0x614d) },
	{ }
};
MODULE_DEVICE_TABLE(usb, gud_id_table);

static struct usb_driver gud_usb_driver = {
#ifdef GUD_XDISP_LZ4_12800
	#ifdef GUD_XDISP_PM_TEST
	.name = "gud_xdisp_lz4_12800_pmtest",
	#else
	.name = "gud_xdisp_lz4_12800",
	#endif
#else
	.name = "gud",
#endif
	.probe = gud_probe,
	.disconnect = gud_disconnect,
	.id_table = gud_id_table,
#ifdef GUD_XDISP_PM_TEST
	.suspend = gud_suspend,
	.resume = gud_resume,
	.reset_resume = gud_reset_resume,
	.supports_autosuspend = 1,
#endif
};
module_usb_driver(gud_usb_driver);

#ifdef GUD_XDISP_LZ4_12800
	#ifdef GUD_XDISP_PM_TEST
MODULE_DESCRIPTION("TEST-ONLY OnePlus 6 GUD XDISP runtime-PM diagnostic");
MODULE_VERSION("xdisp-p0.1-adaptive-12800-pmtest-v1");
	#else
MODULE_DESCRIPTION("OnePlus 6 GUD XDISP adaptive LZ4 12800-byte diagnostic");
MODULE_VERSION("xdisp-p0.1-adaptive-12800-upstream-lz4-v2");
	#endif
#else
MODULE_DESCRIPTION("OnePlus 6 GUD USB probe backport");
#endif
MODULE_LICENSE("GPL");
