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
mode:

```bash
ssh phablet@<phone-ip> \
  "echo <sudo-password> | sudo -S sh -c 'printf host > /sys/bus/platform/devices/a600000.ssusb/mode'"
ssh phablet@<phone-ip> 'cat /sys/bus/platform/devices/a600000.ssusb/mode'
```

The expected result is:

```text
host
```

This command was required before the Pi enumerated. The controller may report
`host` before a peripheral is attached; host mode alone does not prove the Pi
is visible.

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

The validated topology included a USB hub and the Pi at `1-1.4`:

```text
1-1   214b:7250  USB2.0 HUB
1-1.4 1d50:614d  Generic USB Display
```

Only root hubs (`1d6b:0002` and `1d6b:0003`) means no peripheral has
enumerated. Recheck the OTG-capable adapter, Pi gadget/data port, cable, and
external Pi power before debugging `gud.ko`.

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

## Separate Atomic-Modeset Failure

The USB-host, Pi-enumeration, GUD-probe, and DRM-node milestones succeeded.
The first `gud-kms-smoke /dev/dri/card1` attempt then did not return within two
minutes. The SSH service subsequently became unreachable. This is an unresolved
Ticket 4 KMS issue, likely related to the simple pipe's lack of a hardware
vblank/completion source, not an OTG or GUD USB probe failure.

Until the cause is fixed, do not treat a stalled smoke utility as a reason to
change cables or host-mode settings. After the phone is reachable again,
capture the complete kernel log before retrying the modeset.

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
