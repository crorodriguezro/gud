# OnePlus 6 GUD USB Probe Design

## Purpose

Ticket 2 proves that the OnePlus 6 can enumerate the known Raspberry Pi Zero 2 W GUD gadget, validate the device's GUD display descriptor, and tear down USB-private state safely. It deliberately does not create DRM/KMS objects or transmit framebuffer data.

## Preconditions

Ticket 2 may begin only after Ticket 1 has completed a real-phone `insmod` and `rmmod` cycle using the exact installed OnePlus 6 kernel ABI.

Before the host driver is changed, capture the known-good Pi gadget's USB identity from a laptop host. A new `backport-4.9/env/capture-pi-usb.sh` script will require a selected USB device and retain raw `lsusb -v`, `usb-devices`, and relevant sysfs device/interface descriptor data under ignored local evidence storage. It must fail rather than guess when it cannot identify one selected Pi device or cannot read required descriptor data.

The reviewed capture supplies the fixed vendor and product IDs for the first `gud.ko` USB ID table. The driver must bind only to that verified Pi VID/PID in this ticket; it must not attempt generic class/interface matching or expose a runtime VID/PID parameter.

## Driver Structure

`backport-4.9/gud_protocol.h` contains only the packed GUD protocol v1 requests and descriptor structures needed to retrieve the display descriptor and establish the information required by later connector and mode discovery. Wire-format multi-byte fields use `__le16` or `__le32`. Host code converts values explicitly before validation or use.

`backport-4.9/gud_internal.h` defines `struct gud_device`, owned by USB probe until disconnect completes. It contains:

- `struct usb_device *usb` retained with `usb_get_dev()`.
- `struct usb_interface *intf` for the bound interface.
- The required bulk-out endpoint address.
- Descriptor-derived protocol and transfer-limit information required by subsequent tickets.
- A `bool disconnected` state and one mutex to serialize disconnect against future control or bulk transfer setup.

`backport-4.9/gud_drv.c` registers `struct usb_driver gud_usb_driver` using the reviewed fixed VID/PID table. Probe allocates private state, retains the USB device, records interface data, discovers the required bulk-out endpoint, retrieves the GUD display descriptor with `usb_control_msg()`, validates it, and completes without registering DRM, GEM, KMS, workqueues, sysfs objects, or data transfers.

Disconnect calls `usb_set_intfdata(intf, NULL)` first so later lookups cannot obtain stale private state. Under the device mutex it marks the device unavailable. It then releases the USB reference and frees private memory. The ticket contains no queued work and no asynchronous transfer, so disconnect must not wait for driver-owned work.

The external Kbuild composition changes from the Ticket 1 stub-only object to include the USB driver sources. It remains an out-of-tree `gud.ko` built through the accepted Ticket 1 kernel output tree.

## Failure Rules

Probe must fail and unwind all acquired resources when any of the following occurs:

- Allocation or USB-device-reference acquisition fails.
- The selected interface has no usable bulk-out endpoint.
- The display-descriptor control request fails or returns a short response.
- The descriptor reports an unsupported GUD protocol version.
- A descriptor length, capability value, or maximum transfer size is malformed, zero where invalid, or incompatible with the local structure.

The failure path must clear any installed interface data, release the USB device reference exactly once, and free private state. Logging must identify the rejected operation and relevant descriptor value without dumping arbitrary device memory.

## Verification And Acceptance

Local verification uses the exact Ticket 1 kernel build output to build `gud.ko`, inspect `modinfo`, and inspect undefined symbols against the matching exported-symbol information. The source must use only Linux 4.9 USB and core interfaces exported to external modules.

Hardware verification occurs with the Pi gadget attached through the phone's OTG host connection:

1. Retain the reviewed laptop USB-capture evidence that produced the fixed VID/PID.
2. Load `gud.ko` on the phone and retain `dmesg` showing one successful probe, the fixed USB identity, selected bulk-out endpoint, and validated display-descriptor values.
3. Remove and reattach the Pi at least five times, retaining console output for every cycle.
4. Verify the complete log has no kernel warning, oops, lock warning, leak report, failed disconnect cleanup, or stale-interface-data symptom.
5. Unload the module after the final removal and retain the unload evidence.

Ticket 2 is accepted only when all steps succeed. `BACKLOG.md` is updated only with evidence-backed completion after hardware acceptance.

## Out Of Scope

Ticket 2 must not add DRM device registration, DRM/KMS connector or pipe objects, GEM/framebuffer allocation, mode enumeration, `GUD_REQ_SET_BUFFER`, framebuffer uploads, workqueues, asynchronous USB transfers, generic GUD matching, or configurable USB IDs. Those concerns remain in later tickets.
