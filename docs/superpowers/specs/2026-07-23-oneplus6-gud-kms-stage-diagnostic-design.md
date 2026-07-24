# OnePlus 6 GUD KMS Stage Diagnostic Design

## Purpose

The Ticket 4 smoke utility causes the phone to reboot before it writes output.
The reset has no persisted kernel panic, Oops, or watchdog record. This
diagnostic isolates the first DRM userspace boundary that triggers the reset
without changing `gud.ko`.

## Scope

Create a temporary phone-side diagnostic utility with four independently
invocable stages. Each stage opens the GUD primary node and cleans up every
resource it creates before exiting. The host runs one stage at a time under a
20-second timeout while a separate SSH session streams `sudo dmesg -w` directly
to an ignored local evidence file.

The Pi must enumerate as `1d50:614d`, `gud.ko` must probe successfully, and the
GUD card must exist as `/dev/dri/card1` before any stage runs.

## Stages

1. `caps`: open the card, enable `DRM_CLIENT_CAP_UNIVERSAL_PLANES`, then enable
   `DRM_CLIENT_CAP_ATOMIC`. It does not allocate a GEM object.
2. `dumb`: perform `DRM_IOCTL_MODE_CREATE_DUMB` for 1280x720 32-bpp storage,
   obtain its mmap offset, map it, write a fixed byte pattern, unmap it, and
   issue `DRM_IOCTL_MODE_DESTROY_DUMB`. It does not create a framebuffer.
3. `fb`: repeat `dumb`, then call `drmModeAddFB2()` for XRGB8888 and remove the
   framebuffer before destroying the dumb object. It does not enumerate or
   commit KMS state.
4. `atomic`: repeat `fb`, select the connected 1280x720 GUD connector, its
   compatible CRTC, and a compatible primary plane, then build and issue the
   same atomic modeset request used by `gud-kms-smoke`.

The first stage that resets the phone is the boundary to investigate. Do not
change the driver based on failures in a later stage when an earlier stage has
not completed successfully.

## Evidence

For each stage, retain local ignored files under
`backport-4.9/env/local/evidence/`:

- `drm-kms-stage-<stage>-stdout.txt`
- `drm-kms-stage-<stage>-stderr.txt`
- `drm-kms-stage-<stage>-exit.txt`
- `drm-kms-stage-<stage>-dmesg.txt`

The host must start the dmesg stream after clearing the phone kernel log and
before invoking the stage. It must not depend on files stored solely on the
phone because reboot can clear volatile paths. After every reset, retrieve
`/sys/fs/pstore` and the next boot's PMIC reset reason before running another
stage.

## Non-Goals

- No GUD protocol requests, USB bulk transfers, display enable, or pixel test.
- No change to GEM, KMS, vblank, or USB lifetime behavior.
- No Ticket 4 completion claim from a diagnostic stage alone.

## Success

The diagnostic succeeds when it identifies the first stage that does not
complete normally, with streamed kernel and userspace evidence sufficient to
attribute the next investigation to dumb-buffer mmap, framebuffer creation, or
atomic modeset setup/commit.
