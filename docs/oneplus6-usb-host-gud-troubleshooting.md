# OnePlus 6 USB Host And GUD Troubleshooting

## Scope

This record covers the OnePlus 6 Ubuntu Touch / Halium 9 USB-host setup used
for the Linux 4.9 GUD backport. It distinguishes USB enumeration from DRM/KMS
validation so a later atomic-modeset problem is not misdiagnosed as a cable or
host-mode failure.

Target phone ABI:

```text
4.9.112-g6b190d86b SMP preempt mod_unload modversions aarch64
```

The Pi GUD gadget identity is `1d50:614d`.

## Connection Requirements

The phone must be the USB host and the Pi must use its USB gadget/data port.
Connect through a USB-C OTG-capable adapter or hub that presents the Pi as a
USB peripheral to the phone. A charging-only connection is insufficient.

Use Wi-Fi SSH for control after disconnecting the laptop USB cable from the
phone. USB ADB and the Pi cannot occupy the OnePlus 6's only USB-C port at the
same time.

## Working Host-Mode Control

On this kernel, the controller mode attribute is writable and forces host
mode. Use interactive `sudo`. For the dedicated project test phone, `1026` is
the validated sudo password; provide it only to the active shell/session, not
to a committed script or shell history:

```bash
ssh -t phablet@<phone-ip> \
  "printf host | sudo tee /sys/bus/platform/devices/a600000.ssusb/mode >/dev/null"
ssh phablet@<phone-ip> 'cat /sys/bus/platform/devices/a600000.ssusb/mode'
```

The expected result is:

```text
host
```

This command was required before the Pi enumerated. The controller may report
`host` before a peripheral is attached; host mode alone does not prove the Pi
is visible.

## Mandatory Start-Of-Session Enumeration Gate

Every new hardware-test session must force host mode and then poll for the Pi
by VID/PID before loading `gud.ko` or diagnosing DRM. Do not assume that a Pi
seen in a previous SSH session is still enumerated.

```bash
PHONE_HOST=phablet@192.168.1.120

ssh -t "$PHONE_HOST" \
  "printf host | sudo tee /sys/bus/platform/devices/a600000.ssusb/mode >/dev/null"

ssh "$PHONE_HOST" '
i=0
while [ "$i" -lt 15 ]; do
    for dev in /sys/bus/usb/devices/*/idVendor; do
        test -r "$dev" || continue
        v=$(cat "$dev")
        p=$(cat "${dev%/*}/idProduct")
        if [ "$v" = 1d50 ] && [ "$p" = 614d ]; then
            echo "FOUND: ${dev%/*}"
            exit 0
        fi
    done
    i=$((i + 1))
    sleep 2
done
echo "Pi GUD 1d50:614d did not enumerate" >&2
exit 2
'
```

The poll is intentional. A single empty scan is only a point-in-time result;
it is not enough to conclude that the adapter, cable, or Pi is faulty. If the
poll fails, keep the controller in `host`, reseat the Pi data connection, and
run the same poll again before investigating anything above USB enumeration.

Important session rules:

- Match `1d50:614d`; never hardcode a topology such as `1-1.2` or `1-1.4`.
- Run the VID/PID scan independently. Do not put it after an unrelated
  `sudo -n ... &&` check, because a sudo failure would skip the scan.
- Do not use controller mode, extcon state, a prior dmesg capture, or the
  existence of root hubs as a substitute for the `FOUND:` result.
- Do not try to SSH into the Pi to prove USB attachment. The phone's sysfs USB
  identity is the acceptance gate for the host-side tests.
- Run the gate immediately before the KMS stage loop. The stage runner checks
  VID/PID again and intentionally refuses to continue if the Pi disappeared.

## Do Not Use The USB-PD Status Node

The Qualcomm USB-PD driver exposes this `data_role` attribute:

```text
/sys/devices/platform/soc/c440000.qcom,spmi/spmi-0/spmi0-02/c440000.qcom,spmi:qcom,pmi8998@2:qcom,usb-pdphy@1700/usbpd/usbpd0/otg_default/data_role
```

It reported `device` during the successful controller-host session, but writes
to it fail with `Permission denied`, including from root. Treat it as status
only. Do not attempt to force host mode through that node.

