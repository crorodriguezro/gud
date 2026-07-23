# OnePlus 6 GUD Host Backport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load a standalone `gud.ko` on the OnePlus 6 Ubuntu Touch Linux 4.9 kernel and display a static 1280x720 XRGB8888 test pattern on the already-validated Raspberry Pi Zero 2 W GUD gadget.

**Architecture:** The OnePlus 6 is the USB host and owns a single GUD DRM device. The existing Pi image remains the USB gadget and HDMI endpoint; it is outside this plan. The module uses Linux 4.9 DRM primitives, an explicit GUD GEM implementation, and synchronous full-frame bulk USB transfers for the first-pixels milestone.

**Tech Stack:** Downstream OnePlus 6 Linux 4.9, arm64 cross compiler, out-of-tree Kbuild modules, DRM/KMS atomic modesetting, Linux USB core, GUD protocol v1, Raspberry Pi Zero 2 W GUD gadget.

## Global Constraints

- Do not patch DRM core unless a documented, exported-symbol audit proves a standalone module cannot work.
- Keep all host-driver source in `backport-4.9/`; use explicit allocation and cleanup rather than `drmm_*` helpers.
- Use Linux 4.9 APIs; do not import `drm_gem_shmem_*`, DRM damage helpers, or modern unplug helpers.
- Initial display format is XRGB8888 only.
- Initial transfer behavior is one synchronous, full-frame USB bulk transfer after each accepted framebuffer update.
- Initial hardware target is one externally powered Pi Zero 2 W gadget connected through the Pi USB data port and HDMI to a monitor.
- Initial validation display mode is 1280x720 at 60 Hz; 60 Hz describes scanout timing, not full-frame upload rate.
- Do not claim runtime success without `dmesg` and a photographed or observed test pattern on the target hardware.

---

## Ticket 1: Reproduce the OnePlus 6 External-Module Build

**Purpose:** Establish a repeatable arm64 build and load environment before writing driver code.

**Files:**
- Create: `backport-4.9/Makefile`
- Create: `backport-4.9/Kbuild`
- Create: `backport-4.9/gud_stub.c`
- Create: `backport-4.9/README.md`
- Modify: `BACKLOG.md`

**Consumes:** Exact phone `uname -a`, kernel source commit, `.config`, generated headers, `Module.symvers`, and matching arm64 compiler identity.

**Produces:** A documented `make -C <kernel-build-dir> M=$PWD/backport-4.9 modules` command and a trivial arm64 `gud.ko` that loads and unloads on the phone.

- [ ] Record the phone kernel release, build fingerprint, compiler version, `CONFIG_MODVERSIONS` value, and `CONFIG_MODULE_SIG` policy in `backport-4.9/README.md`.
- [ ] Add a minimal external-module Kbuild file that declares `obj-m += gud.o` and compiles `gud_stub.o` into `gud.ko`.
- [ ] Implement `gud_stub.c` with `module_init` and `module_exit` logging the module version, without registering USB or DRM objects.
- [ ] Build `gud.ko` against the exact kernel output directory; retain the complete command and compiler output in the ticket evidence.
- [ ] Inspect `modinfo gud.ko` and `nm -u gud.ko`; verify vermagic matches the phone and no unexpected unresolved symbols are introduced.
- [ ] Push the module with `adb`, load with `insmod`, collect `dmesg`, then unload with `rmmod`.
- [ ] Mark the target-build backlog items complete only when the module loads without `Invalid module format`.

**Acceptance:** The trivial module builds reproducibly and completes a load/unload cycle on the phone.

## Ticket 2: Audit and Add the USB/GUD Probe Skeleton

**Purpose:** Prove the phone can enumerate the working Pi as a GUD USB device and safely own device-private state.

**Files:**
- Create: `backport-4.9/gud_protocol.h`
- Create: `backport-4.9/gud_internal.h`
- Create: `backport-4.9/gud_drv.c`
- Modify: `backport-4.9/Makefile`
- Modify: `backport-4.9/Kbuild`
- Modify: `backport-4.9/README.md`

