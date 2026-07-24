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
bash tests/test-gem-contract.sh
bash tests/test-drm-kms-contract.sh
```

All test scripts are hermetic and do not require a phone or kernel tree.

## Ticket 3 GEM Build And Symbol Audit

Ticket 3 adds page-backed local GEM helpers but does not register a DRM device.
Build against the captured target ABI:

```bash
cd backport-4.9
make MANIFEST="$PWD/env/target-manifest.env" modules
nm -u gud.ko | sort
grep -E 'drm_gem_(object_init|get_pages|put_pages|mmap|handle_create|object_release|object_unreference_unlocked|create_mmap_offset)|\b(vmap|vunmap|vm_insert_page)\b' \
  env/local/kernel/build/Module.symvers
grep -E '\b(vmap|vunmap|vm_insert_page)\b' env/local/capture/kallsyms.txt
```

The generated module must reference only target-exported GEM and VM symbols.
The `gud_gem_4_9.o` object is expected in the module link.
In particular, verify `drm_gem_get_pages`, `drm_gem_put_pages`,
`drm_gem_mmap`, and `drm_gem_handle_create` remain target-exported.

Ticket 3 has no `/dev/dri/cardX`, so it has no phone-side userspace test.
Ticket 4 registers the DRM device and must create, map, write, and destroy a
1280x720 XRGB8888 dumb buffer while retaining `dmesg` evidence with no kernel
warning.

Before the first Ticket 4 build, confirm required DRM symbols are exported by
the exact target tree:

```bash
grep -E 'drm_(dev_alloc|dev_register|dev_unregister|dev_unref|unplug_dev|mode_config_init|mode_config_cleanup|connector_init|connector_cleanup|simple_display_pipe_init|atomic_helper_check|atomic_helper_commit|framebuffer_init|framebuffer_cleanup|gem_object_lookup|gem_handle_create)' \
    env/local/kernel/build/Module.symvers
```

Build the phone smoke utility on a host with libdrm development headers:

```bash
cc -Wall -Wextra -Werror -O2 -o tests/gud-kms-smoke tests/gud-kms-smoke.c -ldrm
```

If the distribution installs libdrm headers outside the default include path,
use pkg-config:

```bash
cc -Wall -Wextra -Werror -O2 $(pkg-config --cflags libdrm) \
  -o tests/gud-kms-smoke tests/gud-kms-smoke.c $(pkg-config --libs libdrm)
```

Phone runtime evidence workflow:

```bash
# Build and load while attached over ADB, then switch to Wi-Fi SSH before
# attaching the Pi to the phone's only USB-C port.
cd backport-4.9
make MANIFEST="$PWD/env/target-manifest.env" modules
adb push gud.ko /home/phablet/
adb push tests/gud-kms-smoke /home/phablet/
ssh phablet@<phone-ip> 'sudo insmod /home/phablet/gud.ko'
ssh phablet@<phone-ip> 'ls -l /dev/dri && modetest -c -p'
ssh phablet@<phone-ip> '/home/phablet/gud-kms-smoke /dev/dri/cardX'
ssh phablet@<phone-ip> 'dmesg' > env/local/evidence/drm-kms-dmesg.txt
```

Choose `cardX` from `modetest` as the card named `gud`. Save evidence under
`env/local/evidence/` as:

- `drm-kms-modetest.txt`
- `drm-kms-smoke.txt`
- `drm-kms-dmesg.txt`

Required evidence must show one connector/CRTC/encoder topology, XRGB8888,
preferred `1280x720@60`, and `atomic modeset succeeded`. Reject logs containing
`BUG:`, `Oops`, `WARNING:`, `lockdep`, or `use-after-free`.

## Ticket 4 KMS Reset Diagnostic

If `gud-kms-smoke` resets the phone before producing output, isolate the first
failing DRM boundary without changing the kernel driver. Build the stage tool,
then run stages strictly in this order:

```bash
cd backport-4.9
cc -Wall -Wextra -Werror -O2 -o tests/gud-kms-stage \
  tests/gud-kms-stage.c $(pkg-config --cflags --libs libdrm)
export PHONE_HOST=phablet@192.168.1.120
export PHONE_SUDO_PASSWORD='<phone sudo password>'
export STAGE_BINARY="$PWD/tests/gud-kms-stage"
for STAGE in caps dumb fb atomic; do
  export STAGE
  ./env/kms-stage-test.sh || break
done
```

Before each stage, confirm the Pi is enumerated as `1d50:614d`, set controller
mode to `host`, and verify the GUD card exists. The runner writes local ignored
evidence as `drm-kms-stage-<stage>-{stdout,stderr,exit,dmesg}.txt`. If a stage
resets the phone, stop the sequence, recover USB/SSH, capture pstore and PMIC
reset reason, and do not run a later stage.

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

### Phone connection topology (single USB-C port)

Because the OnePlus 6 has one USB-C port, you cannot keep both:

- Pi gadget connected to phone (required for GUD probe), and
- USB ADB cable connected to laptop

at the same time.

Use this flow:

1. Connect phone directly to laptop over USB and complete build/deploy.
2. Configure wireless remote access.
3. Disconnect USB cable from laptop.
4. Connect Pi gadget to phone and collect probe/cycle logs remotely.

### Remote access setup (ADB over Wi-Fi attempt + Wi-Fi fallback used)

Try ADB over Wi-Fi first:

```bash
adb devices
adb shell 'ip addr show wlan0 | grep "inet " | awk "{print \$2}" | cut -d/ -f1'
adb -s <serial> tcpip 5555
adb connect <phone-ip>:5555
```

On this Ubuntu Touch setup, `adb tcpip` did not stay available. The working
fallback was SSH over Wi-Fi:

```bash
# one-time, while USB ADB is connected
adb shell 'echo <sudo-password> | sudo -S systemctl start ssh'
pubkey=$(cat ~/.ssh/id_ed25519.pub)
adb shell "mkdir -p /home/phablet/.ssh && chmod 700 /home/phablet/.ssh"
adb shell "echo '$pubkey' >> /home/phablet/.ssh/authorized_keys && chmod 600 /home/phablet/.ssh/authorized_keys"
ssh -o StrictHostKeyChecking=no phablet@<phone-ip> 'uname -r'
```

When Pi is attached to phone, use SSH for monitoring/control:

```bash
ssh phablet@<phone-ip> 'dmesg | tail -n 50'
ssh phablet@<phone-ip> 'echo <sudo-password> | sudo -S rmmod gud'
```

After a successful Ticket 1 acceptance and Pi USB capture:

```bash
cd backport-4.9
make MANIFEST="$PWD/env/target-manifest.env" modules
./env/probe-test.sh
ssh phablet@<phone-ip> 'dmesg -w'
# Remove and reattach the Pi USB data cable five times while recording console output.
ssh phablet@<phone-ip> 'echo <sudo-password> | sudo -S rmmod gud'
ssh phablet@<phone-ip> 'dmesg' > env/local/evidence/probe-unload-dmesg.txt
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
