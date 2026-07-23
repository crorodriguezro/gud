#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "gud_internal.h"
#include "gud_protocol.h"

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
		 "GUD v%u bulk-out=0x%02x buffer=%u width=%u-%u height=%u-%u\n",
		 gud->protocol_version, gud->bulk_out_endpoint, gud->max_buffer_size,
		 gud->min_width, gud->max_width, gud->min_height, gud->max_height);
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

	usb_set_intfdata(intf, gud);
	dev_info(&intf->dev, "GUD probe complete for %04x:%04x\n",
		le16_to_cpu(gud->usb->descriptor.idVendor),
		le16_to_cpu(gud->usb->descriptor.idProduct));
	return 0;

err_put_usb:
	usb_put_dev(gud->usb);
	kfree(gud);
	return ret;
}

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
	usb_put_dev(gud->usb);
	kfree(gud);
}

static const struct usb_device_id gud_id_table[] = {
	{ USB_DEVICE(0x1d50, 0x614d) },
	{ }
};
MODULE_DEVICE_TABLE(usb, gud_id_table);

static struct usb_driver gud_usb_driver = {
	.name = "gud",
	.probe = gud_probe,
	.disconnect = gud_disconnect,
	.id_table = gud_id_table,
};
module_usb_driver(gud_usb_driver);

MODULE_DESCRIPTION("OnePlus 6 GUD USB probe backport");
MODULE_LICENSE("GPL");
