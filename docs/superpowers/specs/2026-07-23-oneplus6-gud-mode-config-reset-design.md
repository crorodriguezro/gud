# OnePlus 6 GUD Mode Config Reset Design

## Purpose

`drmModeGetResources()` succeeds, but the first connector query resets the
phone before the GUD connector callbacks run. The GUD Linux 4.9 DRM setup
creates its connector, simple pipe, CRTC, primary plane, and encoder but does
not initialize their atomic state objects. Comparable Linux 4.9 atomic drivers
call `drm_mode_config_reset()` after KMS object creation and before device
registration.

## Change

Call:

```c
drm_mode_config_reset(gud->drm);
```

once in `gud_pipe_init()` after `drm_simple_display_pipe_init()` succeeds and
before the function returns success. `gud_drm_init()` then calls
`drm_dev_register()` as it already does.

The existing connector, CRTC, and plane atomic reset callbacks initialize the
state objects required by the Linux 4.9 connector-query path. No reset call is
made on partial initialization failure; that path continues directly to
`drm_mode_config_cleanup()`.

## Validation

Add a source contract requiring `drm_mode_config_reset(gud->drm)` after the
successful simple-pipe initialization block and before `return 0` in
`gud_pipe_init()`. Build against the exact target kernel, run all existing
contracts, then rerun the phone `connector` stage with the temporary connector
markers enabled.

The required result is a successful `stage=connector complete` line and a
streamed dmesg sequence containing the connector detect and get-modes markers.
No visible pixels or GUD USB transfer is expected.

## Non-Goals

- No DRM core patch, vblank change, atomic commit change, USB change, or GUD
  protocol request.
- No mode, format, GEM, framebuffer, or connector behavior change.
- No Ticket 4 completion claim until the remaining atomic stages pass.
