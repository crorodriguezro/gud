# OnePlus 6 USB Host And GUD Troubleshooting

## Scope

This record covers the OnePlus 6 Ubuntu Touch / Halium 9 USB-host setup used
for the Linux 4.9 GUD backport. It distinguishes USB enumeration from DRM/KMS
validation so a later atomic-modeset problem is not misdiagnosed as a cable or
host-mode failure.

For the verified pre-benchmark hardware and pipeline baseline, see
[benchmark/oneplus6-pi-zero2w-verification.md](</home/cristianr/Projects/linux-mobile/gud-benchmark/benchmark/oneplus6-pi-zero2w-verification.md>).

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

### Project SSH Access

The project devices are reachable on the local network at:

| Device | SSH target | Login password | `sudo` password |
| --- | --- | --- | --- |
| OnePlus 6 | `phablet@192.168.1.120` | `1026` | `1026` |
| Raspberry Pi | `cristian@192.168.1.110` | `cristian` | `cristian` |

The workstation's default SSH configuration currently rejects the systemd
proxy snippet because of its local file permissions. Bypass it with
`-F /dev/null`. The phone accepts both the workstation's authorized key and
password authentication; password authentication should be forced for an
agent that does not have the workstation key:

```bash
ssh -F /dev/null \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o KbdInteractiveAuthentication=no \
  phablet@192.168.1.120

ssh -F /dev/null \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o KbdInteractiveAuthentication=no \
  cristian@192.168.1.110
```

For non-interactive privileged commands, feed the device password to remote
`sudo`, not to the SSH client:

```bash
printf '1026\n' | ssh -F /dev/null phablet@192.168.1.120 \
  'sudo -S <command>'
printf 'cristian\n' | ssh -F /dev/null cristian@192.168.1.110 \
  'sudo -S <command>'
```

These credentials are specific to this lab setup and are intentionally stored
in this runbook so authorized troubleshooting agents can connect without
guesswork. Do not remove or redact them from this document unless the lab
credentials are deliberately rotated and this section is updated at the same
time. Do not reuse them outside this isolated network or copy them into other
commits, scripts, or shell history.

### Validated Cable And Power Topology

The fixed BENFEI port and the following physical arrangement were validated on
2026-08-02:

- white cable from the Pi OTG/data port (the port near HDMI) to the BENFEI port;
- either the short black or long black cable from the Pi energy/power port to
  the power supply;
- the hub's upstream/host connection attached to the OnePlus OTG adapter and
  phone.

Both black power cables passed the high-speed preflight when the hub was also
connected to the phone: the Pi reported `high-speed` and the phone reported
`480`. Earlier failed trials were invalid because the hub upstream connection
to the phone had been left disconnected; those trials showed only root hubs or
left the Pi UDC `not attached`. Cable comparisons must therefore keep the
BENFEI port and hub-to-phone connection fixed.

For repeatable trials, disconnect the topology and wait 15 seconds, connect
the selected black power cable to the Pi energy port, wait for the Pi to boot,
connect the white data cable to the Pi OTG port and BENFEI, connect the hub
upstream cable to the phone OTG adapter, then force the phone controller
through `device` and back to `host` before running the dynamic VID/PID poll.

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

## Recovery When Host Mode Does Not Enumerate The Pi

If the phone reports `host` but the VID/PID poll still finds only the xHCI
root hubs (`1d6b:0002` and `1d6b:0003`), reset the OnePlus controller role by
cycling it through `device` and back to `host`:

```bash
ssh -t phablet@<phone-ip> '
printf device | sudo tee /sys/bus/platform/devices/a600000.ssusb/mode >/dev/null
sleep 2
printf host | sudo tee /sys/bus/platform/devices/a600000.ssusb/mode >/dev/null
cat /sys/bus/platform/devices/a600000.ssusb/mode
'
```

Then run the complete dynamic VID/PID poll again. Do not treat the final
`host` text as success; success still requires finding `1d50:614d`.

This recovery was validated on 2026-07-27. Restarting
`gud-userspace.service` cleanly rebound the Pi gadget, but the Pi UDC remained
`not attached` and the phone still exposed only its root hubs. Cycling the
OnePlus controller `device` -> `host` caused the Pi to enumerate at the
dynamically assigned path `1-1.3` as `1d50:614d` (`Generic USB Display`) at
480 Mbit/s. The Pi UDC changed to `configured`, and FunctionFS received its
`Enable` event. This identifies a stale OnePlus host-controller/role state,
not a GUD service failure.

Use this role cycle only after confirming that the Pi service is active and
the gadget is bound to its UDC. If the Pi still reports `not attached` after
the cycle and the phone still sees only root hubs, reseat and verify the USB
data/OTG connection; restarting GUD repeatedly will not repair a missing
electrical attachment.

## Recovery When The Powered Hub Itself Is Missing

Before blaming the Pi or GUD, distinguish a missing external hub from a
missing gadget. The validated powered-hub topology contains the hub
`214b:7250`; one tested USB-C adapter behind it also identifies as
`343c:0000`. These identities are diagnostic observations, not replacements
for the mandatory dynamic `1d50:614d` gate.

If the phone is in `host` mode but its USB tree contains only the xHCI root
hubs, the failure is below GUD: the phone has not enumerated the external hub.
A controller role cycle may not recover a hub that remains partially powered
through another cable. Completely depower the topology:

