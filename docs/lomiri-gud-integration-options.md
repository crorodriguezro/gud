# Lomiri External GUD Output Options

## Context

The OnePlus 6 GUD driver has a hardware-validated DRM/KMS output at
`/dev/dri/card1`, but the Ubuntu Touch compositor does not enumerate it.
The running `lomiri-system-compositor` is Mir 1.8.2 using the
`ubports:android2` graphics platform. That platform obtains its display list
from the Android Hardware Composer (HWC), rather than by scanning every Linux
DRM card.

On the phone, Mir currently reports the built-in `LVDS` display plus
disconnected `DisplayPort` and `Virtual` outputs. `Virtual` is the wireless
display/Aethercast path exposed by the Settings UI. A separately registered
GUD DRM card is neither of those HWC outputs, so the Settings UI cannot enable
it directly.

The target is a **true independent Lomiri external display**, not a test
pattern or a mirror of the internal screen.

## Options considered

### 1. Standalone GUD KMS client

Run an application such as `gud-kms-fill` directly on `/dev/dri/card1`.

- Advantages: already works; simple; useful for driver and Pi validation.
- Limitations: it only displays application-owned content. It cannot show the
  Lomiri desktop or participate in the desktop layout.
- Decision: retain as a diagnostic tool only.

### 2. Screen-capture display bridge

Capture the finished internal Lomiri image from Mir (for example with
`mirscreencast`), then use a separate process to scale, convert to RGB565, and
submit it through GUD.

- Advantages: shortest route to showing real phone content on the Pi; keeps
  the existing compositor unchanged.
- Limitations: mirror-only. It cannot create an extended desktop, place
  windows independently, or make the Pi a configurable Lomiri output. Full
  frame transfers are also currently slow.
- Decision: rejected for the product use case. It remains a useful fallback
  diagnostic and performance baseline.

### 3. Generic native multi-DRM Mir output

Extend or replace the Mir graphics integration so Mir directly treats the GUD
card as another DRM output alongside the Android display stack.

- Advantages: architecturally direct; can support a proper extended desktop.
- Limitations: the phone uses Mir's old Android-HWC platform, not generic
  KMS. This approach must solve multi-platform display ownership plus transfer
  of Android/Adreno-rendered buffers to the CPU-readable GUD framebuffer.
  The current GUD driver intentionally has no PRIME/dma-buf import path.
- Decision: not selected as the initial implementation because it duplicates
  existing HWC external-display policy and has the largest integration scope.

### 4. Software DisplayPort (Alt-Mode) shim — selected

Extend `mir-android2-platform` to advertise a connected DisplayPort-like
output when the GUD connector is present. The shim supplies the GUD mode and
hotplug state, while its presentation path converts/copies frames for that
output into GUD buffers on `/dev/dri/card1`.

```text
Mir/Lomiri sees: Android panel + connected DisplayPort output
Actual transport: GUD DRM card -> USB -> Pi gadget -> Pi HDMI
```

- Advantages: reuses the existing Android-HWC/Mir/Lomiri external-display
  policy and gives Lomiri a genuine independent output. It avoids modifying
  the vendor HWC implementation or pretending that the Pi is electrically a
  USB-C DisplayPort Alt-Mode device.
- Limitations: pixel copies and USB transport are still required. The shim is
  a userspace compatibility layer, not zero-copy hardware Alt Mode. It must
  handle display configuration, frame conversion, reconnects, and eventually
  partial updates for responsiveness.
- Decision: **selected implementation direction**.

### 5. Modify or emulate the vendor Android Hardware Composer

Attempt to make the vendor HWC itself emit a real DisplayPort hotplug event
for GUD, or emulate USB-C DisplayPort at the kernel/HAL level.

- Advantages: would be indistinguishable from a hardware Alt-Mode display to
  the Android platform.
- Limitations: the vendor HWC and its DisplayPort stack are device-specific,
  largely proprietary, and expect actual Type-C/DisplayPort hardware. A GUD
  USB display is not that hardware. This would be high risk and difficult to
  maintain.
- Decision: rejected.

## Feasibility POC result and priority tracking

The selected shim was implemented as a deliberately rough POC in the separate
`mir-android2-platform-gud` repository. On the OnePlus it caused Lomiri to
advertise `DisplayPort-2` with a separate `1280x720` geometry next to the
internal panel. The touch cursor could move onto it, which verifies an
independent output rather than a screen mirror.

The POC is intentionally **rolled back** on the phone. It copies the Android
external buffer and submits a synchronous GUD update from the compositor commit
path. A slow or stalled USB transfer consequently freezes or severely slows the
phone UI. It also assumes `/dev/dri/card1`, which is not stable across reconnects.
Intermittent narrow/cropped external content remains unverified; it must be
treated as a geometry/layout investigation, not as proof of a simple stride
bug.

`PROJECT-STATUS.md` is the canonical board for this work. Use the shared IDs
there in all three repositories: `XDISP-P0.1` FunctionFS rebind reliability,
`XDISP-P0.2` asynchronous Mir presentation, `XDISP-P0.3` dynamic GUD
discovery/hotplug, `XDISP-P1.1` geometry and layout, and `XDISP-P2.1`
performance. Completion requires the stated acceptance evidence, not merely a
single successful session.

## Selected-path plan

1. Build and run a phone-compatible development copy of
   `mir-android2-platform`; identify its external-output and configuration
   interfaces.
2. Detect the GUD connector and expose a connected DisplayPort-like output
   with its 1280x720 mode.
3. Add a GUD-backed presentation target which copies/converts the output into
   RGB565 GUD KMS updates.
4. Verify that Lomiri can configure it as an extended display and place
   windows independently of the internal panel.
5. Add disconnect/reconnect handling, then measure frame rate and latency.
6. Add damage tracking/partial updates and other performance work after the
   functional extended-output path is proven.

## Scope and estimate

This is a userspace compositor/platform project, not a small kernel-driver
change. A feasibility spike is estimated at 3--5 days; a first independent
external desktop at 2--3 weeks; and a robust usable implementation at 3--6
weeks. The largest technical risk is presenting Android/Adreno-rendered frames
on the CPU-backed GUD output without regressing the internal display.
