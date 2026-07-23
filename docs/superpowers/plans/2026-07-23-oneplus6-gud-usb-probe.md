# OnePlus 6 GUD USB Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an out-of-tree `gud.ko` that binds only to the validated Pi GUD gadget, validates its GUD v1 display descriptor, and survives repeated USB removal without DRM/KMS objects or framebuffer transfers.

**Architecture:** A laptop-side evidence script first captures the exact Pi USB descriptor and verifies the configured `1d50:614d` identity. The kernel module then replaces the Ticket 1 no-op object with a Linux 4.9 USB driver, a small protocol header, and explicit `struct gud_device` ownership. Probe obtains one USB reference, finds bulk-out, reads and validates the fixed-size descriptor, then publishes interface data; disconnect withdraws interface data before marking state disconnected and releasing it.

**Tech Stack:** Bash, `lsusb`, `usb-devices`, sysfs USB descriptors, Linux 4.9 USB core, out-of-tree Kbuild modules, GUD protocol v1, ADB over USB, and the OnePlus 6 Ubuntu Touch kernel ABI established by Ticket 1.

## Global Constraints

- Start implementation only after Ticket 1 has real-phone `insmod`/`rmmod` acceptance against the exact installed kernel ABI.
- Do not patch DRM core, modify the installed kernel or boot image, or substitute a generic OnePlus 6 kernel tree.
- Bind only the captured Pi GUD gadget VID/PID `0x1d50:0x614d`; do not add generic class matching, module parameters, or alternate IDs.
- Keep raw Pi captures, module metadata, and phone logs in ignored `backport-4.9/env/local/`.
- Use Linux 4.9 USB APIs and explicit allocation/cleanup; do not import managed DRM helpers.
- Ticket 2 must not register DRM/KMS/GEM objects, create workqueues, perform asynchronous transfers, enumerate modes, or send framebuffers.
- Treat failed descriptor capture, failed descriptor validation, kernel warnings, oopses, leak reports, and unload failures as acceptance failures; preserve evidence and stop.
- Runtime acceptance requires one observed successful phone probe, five complete detach/reattach cycles, and module unload evidence. A local build is not runtime acceptance.

---

## File Map

| File | Responsibility |
| --- | --- |
| `backport-4.9/env/capture-pi-usb.sh` | Captures a selected Pi gadget's `lsusb`, `usb-devices`, and sysfs USB descriptor evidence; refuses ambiguous or mismatched identity. |
| `backport-4.9/tests/env/test-capture-pi-usb.sh` | Hermetic mock-sysfs/mock-`lsusb` tests for the capture script. |
| `backport-4.9/gud_protocol.h` | Packed GUD v1 display-descriptor wire type and request constants used by probe only. |
| `backport-4.9/gud_internal.h` | USB-private `struct gud_device`, descriptor-derived state, and driver-private function declarations. |
| `backport-4.9/gud_drv.c` | Fixed-ID USB driver, descriptor control transfer, endpoint discovery, probe, and disconnect. |
| `backport-4.9/Kbuild` | Composes `gud.ko` from the USB driver object instead of the Ticket 1 stub. |
| `backport-4.9/env/probe-test.sh` | Deploys the probe module, records metadata/dmesg, and provides an evidence boundary for manual cable cycling. |
| `backport-4.9/tests/env/test-probe-test.sh` | Hermetic mock-ADB tests for guarded probe deployment and evidence retention. |
| `backport-4.9/tests/test-usb-probe-contract.sh` | Source-contract checks for fixed ID matching, wire layout, required validation, and forbidden Ticket 3+ APIs. |
| `backport-4.9/README.md` | Documents Pi capture, rebuild, deployment, manual cycle evidence, and Ticket 2 acceptance. |

## Task 1: Capture And Validate The Pi USB Identity

**Files:**
- Create: `backport-4.9/env/capture-pi-usb.sh`
- Create: `backport-4.9/tests/env/test-capture-pi-usb.sh`
- Modify: `backport-4.9/README.md`