The extcon state can also report `USB=0` and `USB_HOST=0` while the controller
host mode successfully enumerates the Pi. Do not use either value as the sole
host-mode success criterion.

## Verify USB Enumeration

After setting controller mode to host and attaching the Pi, verify the USB
device tree:

```bash
ssh phablet@<phone-ip> '
for path in /sys/bus/usb/devices/*; do
    test -r "$path/idVendor" || continue
    printf "%s " "${path##*/}"
    cat "$path/idVendor" "$path/idProduct"
done'
```

Success requires a Pi entry containing:

```text
1d50
614d
```

Validated runs have placed the same Pi at both `1-1.2` and `1-1.4`; USB paths
are assigned dynamically and are not stable test identifiers. One recorded
topology was:

```text
1-1   214b:7250  USB2.0 HUB
1-1.4 1d50:614d  Generic USB Display
```

Only root hubs (`1d6b:0002` and `1d6b:0003`) means no peripheral has
enumerated at that instant. First repeat the bounded poll above; if it still
fails, recheck the OTG-capable adapter, Pi gadget/data port, cable, and external
Pi power before debugging `gud.ko`.

## Canonical Ordered KMS Test

After the enumeration gate prints `FOUND:`, run the checked-in stage runner
from `backport-4.9/` exactly as follows:

```bash
export PHONE_HOST=phablet@192.168.1.120
export PHONE_SUDO_PASSWORD='<phone sudo password>'
export STAGE_BINARY="$PWD/tests/gud-kms-stage"
export MODULE_PATH="$PWD/gud.ko"

for STAGE in caps dumb fb resources connector encoder-crtc planes \
             properties atomic-build atomic-test atomic-commit; do
    STAGE="$STAGE" ./env/kms-stage-test.sh || break
done
```

Do not skip directly to `atomic-commit`. Each invocation verifies the Pi by
VID/PID, reloads the supplied module, confirms the probe and `/dev/dri/card1`,
runs one bounded stage, and writes fresh evidence under `env/local/evidence/`.

## Verify GUD Probe And DRM Registration

With `gud.ko` loaded before or after the Pi connection, the successful probe
record is:

```text
gud 1-1.4:1.0: GUD v1 bulk-out=0x01 buffer=8294400 width=640-1920 height=400-1080
gud 1-1.4:1.0: GUD probe complete for 1d50:614d
```

Ticket 4 then creates a new DRM primary node, observed as `/dev/dri/card1`
beside the phone's built-in `card0`:

```bash
ssh phablet@<phone-ip> 'ls -l /dev/dri; dmesg | grep -E "gud|GUD|1d50|614d|New USB device found"'
```

The corresponding connector is `/sys/class/drm/card1-Virtual-2`. It can report
`status=unknown` and an empty `modes` file before userspace probes it; this is
not evidence that USB enumeration or DRM registration failed.

## Atomic KMS Status

The initial connector query reset the phone because the GUD simple pipe lacked
the initial atomic connector, CRTC, and plane state required by Linux 4.9.
Calling `drm_mode_config_reset()` after simple-pipe construction fixed that
query path. A later `atomic-commit` timeout was caused by the no-transfer pipe
not consuming the pending DRM event. `gud_pipe_update()` now sends that event
synchronously under the DRM event lock.

Fresh phone evidence passes every ordered stage, including the state-applying
`atomic-commit`, with no `WARNING:`, `flip_done timed out`, `BUG:`, `Oops`,
`lockdep`, or `use-after-free` record. This result requires the rebuilt module;
the runner's explicit `MODULE_PATH="$PWD/gud.ko"` prevents accidentally testing
an older copy.

## Evidence Handling

Store raw runtime captures under the ignored directory:

```text
backport-4.9/env/local/evidence/drm-kms-dmesg.txt
backport-4.9/env/local/evidence/drm-kms-modetest.txt
backport-4.9/env/local/evidence/drm-kms-smoke.txt
```

Do not mark Ticket 4 hardware-complete in `PROJECT-STATUS.md` or `BACKLOG.md`
until the smoke test returns successfully and the relevant `dmesg` interval has
no `BUG:`, `Oops`, `WARNING:`, `lockdep`, or `use-after-free` report.