**Consumes:** Ticket 1 build environment and the Pi gadget USB vendor/product descriptor captured from a known-good laptop host.

**Produces:** `struct gud_device`, `struct usb_driver gud_usb_driver`, USB ID matching, descriptor reads, and a clean probe/disconnect lifecycle without DRM registration.

- [ ] Extract only GUD protocol v1 request and descriptor definitions required for device, connector, and mode discovery into `gud_protocol.h`; keep wire structures packed and use `__le16`/`__le32` fields.
- [ ] Define `struct gud_device` in `gud_internal.h` with `struct usb_device *usb`, `struct usb_interface *intf`, endpoint addresses, descriptor-derived limits, and a mutex protecting disconnect versus transfer setup.
- [ ] Register a `usb_driver` in `gud_drv.c`; bind exclusively to the Pi’s validated GUD USB ID during initial hardware bring-up.
- [ ] In probe, retain the USB device, select the interface, locate the required bulk-out endpoint, read the GUD display descriptor using `usb_control_msg`, and reject malformed lengths or unsupported protocol versions.
- [ ] In disconnect, clear the interface data, mark the device disconnected under the mutex, release the USB device reference, and free private state. Do not register workqueues in this ticket.
- [ ] Test with the Pi connected through OTG: capture `dmesg` showing one successful probe, device descriptor values, and a clean disconnect after cable removal.
- [ ] Test repeated attach/detach at least five times and retain the console output showing no kernel warning, oops, or leak report.

**Acceptance:** The module recognizes the Pi gadget, validates its descriptor, and survives repeated USB disconnects without DRM objects or queued work.

## Ticket 3: Add Linux 4.9 GEM and Dumb-Buffer Support

**Purpose:** Provide CPU-readable framebuffer memory that can later be copied to the Pi over USB.

**Files:**
- Create: `backport-4.9/gud_gem_4_9.c`
- Create: `backport-4.9/gud_compat_4_9.h`
- Modify: `backport-4.9/gud_internal.h`
- Modify: `backport-4.9/Makefile`
- Modify: `backport-4.9/Kbuild`

**Consumes:** Ticket 1 exact kernel build tree and Linux 4.9 `drivers/gpu/drm/udl/` source as the memory-management reference.

**Produces:** `struct gud_gem_object`, allocation/free callbacks, dumb-buffer creation and mmap callback contracts, and a kernel mapping helper for future XRGB8888 scanout buffers. Ticket 3 does not register a DRM device or expose userspace ioctls.

- [ ] Define `struct gud_gem_object` around `struct drm_gem_object` with allocated page storage and a cached virtual mapping; use a conversion helper with `container_of`.
- [ ] Implement object allocation with the Linux 4.9 GEM initialization API and page allocation compatible with the target kernel configuration.
- [ ] Implement object destruction that unmaps virtual memory, frees pages, releases the GEM object, and frees the wrapper in reverse acquisition order.
- [ ] Implement `dumb_create` to require 32 bpp, validate XRGB8888-compatible pitch and size overflow, return a four-byte-aligned pitch, and create a GEM handle. Format advertisement and framebuffer validation remain Ticket 4 responsibilities.
- [ ] Implement the GEM mmap callback contract through the 4.9 GEM mmap path and reject imported dma-buf objects rather than silently trying to map them. Ticket 4 attaches it when registering the DRM driver.
- [ ] Add a `gud_gem_vmap()` helper returning a cached CPU-accessible pointer and size for local GUD-owned buffers. The mapping remains until object destruction rather than being released after each caller.
- [ ] Build the module and inspect undefined symbols against `Module.symvers`; resolve all GEM dependencies using exported 4.9 interfaces only.

**Acceptance:** `gud.ko` builds the page-backed local GEM layer against the exact target ABI, every required GEM symbol is exported by the target kernel, and the documented callback contract is available for Ticket 4. Userspace allocation and mapping are deferred because no DRM node exists yet.

