#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/usb.h>
#include <linux/usb/ch11.h>
#include <linux/usb/hcd.h>
#include <linux/workqueue.h>

#include "gud_reconnect.h"

#define GUD_RECONNECT_POLL_MS		100
#define GUD_RECONNECT_STABLE_POLLS	15
#define GUD_RECONNECT_MAX_POLLS		6000

struct gud_reconnect_monitor {
	struct mutex lock;
	struct delayed_work work;
	struct usb_hcd *hcd;
	unsigned int portnum;
	unsigned int polls;
	unsigned int stable_polls;
	bool detached_observed;
	bool stopping;
};

static struct gud_reconnect_monitor gud_reconnect;

static struct usb_device *gud_reconnect_root_child(struct usb_device *udev)
{
	if (!udev || !udev->parent)
		return NULL;

	while (udev->parent && udev->parent->parent)
		udev = udev->parent;

	return udev;
}

static int gud_reconnect_port_status(struct usb_hcd *hcd,
				     unsigned int portnum, u32 *status)
{
	__le32 raw_status = 0;
	int ret;

	if (!hcd->driver || !hcd->driver->hub_control ||
	    !test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags))
		return -EAGAIN;

	ret = hcd->driver->hub_control(hcd, GetPortStatus, 0, portnum,
				       (char *)&raw_status, sizeof(raw_status));
	if (ret)
		return ret;

	*status = le32_to_cpu(raw_status);
	return 0;
}

static void gud_reconnect_work(struct work_struct *work)
{
	struct gud_reconnect_monitor *monitor =
		container_of(to_delayed_work(work),
			     struct gud_reconnect_monitor, work);
	struct usb_hcd *finished_hcd = NULL;
	unsigned int finished_port = 0;
	u32 portstatus = 0;
	bool notify = false;
	bool timeout = false;
	int ret;

	mutex_lock(&monitor->lock);
	if (!monitor->hcd || monitor->stopping)
		goto out_unlock;

	monitor->polls++;
	ret = gud_reconnect_port_status(monitor->hcd, monitor->portnum,
					&portstatus);
	if (!ret && !(portstatus & USB_PORT_STAT_CONNECTION)) {
		monitor->detached_observed = true;
		monitor->stable_polls = 0;
	} else if (!ret && monitor->detached_observed) {
		monitor->stable_polls++;
		if (monitor->stable_polls >= GUD_RECONNECT_STABLE_POLLS) {
			notify = true;
			finished_hcd = monitor->hcd;
			finished_port = monitor->portnum;
			monitor->hcd = NULL;
		}
	} else {
		monitor->stable_polls = 0;
	}

	if (!finished_hcd && monitor->polls >= GUD_RECONNECT_MAX_POLLS) {
		timeout = true;
		finished_hcd = monitor->hcd;
		finished_port = monitor->portnum;
		monitor->hcd = NULL;
	}

	if (monitor->hcd)
		schedule_delayed_work(&monitor->work,
			msecs_to_jiffies(GUD_RECONNECT_POLL_MS));

out_unlock:
	mutex_unlock(&monitor->lock);

	if (notify) {
		char port_env[32];
		char *envp[] = {
			"GUD_HOST_REATTACH=1",
			port_env,
			NULL,
		};

		snprintf(port_env, sizeof(port_env), "GUD_ROOT_PORT=%u",
			 finished_port);
		dev_info(finished_hcd->self.controller,
			 "GUD root port %u reattached stably; emitting recovery signal\n",
			 finished_port);
		if (finished_hcd->self.root_hub)
			kobject_uevent_env(&finished_hcd->self.root_hub->dev.kobj,
					   KOBJ_CHANGE, envp);
	} else if (timeout) {
		dev_warn(finished_hcd->self.controller,
			 "GUD root-port recovery monitor expired on port %u\n",
			 finished_port);
	}

	if (finished_hcd)
		usb_put_hcd(finished_hcd);
}

int gud_reconnect_init(void)
{
	mutex_init(&gud_reconnect.lock);
	INIT_DELAYED_WORK(&gud_reconnect.work, gud_reconnect_work);
	return 0;
}

void gud_reconnect_exit(void)
{
	struct usb_hcd *hcd;

	mutex_lock(&gud_reconnect.lock);
	gud_reconnect.stopping = true;
	hcd = gud_reconnect.hcd;
	gud_reconnect.hcd = NULL;
	mutex_unlock(&gud_reconnect.lock);

	cancel_delayed_work_sync(&gud_reconnect.work);
	if (hcd)
		usb_put_hcd(hcd);
}

void gud_reconnect_arm(struct usb_device *udev)
{
	struct usb_device *root_child;
	struct usb_hcd *hcd;

	root_child = gud_reconnect_root_child(udev);
	if (!root_child)
		return;

	hcd = usb_get_hcd(bus_to_hcd(udev->bus));
	if (!hcd)
		return;

	mutex_lock(&gud_reconnect.lock);
	if (gud_reconnect.stopping || gud_reconnect.hcd) {
		mutex_unlock(&gud_reconnect.lock);
		usb_put_hcd(hcd);
		return;
	}

	gud_reconnect.hcd = hcd;
	gud_reconnect.portnum = root_child->portnum;
	gud_reconnect.polls = 0;
	gud_reconnect.stable_polls = 0;
	gud_reconnect.detached_observed = false;
	schedule_delayed_work(&gud_reconnect.work, 0);
	mutex_unlock(&gud_reconnect.lock);

	dev_info(hcd->self.controller,
		 "GUD disconnect armed root-port recovery monitor on port %u\n",
		 root_child->portnum);
}

void gud_reconnect_cancel(struct usb_device *udev)
{
	struct usb_device *root_child;
	struct usb_hcd *hcd = NULL;
	struct usb_hcd *udev_hcd;

	root_child = gud_reconnect_root_child(udev);
	if (!root_child)
		return;
	udev_hcd = bus_to_hcd(udev->bus);

	mutex_lock(&gud_reconnect.lock);
	if (gud_reconnect.hcd == udev_hcd &&
	    gud_reconnect.portnum == root_child->portnum) {
		hcd = gud_reconnect.hcd;
		gud_reconnect.hcd = NULL;
	}
	mutex_unlock(&gud_reconnect.lock);

	if (!hcd)
		return;

	cancel_delayed_work_sync(&gud_reconnect.work);
	dev_info(hcd->self.controller,
		 "GUD re-enumerated; cancelled root-port recovery monitor\n");
	usb_put_hcd(hcd);
}