1. Disconnect the phone from the hub.
2. Disconnect the hub power supply.
3. Disconnect the Pi data cable and Pi power supply.
4. Wait at least 15 seconds so every possible hub power path is removed.
5. Power the hub first, then power the Pi separately.
6. Connect the Pi gadget/data port to a hub downstream port.
7. Connect the hub's dedicated upstream/host cable to the phone's OTG adapter.

Then cycle the OnePlus controller `device` -> `host` and run the complete
dynamic VID/PID poll again. Verify the complete phone tree as well. Seeing
`214b:7250` proves the phone-to-hub data path recovered; it does not prove the
Pi is ready.

This recovery was validated on 2026-07-28. Before the complete power removal,
repeated Pi service restarts, OnePlus reboots, and controller role cycles left
the phone showing only its root hubs. After the ordered power cycle, the phone
enumerated `214b:7250` at `1-1` and the adapter `343c:0000` at `1-1.1`.

## Recovery After A Pi Power Cycle

A Pi power cycle can introduce a second, independent failure. On the project
Pi, `gud-userspace.service` is enabled at boot, but verify that it is active
after the Pi returns. If it is inactive, the GUD configfs gadget does not exist
and the phone cannot read `1d50:614d`, even though the hub is now working.

When the hub enumerates but the Pi does not, inspect both sides before
changing cables again:

```bash
ssh <pi-host> '
systemctl is-active gud-userspace.service
cat /sys/class/udc/3f980000.usb/state
test ! -r /sys/kernel/config/usb_gadget/usb-gadget0/UDC || \
    cat /sys/kernel/config/usb_gadget/usb-gadget0/UDC
'

ssh phablet@<phone-ip> '
dmesg | grep -E "usb 1-|device descriptor|unable to enumerate" | tail -n 100
'
```

An inactive service, missing `usb-gadget0`, and downstream phone errors such
as `device descriptor read/64, error -110`, `device not accepting address`, or
`unable to enumerate USB device` identify this post-reboot state. Start the
service interactively and verify that it binds the expected UDC:

```bash
ssh -t <pi-host> '
sudo systemctl start gud-userspace.service
systemctl is-active gud-userspace.service
cat /sys/kernel/config/usb_gadget/usb-gadget0/UDC
cat /sys/class/udc/3f980000.usb/state
'
```

Expected service/binding output includes `active` and `3f980000.usb`. The UDC
may remain `not attached` until the phone retries enumeration. After starting
the service, cycle the OnePlus controller `device` -> `host` once to clear the
hub port's failed descriptor state, then run the mandatory VID/PID poll. The
2026-07-28 recovery immediately found the Pi dynamically at `1-1.2` as
`1d50:614d`.

This sequence distinguishes two failures that can occur in one session:

- only root hubs: recover the phone-to-hub attachment with a true full-topology
  power cycle;
- external hub present but GUD absent after a Pi reboot: restore the Pi GUD
  service, then reset the phone role and poll again.

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

The Qualcomm USB-PD driver exposes `data_role` and `power_role` attributes
under this directory:

```text
/sys/devices/platform/soc/c440000.qcom,spmi/spmi-0/spmi0-02/c440000.qcom,spmi:qcom,pmi8998@2:qcom,usb-pdphy@1700/usbpd/usbpd0/otg_default/
```

They are read-only on this kernel. `data_role` reported `device` during a
successful controller-host session. During the 2026-07-28 powered-hub
recovery, `power_role` reported `sink` and the kernel reported a Type-C Source
partner even though forcing the writable controller node to `host` later
enumerated the hub and Pi successfully. Treat both values as status only; do
not attempt to force the data role, power role, or VBUS through these nodes.
Forcing a regulator or GPIO below USB-PD policy risks contention with the
powered hub and is outside this procedure.

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

## Enumeration Acceptance Gate

Do not call enumeration working merely because `1d50:614d` is visible or the
Pi UDC is `configured`. A usable attach requires all of these checks from the
same fresh connection attempt:

- the phone controller reports `host`;
- the phone dynamically finds `1d50:614d` under `/sys/bus/usb/devices/*`;
- the Pi service is active and its UDC reports `configured` with
  `3f980000.usb` bound;
- phone dmesg contains `GUD probe complete for 1d50:614d`;
- `/dev/dri/card1` exists on the phone.

Treat `can't set config #1, error -32`, a Pi `Disable` event, a missing
`/dev/dri/card1`, or a missing fresh probe as an unusable enumeration even if
the VID/PID remains visible. Reset the phone controller through `device` and
back to `host`, then rerun the complete gate. Do not send diagnostic or KMS
payloads until every acceptance check passes.

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

On 2026-08-29, the phone initially saw only its root hubs while the Pi
service was active and its UDC was bound. Cycling the OnePlus controller
through `device` and back to `host` recovered enumeration at the dynamic path
`1-1.3`. The bounded poll then found `1d50:614d`, the Pi UDC became
`configured`, and the rebuilt module created `/dev/dri/card1`.

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

The first 2026-08-29 `atomic-test` attempt returned `EINVAL` because the
legacy `gud-kms-stage` helper created an XRGB8888 framebuffer, while the active
Pi gadget advertises RGB565. The Pi's `GUD state check failed: -22` was
therefore a correct format rejection, not a USB or kernel failure. After the
helper was corrected to use 16-bpp `DRM_FORMAT_RGB565`, the contract test and
all ordered KMS stages passed. The RGB565 `gud-kms-smoke` then completed the
atomic modeset and one compressed 1280x720 transfer; Pi telemetry reported
`processing_failed=0`, `poisoned_transactions=0`, and `aggregate_state=Idle`.

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