## Ticket 4: Register One DRM Device, Pipe, and Connector

**Purpose:** Expose the Pi gadget as one usable DRM/KMS output before transmitting pixels.

**Files:**
- Create: `backport-4.9/gud_pipe.c`
- Create: `backport-4.9/gud_connector.c`
- Modify: `backport-4.9/gud_drv.c`
- Modify: `backport-4.9/gud_internal.h`
- Modify: `backport-4.9/Makefile`
- Modify: `backport-4.9/Kbuild`

**Consumes:** Ticket 2 USB device state and Ticket 3 GEM/dumb-buffer callbacks.

**Produces:** A registered DRM device with the Ticket 3 GEM callbacks attached, one `drm_simple_display_pipe`, one connector, XRGB8888 format advertisement, and a 1280x720@60 mode.

- [ ] Initialize `struct drm_device` and `drm_mode_config` with Linux 4.9 APIs; attach the `gud_device` as driver-private state, attach Ticket 3's GEM free, dumb-create, dumb-map-offset, and mmap callbacks to the DRM driver, and register the device only after all KMS objects initialize.
- [ ] Define one local XRGB8888 format entry and advertise no other framebuffer format.
- [ ] Initialize `drm_simple_display_pipe` with atomic check and enable/update callbacks that validate a connected device and an XRGB8888 framebuffer but do not transfer pixels yet.
- [ ] Read the Pi GUD connector descriptor and expose one DRM connector. Read EDID when provided; otherwise add a fixed 1280x720@60 mode marked preferred for this milestone.
- [ ] Attach the connector to the simple-pipe encoder and implement connector detect from the GUD connection state or a stable “connected while USB-bound” result.
- [ ] On probe failure after DRM allocation, unwind every initialized KMS object before freeing USB state. On disconnect, unregister DRM before freeing `gud_device`.
- [ ] Build and load the module with the Pi attached; verify `/dev/dri/cardX` appears.
- [ ] Run `modetest -M gud -c -p` or equivalent and capture output proving one connector, one CRTC, XRGB8888 support, and the 1280x720 mode.
- [ ] Run a phone-side DRM dumb-buffer utility against the GUD card to create, map, write, and destroy a 1280x720 XRGB8888 buffer; retain its output and `dmesg` showing no kernel warning.

**Acceptance:** The Pi appears as one DRM/KMS output, and userspace can create, map, write, and destroy a 1280x720 XRGB8888 dumb buffer on the GUD card without a kernel warning.

## Ticket 5: Commit GUD State and Send the First Full Frame

**Purpose:** Turn one modeset/update into a visible 720p static pattern on the Pi HDMI monitor.

**Files:**
- Modify: `backport-4.9/gud_protocol.h`
- Modify: `backport-4.9/gud_pipe.c`
- Modify: `backport-4.9/gud_drv.c`
- Modify: `backport-4.9/gud_internal.h`
- Create: `backport-4.9/tests/gud-kms-fill.c`
- Modify: `backport-4.9/README.md`

**Consumes:** Ticket 3 CPU-mappable GEM buffers and Ticket 4 atomic pipe/connector lifecycle.

**Produces:** GUD state-check/commit requests, display enable sequencing, `GUD_REQ_SET_BUFFER`, split bulk writes constrained by the Pi descriptor limit, and a deterministic KMS test program.