**Consumes:** A known-good Pi Zero 2 W GUD gadget attached to a laptop host and the ignored `backport-4.9/env/local/` boundary.

**Produces:** `env/local/pi-usb/` evidence containing raw `lsusb -v`, `usb-devices`, binary sysfs descriptors, and a reviewed `identity.env` confirming `GUD_USB_VENDOR_ID=0x1d50` and `GUD_USB_PRODUCT_ID=0x614d`.

- [ ] **Step 1: Write the failing capture-script tests**

Create `backport-4.9/tests/env/test-capture-pi-usb.sh` with strict Bash mode, a temporary fake sysfs tree, and an `LSUSB` override. The success fixture must create `idVendor` containing `1d50`, `idProduct` containing `614d`, `busnum` containing `3`, `devnum` containing `10`, a binary `descriptors` file, and one interface directory named `3-1:1.0` containing its `descriptors` file. The mock `lsusb` must succeed for `-v -s 003:010`; `usb-devices` output may be fixed.

Include these assertions:

```bash
run_capture() {
    SYSFS_USB_ROOT="$tmp/sys/bus/usb/devices" \
    LSUSB="$tmp/lsusb" \
    USB_DEVICES="$tmp/usb-devices" \
    PI_CAPTURE_DIR="$tmp/capture" \
    bash "$repo_root/backport-4.9/env/capture-pi-usb.sh" 3-1
}

test -f "$tmp/capture/lsusb-v.txt"
test -f "$tmp/capture/usb-devices.txt"
test -f "$tmp/capture/device-descriptors.bin"
test -f "$tmp/capture/interface-3-1:1.0-descriptors.bin"
grep -qx 'GUD_USB_VENDOR_ID=0x1d50' "$tmp/capture/identity.env"
grep -qx 'GUD_USB_PRODUCT_ID=0x614d' "$tmp/capture/identity.env"
```

Add a second fixture that changes `idProduct` to `a4a0`. It must exit `2` and print exactly:

```text
selected device is not the configured Pi GUD gadget: expected 1d50:614d
```

- [ ] **Step 2: Run the capture tests to verify they fail**

Run: `bash backport-4.9/tests/env/test-capture-pi-usb.sh`

Expected: nonzero exit because `backport-4.9/env/capture-pi-usb.sh` does not exist.

- [ ] **Step 3: Implement strict selected-device capture**

Create `backport-4.9/env/capture-pi-usb.sh` with this selection and identity validation logic:

```bash
#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sysfs_root="${SYSFS_USB_ROOT:-/sys/bus/usb/devices}"
capture_dir="${PI_CAPTURE_DIR:-$script_dir/local/pi-usb}"
lsusb_bin="${LSUSB:-lsusb}"
usb_devices_bin="${USB_DEVICES:-usb-devices}"
device_name="${1:-}"

if [ -z "$device_name" ] || [ ! -d "$sysfs_root/$device_name" ]; then
    printf 'usage: %s <sysfs-usb-device-name>\n' "$0" >&2
    exit 2
fi

device_dir="$sysfs_root/$device_name"
for name in idVendor idProduct busnum devnum descriptors; do
    if [ ! -r "$device_dir/$name" ]; then
        printf 'selected USB device is missing readable %s: %s\n' "$name" "$device_dir" >&2
        exit 2
    fi
done

vendor=$(tr -d '[:space:]' < "$device_dir/idVendor")
product=$(tr -d '[:space:]' < "$device_dir/idProduct")
if [ "$vendor" != 1d50 ] || [ "$product" != 614d ]; then
    printf 'selected device is not the configured Pi GUD gadget: expected 1d50:614d\n' >&2
    exit 2
fi
```

