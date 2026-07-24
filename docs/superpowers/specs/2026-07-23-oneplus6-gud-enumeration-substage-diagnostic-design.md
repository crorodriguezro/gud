# OnePlus 6 GUD Enumeration Substage Diagnostic Design

## Purpose

`atomic-discover` resets the phone before userspace output. It bundles five
DRM resource-enumeration operations, so it cannot identify the failing ioctl.
This diagnostic splits those operations without changing `gud.ko`.

## Scope

Replace `atomic-discover` in `gud-kms-stage` with five ordered stages. Every
stage repeats the already validated client-capability, dumb-buffer, and AddFB2
setup. It advances only through its named DRM enumeration boundary and then
releases all resources normally.

## Stages

1. `resources`: call `drmModeGetResources()` only. It does not fetch a
   connector, encoder, plane, property, or mode blob.
2. `connector`: complete `resources`, call `drmModeGetConnector()` for each
   resource connector, and select one connected connector containing a
   1280x720 mode. It does not fetch an encoder or plane.
3. `encoder-crtc`: complete `connector`, call `drmModeGetEncoder()`, and
   resolve a compatible CRTC ID/index. It does not fetch plane resources.
4. `planes`: complete `encoder-crtc`, call `drmModeGetPlaneResources()` and
   `drmModeGetPlane()`, then select one compatible primary plane by querying
   only its `type` property.
5. `properties`: complete `planes`, query all connector, CRTC, and selected
   plane properties required by the later modeset. It does not create a mode
   blob, atomic request, or issue an atomic ioctl.

Each successful stage prints `stage=<name> complete` after its final boundary
returns. Each cleans up libdrm objects, framebuffer, mapped storage, dumb
object, and file descriptor in reverse acquisition order.

## Ordering And Evidence

Run stages strictly in listed order. A reset, timeout, nonzero exit, or missing
completion marker stops the sequence. Use the existing reset-safe runner, which
must first require Pi USB identity `1d50:614d`, a GUD probe record, and
`/dev/dri/card1`.

The host retains `stdout`, `stderr`, `exit`, and streamed `dmesg` evidence for
each stage. After a reset, collect persistent crash storage and next-boot PMIC
reset reason before running a later stage.

## Interpretation

- Failure in `resources` identifies DRM resource enumeration.
- Failure in `connector` identifies connector enumeration or fixed-mode
  discovery.
- Failure in `encoder-crtc` identifies encoder or CRTC retrieval.
- Failure in `planes` identifies plane resource retrieval, plane retrieval, or
  the selected plane's `type` property query.
- Failure in `properties` identifies a required connector/CRTC/plane property
  query.

Only the first failing stage may direct the next driver investigation.

## Non-Goals

- No mode blob, atomic request, test-only atomic ioctl, real atomic commit,
  GUD protocol request, USB bulk transfer, or pixel output.
- No change to GEM, DRM/KMS driver code, USB lifetime, or DRM core behavior.
- No Ticket 4 completion claim from this diagnostic work.
