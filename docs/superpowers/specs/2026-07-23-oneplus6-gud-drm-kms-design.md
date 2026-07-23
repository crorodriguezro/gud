# OnePlus 6 GUD Linux 4.9 DRM/KMS Design

## Purpose

Ticket 4 turns the existing USB-bound GUD private state and Ticket 3 local GEM
layer into one registered Linux 4.9 DRM/KMS device. It establishes the complete
DRM node, dumb-buffer, and atomic-modeset paths before Ticket 5 adds any GUD
display-control request or USB framebuffer transfer.

The temporary KMS output has one fixed `1280x720@60` mode. It is an integration
contract for the host driver, not a claim that the gadget has accepted a mode or
is displaying pixels.

## Scope

Add these files under `backport-4.9/`:

- `gud_pipe.c`: one simple-display-pipe implementation and its no-transfer
  atomic callbacks.
- `gud_connector.c`: one fixed-mode connector implementation.

Extend `gud_internal.h` with the DRM device, connector, and simple-pipe state
needed by those units. Modify `gud_drv.c` so successful USB probing initializes
and registers DRM, while disconnect unregisters it before releasing USB and
private state. Update Kbuild to link the new objects.

Ticket 4 must use only APIs exported by the exact OnePlus 6 Linux 4.9 target
kernel. It must not modify DRM core, add managed DRM helpers, or create a
second device abstraction separate from `struct gud_device`.

## DRM Device And Lifecycle

`struct gud_device` owns a `struct drm_device *drm` allocated with
`drm_dev_alloc()` and all Ticket 4 KMS objects. The DRM driver declares the
Ticket 3 callbacks:

- `gem_free_object_unlocked = gud_gem_free_object`
- `dumb_create = gud_gem_dumb_create`
- `dumb_map_offset = gud_gem_dumb_map_offset`
- `gem_vm_ops = &gud_gem_vm_ops`

Its file operations use the standard DRM open, poll, read, ioctl, release, and
seek handlers, with `gud_drm_gem_mmap()` as the mmap handler.

Probe keeps its existing USB descriptor validation. After that succeeds, it
initializes the DRM device and mode configuration, initializes the pipe and
connector, attaches the connector to the pipe encoder, then calls the Linux
4.9 DRM registration API. The DRM node is registered only after every KMS
object has initialized successfully.

Any failure after DRM allocation must unwind the objects in reverse order and
leave the existing USB error path responsible for releasing the USB reference
and `gud_device`. The implementation must not leave an accessible DRM node
after failed KMS initialization.

Disconnect first clears USB interface data and marks `gud->disconnected` under
the existing mutex. It then unregisters and releases DRM/KMS objects before
dropping the USB reference and freeing `gud_device`. Ticket 4 introduces no
workqueue, asynchronous transfer, or additional reference source. Its callbacks
must reject operations after disconnect rather than dereferencing released USB
state.

## KMS Topology

The device exposes exactly these objects:

- one `drm_simple_display_pipe`, providing one CRTC and its encoder;
- one connector attached to that encoder;
- one preferred `1280x720@60` mode;
- one framebuffer format: `DRM_FORMAT_XRGB8888`.

The connector reports connected while its USB interface remains bound and
`gud->disconnected` is false. It adds the fixed mode using local DRM mode
construction rather than reading GUD connector descriptors or EDID. The mode
uses standard 720p60 timing and is marked preferred.

The pipe advertises only XRGB8888. Its atomic check validates that the device
is still connected and, when an active framebuffer is supplied, that its pixel
format is XRGB8888. Its enable, disable, and update callbacks perform no GUD
control requests, bulk writes, or framebuffer mapping. They only provide the
Linux 4.9 simple-pipe callback behavior needed for a valid atomic modeset.

The Ticket 3 GEM layer remains the only supported backing store: 32-bpp dumb
buffers are local, page-backed GUD GEM objects. PRIME and imported dma-buf
objects remain unsupported.

## Deferred Work

Ticket 4 deliberately does not:

- query GUD connector descriptors, EDID, modes, or connection state;
- program controller/display state or perform GUD state validation;
- map a framebuffer in the pipe, issue `GUD_REQ_SET_BUFFER`, bulk-transfer
  pixels, or expose visible output;
- implement damage tracking, compression, PRIME, multiple connectors, or
  hot-unplug stress hardening beyond orderly DRM teardown.

Device-backed connector discovery and mode enumeration are deferred beyond
Ticket 5. Ticket 5 owns display-enable sequencing and the first full-frame
transfer.

## Validation

Before runtime testing, build `gud.ko` against the exact captured target kernel
tree and inspect its new unresolved references against the matching
`Module.symvers`. Extend source-level contract coverage to require the DRM
driver callbacks, one XRGB8888 format, fixed 720p60 connector mode, and no GUD
transfer calls from Ticket 4 pipe callbacks.

Hardware acceptance requires the Pi attached to the OnePlus 6 and a captured
phone `dmesg` record with no driver warning, oops, or use-after-free during the
test. It requires both of these kernel-interface tests against the new GUD DRM
node:

1. Confirm `/dev/dri/cardX` appears and enumerate one connector, one CRTC, one
   encoder, XRGB8888 support, and the preferred `1280x720@60` mode.
2. Use a phone-side DRM utility to create a `1280x720`, 32-bpp dumb buffer;
   obtain its mmap offset; map it; write XRGB8888 pixels; unmap it; destroy it;
   and execute an atomic modeset selecting the fixed mode and framebuffer.

Passing this ticket proves that the standalone module registers its DRM/KMS ABI
and that the local GEM plus atomic paths function on the target kernel. It does
not prove that GUD control requests or output pixels work.

## Acceptance

Ticket 4 is complete when the exact target kernel loads the module with the Pi
connected, exposes one GUD DRM card containing the fixed XRGB8888 720p60 KMS
topology, and passes the real-device dumb-buffer and atomic-modeset test without
kernel failure evidence. No display output is claimed until Ticket 5 has
hardware evidence of a transferred test pattern.