Create the capture directory only after validation. Derive the `lsusb -s` selector with decimal-to-three-digit conversion, run `"$lsusb_bin" -v -s "$selector" > "$capture_dir/lsusb-v.txt"`, run `"$usb_devices_bin" > "$capture_dir/usb-devices.txt"`, and copy the selected device's `descriptors` as `device-descriptors.bin`. For every readable directory matching `"$sysfs_root/$device_name":*`, copy its `descriptors` file as `interface-<basename>-descriptors.bin`; exit `2` when no interface descriptor is captured.

Write `identity.env` exactly as:

```bash
GUD_USB_SYSFS_DEVICE=3-1
GUD_USB_VENDOR_ID=0x1d50
GUD_USB_PRODUCT_ID=0x614d
GUD_USB_BUSNUM=003
GUD_USB_DEVNUM=010
```

Use the actual selected name and zero-padded bus/device values in place of the example values. Mark the script executable.

- [ ] **Step 4: Run the hermetic capture tests**

Run: `bash backport-4.9/tests/env/test-capture-pi-usb.sh`

Expected: exit `0`, including the configured-identity rejection test.

- [ ] **Step 5: Document the laptop evidence command**

Add this block under a new `## Ticket 2 Pi USB Capture` heading in `backport-4.9/README.md`:

```bash
ls /sys/bus/usb/devices/
./env/capture-pi-usb.sh <selected-sysfs-usb-device-name>
cat env/local/pi-usb/identity.env
```

Document that the selected name is a device directory such as `3-1`, not an interface directory such as `3-1:1.0`; that capture must produce `1d50:614d`; and that the raw output stays ignored under `env/local/pi-usb/`.

- [ ] **Step 6: Commit Pi identity capture**

```bash
git add backport-4.9/env/capture-pi-usb.sh backport-4.9/tests/env/test-capture-pi-usb.sh backport-4.9/README.md
git commit -m "build: capture Pi GUD USB identity"
```

## Task 2: Define The Minimal GUD Probe Contract

**Files:**
- Create: `backport-4.9/gud_protocol.h`
- Create: `backport-4.9/gud_internal.h`
- Create: `backport-4.9/tests/test-usb-probe-contract.sh`

**Consumes:** The reviewed `env/local/pi-usb/identity.env` from Task 1 and Linux 4.9 kernel headers from the accepted Ticket 1 build tree.

**Produces:** Exact protocol and private-state definitions used by the USB driver, plus source-level regression checks that prevent Ticket 3+ scope from entering Ticket 2.

- [ ] **Step 1: Write the failing source-contract test**

Create `backport-4.9/tests/test-usb-probe-contract.sh` with strict Bash mode. It must fail until both headers exist and verify:

```bash
for text in \
    'GUD_DISPLAY_MAGIC' \
    'GUD_REQ_GET_DESCRIPTOR' \
    'struct gud_display_descriptor_req' \
    '} __packed;' \
    'struct gud_device' \
    'struct usb_device *usb;' \
    'struct usb_interface *intf;' \
    'struct mutex lock;' \
    'bool disconnected;'; do
    grep -qF "$text" "$repo_root/backport-4.9/gud_protocol.h" \
        "$repo_root/backport-4.9/gud_internal.h" || failures=$((failures + 1))
done
```

Reject any occurrence of these patterns in the two headers:

```text
drm_
work_struct
usb_anchor
module_param
```

- [ ] **Step 2: Run the contract test to verify it fails**

Run: `bash backport-4.9/tests/test-usb-probe-contract.sh`

Expected: nonzero exit because the two headers do not exist.

- [ ] **Step 3: Add the packed protocol header**

Create `backport-4.9/gud_protocol.h` with exactly the protocol surface needed by this ticket:

```c
#ifndef __GUD_PROTOCOL_H__
#define __GUD_PROTOCOL_H__

#include <linux/bitops.h>
#include <linux/types.h>

#define GUD_DISPLAY_MAGIC 0x1d50614d
#define GUD_PROTOCOL_VERSION 1
#define GUD_REQ_GET_DESCRIPTOR 0x01

struct gud_display_descriptor_req {
	__le32 magic;
	__u8 version;
	__le32 flags;
	__u8 compression;
	__le32 max_buffer_size;
	__le32 min_width;
	__le32 max_width;
	__le32 min_height;
	__le32 max_height;
} __packed;

#endif
```

