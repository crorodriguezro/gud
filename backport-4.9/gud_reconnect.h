#ifndef __GUD_RECONNECT_H__
#define __GUD_RECONNECT_H__

struct usb_device;

int gud_reconnect_init(void);
void gud_reconnect_exit(void);
void gud_reconnect_arm(struct usb_device *udev);
void gud_reconnect_cancel(struct usb_device *udev);

#endif
