# Codex Instructions — Linux 4.9 GUD Backport

## Mission

Backport the host-side Generic USB Display (GUD) DRM driver to the OnePlus 6 Ubuntu Touch Linux 4.9 kernel as a standalone loadable module (`gud.ko`) wherever technically possible.

Read `LINUX-4.9-BACKPORT.md` and `BACKLOG.md` before making changes.

## Primary constraint

Do **not** patch DRM core merely to make modern GUD compile unless a concrete blocker proves that a standalone module cannot work.

Prefer local compatibility code and small 4.9-native replacements over importing large pieces of newer DRM infrastructure.

## MVP priorities

1. Reproducible arm64 module build against the exact target kernel.
2. USB probe and GUD descriptor parsing.
3. Single DRM device, single connector, single simple display pipe.
4. XRGB8888 only if necessary.
5. CPU-readable GEM/framebuffer storage using Linux 4.9-native APIs.
6. Full-frame transfers before damage tracking.
7. First pixels on a GUD display.
8. Safe disconnect/unload behavior.

Do not spend time on LZ4, rotation, TV properties, backlight, PRIME optimization, multiple connectors, or damage tracking until the MVP produces pixels.

## Kernel compatibility strategy

Use Linux 4.9 APIs directly where possible.

For missing modern convenience APIs (`drmm_*`, managed DRM allocation, newer logging helpers, etc.), use explicit 4.9-era allocation and cleanup rather than backporting the helper subsystem.

For framebuffer memory, use the Linux 4.9 `udl` DisplayLink DRM driver as the main architectural reference. The GUD driver needs CPU-readable framebuffer memory that can be sent over USB, which is a similar requirement.

Do not assume modern `drm_gem_shmem_*`, modern DRM damage helpers, or modern managed-resource APIs exist.

## Development style

- Keep compatibility code explicit and easy to delete later.
- Prefer a `gud_compat_4_9.h` / `gud_gem_4_9.c` boundary over scattering kernel-version conditionals everywhere.
- Keep protocol logic as close to upstream GUD behavior as practical.
- Add comments where behavior intentionally differs from upstream because of Linux 4.9 limitations.
- Treat USB disconnect and queued work lifetime as correctness-critical.
- Avoid cosmetic refactors while the driver does not yet produce pixels.

## Build expectations

The intended development environment is a Linux host or Codex cloud environment with:

- target OnePlus 6 kernel source
- exact target `.config`
- matching generated kernel headers
- matching `Module.symvers` when required
- arm64 cross compiler/toolchain

The phone is not required for normal compile iterations.

## Hardware-test boundary

Do not claim runtime success based only on compilation.

Before every phone/Pi hardware-test session, read and follow
`docs/oneplus6-usb-host-gud-troubleshooting.md`. Its start-of-session USB gate
is mandatory: force the phone controller to `host`, then poll every
`/sys/bus/usb/devices/*/idVendor` path until `1d50:614d` is found. Do not
hardcode a USB topology; validated sessions have assigned the Pi both `1-1.2`
and `1-1.4`. Do not declare enumeration failure from one empty scan, and do not
let an unrelated sudo check short-circuit the VID/PID scan.

Only after the gate prints `FOUND:` may a session load `gud.ko` or run the KMS
stages. Use the checked-in `env/kms-stage-test.sh` loop in the troubleshooting
document so every stage rechecks the Pi, reloads the intended module, and
captures fresh evidence.

Runtime milestones require testing on the real OnePlus 6 and GUD hardware. Expected manual/local workflow:

```bash
adb push gud.ko /home/phablet/
adb shell
sudo insmod /home/phablet/gud.ko
dmesg -w
```

A successful compile is only the build milestone. First-pixels, hot-unplug, and Lomiri integration require hardware evidence.

## Verification order

For each meaningful step:

1. Build the module.
2. Inspect unresolved symbols/module metadata.
3. Keep the source warning-clean where practical for the old kernel toolchain.
4. For runtime changes, collect `dmesg` evidence from the phone.
5. Test DRM/KMS independently before debugging Lomiri.

## Success definition

The MVP is complete when the stock OnePlus 6 Ubuntu Touch Linux 4.9 kernel can load the out-of-tree `gud.ko`, expose the GUD adapter as a DRM/KMS output, and display a test framebuffer on an external monitor without requiring a replacement kernel image.