Do not add connector, mode, state, buffer, compression, or status wire types in this task.

- [ ] **Step 4: Add USB-private state and function declarations**

Create `backport-4.9/gud_internal.h`:

```c
#ifndef __GUD_INTERNAL_H__
#define __GUD_INTERNAL_H__

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/usb.h>

struct gud_device {
	struct usb_device *usb;
	struct usb_interface *intf;
	u8 bulk_out_endpoint;
	u8 protocol_version;
	u32 max_buffer_size;
	u32 min_width;
	u32 max_width;
	u32 min_height;
	u32 max_height;
	bool disconnected;
	struct mutex lock;
};

int gud_get_display_descriptor(struct gud_device *gud);

#endif
```

Keep USB control-transfer implementation private to `gud_drv.c`; later tickets may add controlled transfer helpers after the lifetime model is proven.

- [ ] **Step 5: Run the source-contract test**

Run: `bash backport-4.9/tests/test-usb-probe-contract.sh`

Expected: exit `0`.

- [ ] **Step 6: Commit the probe contract**

```bash
git add backport-4.9/gud_protocol.h backport-4.9/gud_internal.h backport-4.9/tests/test-usb-probe-contract.sh
git commit -m "feat: define GUD USB probe contract"
```

## Task 3: Implement Fixed-ID USB Probe And Disconnect

**Files:**
- Create: `backport-4.9/gud_drv.c`
- Modify: `backport-4.9/Kbuild`
- Modify: `backport-4.9/tests/test-usb-probe-contract.sh`

**Consumes:** `struct gud_device`, `gud_display_descriptor_req`, and fixed identity `1d50:614d` from Task 2; the exact Linux 4.9 external-module build output accepted by Ticket 1.

**Produces:** A `gud.ko` USB driver that locates bulk-out, reads the GUD v1 descriptor, publishes private state only after validation, and frees it safely at disconnect.

- [ ] **Step 1: Extend the contract test for driver behavior**

Add assertions that `gud_drv.c` contains all of these literal fragments:

```bash
for text in \
    'USB_DEVICE(0x1d50, 0x614d)' \
    'MODULE_DEVICE_TABLE(usb, gud_id_table)' \
    'usb_endpoint_is_bulk_out' \
    'GUD_REQ_GET_DESCRIPTOR' \
    'USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_IN' \
    'le32_to_cpu(desc.magic) != GUD_DISPLAY_MAGIC' \
    'desc.version != GUD_PROTOCOL_VERSION' \
    'usb_set_intfdata(intf, gud);' \
    'usb_set_intfdata(intf, NULL);' \
    'usb_put_dev(gud->usb);' \
    'kfree(gud);' \
    'module_usb_driver(gud_usb_driver)'; do
    grep -qF "$text" "$repo_root/backport-4.9/gud_drv.c" || failures=$((failures + 1))
done
```

Reject these patterns in `gud_drv.c`:

```text
drm_
alloc_workqueue
INIT_WORK
queue_work
usb_submit_urb
module_param
GUD_REQ_SET_BUFFER
```

Also assert `backport-4.9/Kbuild` is exactly:

```make
obj-m += gud.o
gud-y := gud_drv.o
```

- [ ] **Step 2: Run the driver contract test to verify it fails**

Run: `bash backport-4.9/tests/test-usb-probe-contract.sh`

Expected: nonzero exit because `gud_drv.c` does not exist and Kbuild still names `gud_stub.o`.

- [ ] **Step 3: Implement descriptor retrieval and validation**

Create `backport-4.9/gud_drv.c` with this descriptor function. Use `dev_err()` for all rejection paths and preserve the exact `ret` from `usb_control_msg()` when it is negative.

```c
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
```

- [ ] **Step 4: Implement allocation, endpoint discovery, probe, and disconnect**