- [ ] Add only the protocol structures and request IDs needed to set controller state, validate/commit display state, enable the controller, and set/send a framebuffer.
- [ ] In the pipe atomic update path, obtain the current framebuffer GEM object and reject updates when no framebuffer, unsupported pixel format, or disconnected USB device is present.
- [ ] Build the GUD state from the active CRTC mode and framebuffer pitch; issue state check, then state commit, then controller enable in protocol order. Treat a nonzero device status as an atomic-update failure and log the request/status pair.
- [ ] Map the GEM buffer for CPU access and issue `GUD_REQ_SET_BUFFER` before bulk sending its complete XRGB8888 payload.
- [ ] Split `usb_bulk_msg` calls so no request exceeds the display descriptor’s maximum buffer size; verify every completed length matches the requested chunk and abort on disconnect or error.
- [ ] Write `gud-kms-fill.c` to select the 1280x720 connector, allocate an XRGB8888 dumb buffer, paint a fixed color-bar pattern, add an FB, modeset, and keep it active for 30 seconds.
- [ ] Cross-compile the test utility for the phone or build it in the Ubuntu Touch development environment; document the exact build and invocation commands.
- [ ] Run the utility against the Pi-connected card and retain `dmesg`, the utility output, and observed monitor evidence showing the test pattern.

**Acceptance:** A 1280x720 XRGB8888 static test pattern appears through the Pi HDMI output without a driver warning or USB transfer error.

## Ticket 6: Harden Disconnect, Unload, and Failure Paths

**Purpose:** Make the first-pixels implementation safe to use around cable removal and module unloading.

**Files:**
- Modify: `backport-4.9/gud_drv.c`
- Modify: `backport-4.9/gud_pipe.c`
- Modify: `backport-4.9/gud_internal.h`
- Modify: `backport-4.9/README.md`
- Modify: `BACKLOG.md`

**Consumes:** Ticket 5 synchronous transfer and DRM/KMS lifecycle.

**Produces:** A documented lifetime model with transfer serialization, disconnect-aware callbacks, teardown ordering, and hardware stress-test evidence.

- [ ] Define the ownership and locking rule in `gud_internal.h`: probe owns allocation, USB disconnect marks the device unavailable before DRM unregister, and every transfer holds the device mutex while checking availability and issuing control/bulk requests.
- [ ] Ensure pipe callbacks return before dereferencing USB state after disconnect; do not use `drm_dev_enter()`/`drm_dev_exit()` or any unavailable modern helper.
- [ ] Ensure disconnect blocks new transfers, waits for an in-progress synchronous transfer through the mutex, unregisters the DRM device, then releases USB and GEM-related state in reverse ownership order.
- [ ] Ensure module exit deregisters the USB driver first so no new probe can occur, then verifies no driver-owned work or references remain.
- [ ] Add explicit error-path logging for control transfer failure, short bulk write, modeset while disconnected, and module unload with no Pi attached.
- [ ] Run a scripted or manual stress cycle: start the KMS test pattern, remove the USB cable during a transfer, reconnect, re-run the test, repeat ten times, then remove the Pi and run `rmmod gud`.
- [ ] Capture `dmesg` for the entire stress run and verify no oops, use-after-free, lock warning, hung task, or failed module unload occurs.
- [ ] Update `BACKLOG.md` with completion evidence and leave damage tracking, compression, multi-connector, and Lomiri integration unchecked.

**Acceptance:** Ten disconnect/reconnect cycles and final module unload complete cleanly while the driver is modeset and transferring.

## Ticket Order and Review Gates

1. Ticket 1 gates all implementation: no driver work until exact external-module loading is proven.
2. Ticket 2 gates DRM work: no KMS objects until Pi enumeration and descriptor validation are stable.
3. Ticket 3 and Ticket 4 can be reviewed independently, but Ticket 4 requires Ticket 3's GUD GEM object and callback contract before it exposes the first framebuffer ioctls.
4. Ticket 5 is the first-pixels milestone and requires Tickets 1-4.
5. Ticket 6 is required before treating the module as usable outside a controlled static test.

## Explicitly Out of Scope

- Modifying the Pi gadget image or HDMI path, because the Zero 2 W already works with a laptop host.
- Native USB-C DisplayPort Alt Mode or passive USB-C-to-HDMI adapters.
- Damage tracking, LZ4, RGB565, asynchronous USB, PRIME/dma-buf import, multiple connectors, rotation, backlight, suspend/resume hardening, and Mir/Lomiri integration.
