# XDISP-P0.1 FunctionFS Rebind Reliability Design

## Purpose

Make the Raspberry Pi GUD FunctionFS gadget reliably accept the first bulk
framebuffer payload after either a gadget rebind or a OnePlus USB
disconnect/reconnect. This is the first blocking reliability gate for the
independent Lomiri external-display path.

## Context

The standalone OnePlus GUD driver and Pi gadget have previously completed a
full RGB565 frame transfer. In a later fresh session, the Pi could enumerate
successfully (`1d50:614d`), its UDC could report `configured`, and the
FunctionFS service could enter its bulk-reader loop without reporting a
userspace read completion. A 2026-07-25 host-URB capture established that the
OnePlus did submit and complete the first 64,000-byte bulk transfer
(`status=0`, `actual=64000`); the Pi reader nevertheless remained blocked and
the following `SET_BUFFER` control request timed out with `-110`.

This is not the previous `usb-moded` ownership conflict: the gadget can remain
configured without a competing phone USB identity. It is also not a Mir
external-display problem. The test boundary ends at the host KMS fill/smoke
tool; Mir must remain disabled while this issue is investigated.

## Ownership and interfaces

- **Primary owner:** `gud-gadget` FunctionFS lifecycle and Pi-side service.
- **Test client:** `gud` host module and its KMS fill/smoke utility.
- **Consumer blocked by this item:** `mir-android2-platform-gud` POC and its
  future asynchronous presentation path.
- **Canonical status:** `PROJECT-STATUS.md`, item `XDISP-P0.1`.
- **Executable procedure:**
  `../../../../gud-gadget/docs/XDISP-P0.1-FUNCTIONFS-REBIND-TEST.md`.

The actual GUD DRM node is dynamic (`/dev/dri/cardX`). Test tooling must
discover and record it; no P0.1 evidence may assume `card1`.

## Required behavior

For each fresh Pi gadget rebind or OnePlus reconnect:

1. The Pi publishes its FunctionFS descriptors and binds only when its DRM
   presentation state is ready.
2. The OnePlus detects `1d50:614d`, probes `gud.ko`, and exposes a live GUD
   DRM node.
3. The first KMS fill submits at least 64 KiB of RGB565 bulk data.
4. The Pi bulk reader receives that data, returns it to userspace, and presents
   the first tile; the host completes the atomic update without `-110` or a
   subsequent control-request timeout.
5. Any failure has enough Pi and host timestamps to determine whether it
   occurred before host submission, at USB endpoint delivery, or in Pi frame
   presentation.

## Invariants and non-goals

- Do not treat USB enumeration, configured UDC state, or one later successful
  frame as proof that the first-transfer path works.
- Do not use Mir, a DRM-node symlink, or manual retry of the first transfer to
  make an otherwise failing cycle pass.
- Keep Pi endpoint lifecycle explicit: ownership of the FunctionFS endpoint
  file, endpoint enable/disable, reader start/stop, and gadget bind/unbind
  must have a defined order and idempotent cleanup.
- Do not change the normal phone graphics plugin or claim external-display
  readiness from this work.
- The local Pi 1280x720 HDMI-mode preference is a separate, unverified scaling
  experiment; it is out of scope for transport reliability.

## Observability contract

The Pi service must make these events distinguishable in its logs:

- FunctionFS descriptors/strings ready;
- endpoint file opened, enabled, disabled, or closed;
- reader start/exit and the reason for exit;
- gadget bind/unbind and UDC state transitions;
- endpoint request queue/giveback where available, plus start and completion
  of the first bulk read, including byte count and error;
- first tile presentation completion or failure.

The OnePlus evidence must include the matching GUD probe, active card node,
bulk URB submit result and completion status/byte count, start of the first
update, its result, and any `-110`/control timeout. Use a shared timestamp or
record the local clock offset when correlating the two devices.

## Acceptance

`XDISP-P0.1` is **verified** only after ten numbered fresh cycles satisfy all
of the following:

- at least five cycles begin with a Pi gadget rebind and at least five begin
  with OnePlus reconnect/reboot recovery;
- every cycle completes its first 64 KiB-or-larger payload without retry;
- no host evidence contains GUD bulk/atomic `-110` timeout; and
- raw Pi service logs and focused OnePlus kernel logs are retained for every
  pass and failure.

The status may remain **blocked** while failure reproduction is intermittent.
It becomes **in progress** only when an implementation hypothesis is being
tested, and **verified** only after the complete matrix has passed.