Append this code to `gud_drv.c`. The interface is bound only after endpoint and descriptor validation complete. The `usb_set_intfdata(intf, NULL)` ordering makes a concurrent or duplicate disconnect unable to retrieve stale private state.

```c
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
```

- [ ] **Step 5: Replace the Ticket 1 object composition**

Replace all content of `backport-4.9/Kbuild` with:

```make
obj-m += gud.o
gud-y := gud_drv.o
```

Delete `backport-4.9/gud_stub.c`; it must not be linked into a USB-driver module with its obsolete no-op load/unload messages.

- [ ] **Step 6: Run source tests and exact-kernel build verification**

Run:

```bash
bash backport-4.9/tests/test-usb-probe-contract.sh
cd backport-4.9
make MANIFEST="$PWD/env/target-manifest.env" modules
modinfo gud.ko
nm -u gud.ko
```

Expected: the contract test exits `0`; the external module build exits `0`; `modinfo` reports the selected target vermagic; and undefined symbols are only normal, matching kernel-exported module references. Stop if the target kernel lacks an API used by the driver, a required symbol is unexported, or compilation emits an unresolved interface error.

- [ ] **Step 7: Commit the USB driver skeleton**

```bash
git add backport-4.9/Kbuild backport-4.9/gud_protocol.h backport-4.9/gud_internal.h backport-4.9/gud_drv.c backport-4.9/tests/test-usb-probe-contract.sh
```

## Task 4: Deploy, Cycle, And Retain Probe Evidence

**Files:**
- Create: `backport-4.9/env/probe-test.sh`
- Create: `backport-4.9/tests/env/test-probe-test.sh`
- Modify: `backport-4.9/README.md`

**Consumes:** The exact-kernel `gud.ko` from Task 3, the accepted Ticket 1 target manifest, and reviewed Pi USB-capture evidence from Task 1.

**Produces:** A guarded ADB deployment that retains module metadata and phone logs, plus a documented five-cycle manual hardware acceptance procedure.

- [ ] **Step 1: Write failing mock-ADB deployment tests**

Create `backport-4.9/tests/env/test-probe-test.sh` using the `test-deploy-test.sh` fixture pattern. It must create a temporary manifest, empty `gud.ko`, fake kernel build directory, and fake `adb` executable.

Add these cases:

```text
already-loaded: /proc/modules begins with "gud "; expected exit 2 and "refusing to replace an already-loaded gud module"
success: dmesg after insmod contains "GUD probe complete for 1d50:614d"; expected exit 0 and module-metadata.txt plus probe-load-dmesg.txt
probe-failure: dmesg contains "unsupported GUD protocol version"; expected nonzero and probe-load-dmesg.txt remains present
```

The script under test must receive `ADB`, `MANIFEST`, `MODULE_PATH`, and `PROBE_DIR` overrides so the test never touches a device.

- [ ] **Step 2: Run the probe deployment tests to verify they fail**

Run: `bash backport-4.9/tests/env/test-probe-test.sh`

Expected: nonzero exit because `backport-4.9/env/probe-test.sh` does not exist.

- [ ] **Step 3: Implement guarded deployment and evidence capture**

Create `backport-4.9/env/probe-test.sh`. Reuse the exact one-authorized-device selection behavior from `capture-phone.sh`; require a readable manifest, a regular `gud.ko`, and an existing manifest `KERNEL_BUILD_DIR`. Require reviewed Pi evidence before touching ADB:

```bash
pi_identity="${PI_IDENTITY:-$script_dir/local/pi-usb/identity.env}"
if [ ! -f "$pi_identity" ] ||
   ! grep -qx 'GUD_USB_VENDOR_ID=0x1d50' "$pi_identity" ||
   ! grep -qx 'GUD_USB_PRODUCT_ID=0x614d' "$pi_identity"; then
    printf 'validated Pi USB identity evidence is required: %s\n' "$pi_identity" >&2
    exit 2
fi
```

