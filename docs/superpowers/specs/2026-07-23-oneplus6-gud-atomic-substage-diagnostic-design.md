# OnePlus 6 GUD Atomic Substage Diagnostic Design

## Purpose

The first staged diagnostic proved that GUD probe, DRM client capabilities,
local dumb-buffer mapping, and XRGB8888 framebuffer creation are safe on the
OnePlus 6. The `atomic` stage resets the phone before userspace output. This
diagnostic separates its userspace discovery, request construction, kernel
atomic validation, and real state commit without changing `gud.ko`.

## Scope

Extend `gud-kms-stage` with four ordered substages. Every substage repeats the
already validated caps, dumb-buffer, and AddFB2 setup, then stops at one new
atomic boundary. Each run is isolated by the existing reset-safe host runner.

## Substages

1. `atomic-discover`: enumerate DRM resources, select the connected 1280x720
   connector, compatible encoder and CRTC, compatible primary plane, and look
   up every property ID required by the eventual modeset. It does not create a
   mode blob or atomic request.
2. `atomic-build`: complete `atomic-discover`, create the mode property blob,
   allocate `drmModeAtomicReq`, and add all connector, CRTC, and plane property
   values. It frees the request and blob without issuing an atomic ioctl.
3. `atomic-test`: complete `atomic-build`, then call
   `drmModeAtomicCommit()` with `DRM_MODE_ATOMIC_TEST_ONLY |
   DRM_MODE_ATOMIC_ALLOW_MODESET`. This executes Linux 4.9's property-setting
   and atomic-check path without applying display state.
4. `atomic-commit`: complete `atomic-build`, then call
   `drmModeAtomicCommit()` with `DRM_MODE_ATOMIC_ALLOW_MODESET`. This is the
   first substage that applies the KMS state.

Every completed substage prints `stage=<name> complete` only after its named
boundary returns successfully. All cleanup remains reverse order: atomic
request, mode blob, framebuffer, mapping, dumb object, libdrm objects, file
descriptor.

## Evidence And Ordering

Run the substages strictly in the order above. A reset, timeout, nonzero exit,
or missing completion line stops the sequence. Retain the existing four
host-side evidence files for each substage under `env/local/evidence/`.

Before each run, require Pi enumeration as `1d50:614d`, a successful GUD probe,
and `/dev/dri/card1`. After a reset, collect pstore and next-boot PMIC reset
reason before any later substage. Do not rely on phone-local files.

## Interpretation

- Failure in `atomic-discover` identifies a DRM resource/property enumeration
  boundary.
- Failure in `atomic-build` identifies libdrm mode-blob or atomic-request
  construction.
- Failure in `atomic-test` identifies the Linux 4.9 atomic property/check
  path, before state application.
- Failure in `atomic-commit` identifies the driver's atomic commit path after
  successful validation.

Only the first failing substage may motivate the next driver investigation.

## Non-Goals

- No GUD protocol request, USB bulk transfer, pixel output, or vblank change.
- No modification to GEM, KMS, USB lifetime, or DRM core behavior.
- No Ticket 4 completion claim from a diagnostic result.
