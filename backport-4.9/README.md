# GUD Backport — OnePlus 6 External Module Environment

Reproducibly build and ADB-load a no-op `gud.ko` against the installed
OnePlus 6 Ubuntu Touch Linux 4.9 kernel.

## Prerequisites

- ADB authorized USB connection to the OnePlus 6
- `git`, `make`, `gzip`, `sha256sum`, `modinfo`, `nm`
- arm64 cross-compiler (set `CROSS_COMPILE` in the manifest)
- The phone must have `CONFIG_MODULES=y`

## Workflow

```bash
cd backport-4.9
./env/capture-phone.sh
cp env/target-manifest.env.example env/target-manifest.env
# Populate target-manifest.env with the capture output and the selected
# public source URL / 40-character commit SHA, toolchain, and expected
# kernel release.  Record selection rationale and rejected candidates
# in env/local/evidence/ — these stay ignored and never committed.
./env/prepare-kernel.sh
make MANIFEST="$PWD/env/target-manifest.env" modules
./env/deploy-test.sh
```

## Source selection

`capture-phone.sh` does **not** select a kernel source.  After capture,
the operator must:

1. Identify a public kernel repository whose `uname -r` output, `.config`
   SHA-256, and compiler match `env/local/capture/`.
2. Record the immutable 40-character commit SHA, source URL, `KERNEL_SOURCE_REF`,
   and `TOOLCHAIN_VERSION` in `env/target-manifest.env`.
3. Document any rejected candidates under `env/local/evidence/`.

Do not proceed to `prepare-kernel.sh` if source matching is unresolved.

## Hard blockers

Stop and do not continue if any of the following is true:

- `CONFIG_MODULES` is not `y` — module loading is disabled on this kernel.
- `CONFIG_MODULE_SIG_FORCE=y` — the kernel refuses to load any unsigned module.
  This is a **hard stop**. Loading is only possible if you already control a
  signing key that is embedded in and trusted by the **installed kernel image**.
  Building a new key does not help. Do not attempt to work around this without
  a trusted key already present in the installed kernel.
- The `uname -r` of the prepared source tree does not match
  `PHONE_KERNEL_RELEASE` — the build would produce an ABI-incompatible module.
- `Module.symvers` is absent when `CONFIG_MODVERSIONS=y` — symbol version
  CRC checks will fail at load time.
- Source matching remains unresolved — do not guess or substitute a generic tree.

Do **not** modify the phone boot image, installed kernel, or device configuration.

## Local artifacts

All captures, source trees, build output, and evidence logs are
written under `env/local/` which is git-ignored.  The built `gud.ko`
and Kbuild object files are written directly under `backport-4.9/`
and are git-ignored by `backport-4.9/.gitignore`.  Nothing in either
location is ever committed.

| Path | Content |
| --- | --- |
| `env/local/capture/` | Raw ADB captures, `phone.config`, SHA-256, module policy |
| `env/local/kernel/source/` | Pinned kernel source checkout |
| `env/local/kernel/build/` | Prepared external-module build tree |
| `env/local/evidence/` | `prepare-kernel.log`, `module-metadata.txt`, `load-dmesg.txt`, `unload-dmesg.txt` |

## Expected dmesg evidence

After a successful `deploy-test.sh` run, `env/local/evidence/` must contain:

- `load-dmesg.txt` — must include `gud: build probe loaded`
- `unload-dmesg.txt` — must include `gud: build probe unloaded`

The deploy script rejects any output matching `Invalid module format`,
`Required key not available`, `module verification failed`, `BUG:`,
`Oops`, `WARNING:`, `lockdep`, or `use-after-free`.

## Tests

```bash
bash tests/env/test-env-scripts.sh
bash tests/env/test-prepare-kernel.sh
bash tests/env/test-deploy-test.sh
bash tests/env/test-capture-pi-usb.sh
bash tests/env/test-probe-test.sh
bash tests/test-usb-probe-contract.sh
```

All test scripts are hermetic and do not require a phone or kernel tree.

## Ticket 2 Pi USB Capture

Before deploying the GUD USB driver, capture and validate the exact Pi Zero 2 W
GUD gadget identity from a laptop host:

```bash
ls /sys/bus/usb/devices/
./env/capture-pi-usb.sh <selected-sysfs-usb-device-name>
cat env/local/pi-usb/identity.env
```

The selected name is a **device** directory such as `3-1`, not an interface
directory such as `3-1:1.0`.  The capture script validates that the selected
device is `1d50:614d` (the configured Pi GUD gadget VID/PID) and exits `2` with
an error if the identity does not match.

Raw capture output is written to `env/local/pi-usb/` (git-ignored).  The
reviewed `identity.env` must contain:

```
GUD_USB_VENDOR_ID=0x1d50
GUD_USB_PRODUCT_ID=0x614d
```

This evidence is required by `probe-test.sh` before any ADB operations.

## Ticket 2 Phone Probe And Cable Cycle

After a successful Ticket 1 acceptance and Pi USB capture:

```bash
cd backport-4.9
make MANIFEST="$PWD/env/target-manifest.env" modules
./env/probe-test.sh
adb shell dmesg -w
# Remove and reattach the Pi USB data cable five times while recording console output.
adb shell 'sudo rmmod gud'
adb shell dmesg > env/local/evidence/probe-unload-dmesg.txt
```

Retain the following evidence files under `env/local/`:

| File | Must contain |
| --- | --- |
| `probe/module-metadata.txt` | `modinfo` and undefined symbol list for `gud.ko` |
| `probe/probe-load-dmesg.txt` | `GUD probe complete for 1d50:614d` |
| `evidence/probe-cycle-dmesg.txt` | Complete five-cycle console log |
| `evidence/probe-unload-dmesg.txt` | `GUD disconnected` |

Acceptance criteria:

- Every attach must log `GUD probe complete for 1d50:614d`.
- Every removal must log `GUD disconnected`.
- None of the logs may contain: `Invalid module format`, `Required key not available`,
  `module verification failed`, `BUG:`, `Oops`, `WARNING:`, `lockdep`, or `use-after-free`.

Do not update `BACKLOG.md` until all real-device conditions above are met.