Write `module-metadata.txt` using the established `modinfo`/`nm -u` block. Refuse an already-loaded module before `adb push`. Push to `/home/phablet/gud.ko`, invoke `sudo insmod /home/phablet/gud.ko`, then retain `dmesg` in `probe-load-dmesg.txt` regardless of success using an `ERR` trap.

Require the successful probe line:

```bash
grep -qF 'GUD probe complete for 1d50:614d' "$evidence_dir/probe-load-dmesg.txt"
```

Reject all logs containing:

```text
Invalid module format
Required key not available
module verification failed
BUG:
Oops
WARNING:
lockdep
use-after-free
```

Do not call `rmmod` from this script: the loaded module is intentionally left available for manual removal/reattach cycles. Print the evidence directory and the exact next command, `sudo rmmod gud`.

- [ ] **Step 4: Implement and run the mock deployment tests**

Run: `bash backport-4.9/tests/env/test-probe-test.sh`

Expected: exit `0`, including preservation of `probe-load-dmesg.txt` after the failing protocol-version scenario.

- [ ] **Step 5: Document hardware acceptance and perform it after Ticket 1 is accepted**

Append a `## Ticket 2 Phone Probe And Cable Cycle` section to `backport-4.9/README.md` with these commands:

```bash
cd backport-4.9
make MANIFEST="$PWD/env/target-manifest.env" modules
./env/probe-test.sh
adb shell dmesg -w
# Remove and reattach the Pi USB data cable five times while recording console output.
adb shell 'sudo rmmod gud'
adb shell dmesg > env/local/evidence/probe-unload-dmesg.txt
```

Require retention of `module-metadata.txt`, `probe-load-dmesg.txt`, the complete five-cycle console log as `probe-cycle-dmesg.txt`, and `probe-unload-dmesg.txt`. State that every attach must log `GUD probe complete for 1d50:614d`, every removal must log `GUD disconnected`, and none of the logs may contain the forbidden failure patterns. Do not update `BACKLOG.md` until these real-device conditions are met.

- [ ] **Step 6: Run the complete local verification suite**

Run:

```bash
bash -n backport-4.9/env/capture-pi-usb.sh
bash -n backport-4.9/env/probe-test.sh
bash backport-4.9/tests/env/test-env-scripts.sh
bash backport-4.9/tests/env/test-prepare-kernel.sh
bash backport-4.9/tests/env/test-deploy-test.sh
bash backport-4.9/tests/env/test-capture-pi-usb.sh
bash backport-4.9/tests/env/test-probe-test.sh
bash backport-4.9/tests/test-usb-probe-contract.sh
git diff --check
```

Expected: every command exits `0`. This does not replace the exact-kernel module build or real-phone cycle required for acceptance.

- [ ] **Step 7: Commit deployment and documentation**

```bash
git add backport-4.9/env/probe-test.sh backport-4.9/tests/env/test-probe-test.sh backport-4.9/README.md
git commit -m "test: add GUD USB probe evidence workflow"
```

## Plan Coverage Review

| Specification requirement | Covered by |
| --- | --- |
| Ticket 1 and reviewed Pi-capture prerequisite | Global Constraints; Task 1; Task 4 |
| Raw laptop capture of `lsusb`, `usb-devices`, and sysfs descriptors | Task 1 |
| Fixed Pi VID/PID-only binding | Global Constraints; Task 1; Task 3 |
| Packed GUD v1 descriptor definitions and explicit endian conversion | Task 2; Task 3 |
| USB-private state, bulk-out discovery, and descriptor-derived limits | Task 2; Task 3 |
| Control-transfer, malformed/short/unsupported descriptor rejection | Task 3 |
| Interface-data ordering and explicit disconnect lifetime | Task 3 |
| No DRM/KMS/GEM/workqueues/transfers or generic matching | Global Constraints; Tasks 2-3 contract tests |
| Exact-kernel module build and exported-symbol inspection | Task 3 |
| One probe, five detach/reattach cycles, clean unload, and retained evidence | Task 4 |
