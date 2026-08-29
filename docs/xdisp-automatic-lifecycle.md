# Automatic GUD display lifecycle

Status: implementation complete; 2026-08-29 hardware qualification is
partially complete. Startup activation, automatic add/remove, same-boot
reconnect, and bounded teardown passed with the staged ARM64 artifact. The
fresh reconnect run reached an active presenter, but its second Mir topology
snapshot did not report the virtual output as connected; fresh qtmir/Lomiri
observer logs and the full ten-cycle/card-number matrix remain open.

## Selected design

The production MVP uses Option A from the lifecycle task: `xdispd` is a small,
always-running system service. It blocks in GLib's main loop on a libudev DRM
monitor and performs one bounded initial enumeration after the monitor is
armed. When no GUD card is present it owns no Mir client, virtual output,
screencast, presenter, compression work, or USB work.

The service, rather than udev, owns the complete lifecycle:

```text
DRM/udev add or startup scan
    -> xdispd selects the live GUD card
    -> managed mirgud child opens a validated inherited DRM fd
    -> Mir public screencast API, extend mode, creates the virtual output
    -> Mir display configuration notification
    -> qtmir ScreensModel update
    -> Lomiri reacts to the additional screen

DRM/udev remove
    -> xdispd invalidates the exact card identity
    -> SIGTERM and bounded child containment
    -> presenter/KMS cleanup in mirgud
    -> screencast/virtual-output release
    -> Mir display configuration notification
    -> qtmir removes the screen
```

No root udev rule launches a Mir client. The systemd service is ordered after
`lightdm.service`; only its lifecycle owner starts the managed child, using the
published `/run/mir_socket` and the existing `MIR_*_PLATFORM_PATH` environment.

## Discovery and stale-device protection

`xdispd` enumerates the DRM subsystem and inspects only primary `cardN` nodes.
It identifies GUD by the DRM driver name, requires a connected 1280x720 mode,
and records the card's udev sysfs identity and device number. Before spawning
`mirgud`, it reopens the node, verifies `fstat().st_rdev`, and verifies that
udev resolves the same sysfs identity. Card numbering is never used as a
selection policy; `/dev/dri/card1` appears only in diagnostic evidence.

Duplicate add/change events are reconciled against the identity and connector.
Remove/add while the old child is being contained leaves one owner and starts a
fresh child only after the old child has exited. The lifecycle's activation
intent survives physical removal, while `Deactivate` clears it.

## Mir and qtmir boundary

Mir 1.8.3 has no public client-side `create_virtual_output()` function. The
existing public API path is the Aethercast-compatible screencast path:
`mir_connect_sync("/run/mir_socket")`, `mir_screencast_spec_set_mirror_mode`,
and `mir_screencast_create_sync`. The Android2 server's existing public
`VirtualOutput` implementation supplies the virtual-output configuration and
hotplug callback; it was not modified for this lifecycle task.

Retained evidence already demonstrates the normal downstream path: the
managed run records virtual output 3 becoming connected at 1280x720, while the
archived phone service interval records Mir display configuration delivery,
`MirDisplayConfigurationObserver::configuration_applied`, qtmir
`ScreensModel::updateInternal()`, and creation/removal of the 1280x720 screen.
The new lifecycle code does not add a Lomiri rescan, compositor restart, or
Mir restart.

## Persistence and operator status

First install is enabled by default. `Disable` creates the explicit
`/home/phablet/.local/share/lomiri-xdisp/disabled` marker; `Enable` removes it
and retains the legacy `enabled` marker. `xdisp-status` reports the current
GUD identity, bridge state, managed child, virtual-output/presenter state, and
last lifecycle event without enumerating DRM devices.

## Runtime manifest

Phone:

- `/lib/modules/<uname -r>/gud.ko` and `modules-load.d/gud.conf`;
- `/usr/libexec/lomiri-xdisp/xdispd`;
- `/usr/libexec/lomiri-xdisp/mirgud`;
- `/usr/bin/xdisp-status`;
- `xdisp.service` enabled for `graphical.target`;
- D-Bus activation and policy files for `org.lomiri.XDisp`;
- `/home/phablet/.local/share/lomiri-xdisp/` state directory;
- Mir client/server platform paths matching the installed Mir ABI.

Pi:

- the `gud-userspace.service` FunctionFS gadget service;
- configfs gadget identity `1d50:614d`;
- UDC binding `3f980000.usb`;
- the pinned GUD gadget/DRM userspace and its VC4/HDMI runtime.

The Pi service and USB role recovery remain below the Mir lifecycle boundary.
The OnePlus troubleshooting gate must still verify dynamic VID/PID
`1d50:614d`, Pi UDC `configured`, a fresh GUD probe, and the resulting DRM
card before a KMS or display test.

## Qualification status

Offline lifecycle tests cover automatic activation, idempotent add events,
re-add during bounded teardown, explicit deactivation, poison containment, and
the existing presenter/child termination behavior. The reproducible ARM64
container build is documented in the Mir repository and passed the complete
five-target Debian test suite (including 53 xdisp tests). Because the phone
rootfs is immutable, this qualification used staged binaries under
`/home/phablet/`; the shipped package paths remain listed in the runtime
manifest.

The current hardware result is recorded in
`gud-gadget/evidence/xdisp-auto-lifecycle-20260829T1418Z/`. E3-T02 and E3-T03
are hardware-verified for the tested startup/add/remove/reconnect paths. E3-T04
is partial until a fresh qtmir/Lomiri observer capture confirms the reconnect
configuration, and E3-T05 is partial because ten cycles and a forced
card-number change were not run. The Pi cleanup service restart also requires
the physical HDMI sink to be connected; its final failed restart was caused by
`HDMI-A-1 disconnected`, not by the phone lifecycle service.
