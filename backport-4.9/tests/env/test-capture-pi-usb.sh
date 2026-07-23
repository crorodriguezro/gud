#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
failures=0

# Build a minimal fake sysfs + tool tree for one Pi GUD gadget.
make_fixture() {
    local tmp="$1"
    local vendor="${2:-1d50}"
    local product="${3:-614d}"

    local dev_dir="$tmp/sys/bus/usb/devices/3-1"
    local iface_dir="$tmp/sys/bus/usb/devices/3-1:1.0"

    mkdir -p "$dev_dir" "$iface_dir"
    printf '%s\n' "$vendor"  > "$dev_dir/idVendor"
    printf '%s\n' "$product" > "$dev_dir/idProduct"
    printf '3\n'             > "$dev_dir/busnum"
    printf '10\n'            > "$dev_dir/devnum"
    printf '\x12\x01'       > "$dev_dir/descriptors"
    printf '\x09\x04'       > "$iface_dir/descriptors"

    # Fake lsusb that accepts exactly "-v -s 003:010"
    cat > "$tmp/lsusb" <<'MOCK'
#!/usr/bin/env bash
if [ "$1" = "-v" ] && [ "$2" = "-s" ] && [ "$3" = "003:010" ]; then
    printf 'Bus 003 Device 010: ID 1d50:614d OpenMoko, Inc.\n'
    exit 0
fi
printf 'lsusb: unexpected args\n' >&2
exit 1
MOCK
    chmod +x "$tmp/lsusb"

    # Fake usb-devices (fixed output)
    cat > "$tmp/usb-devices" <<'MOCK'
#!/usr/bin/env bash
printf 'T:  Bus=03 Lev=01 Prnt=01 Port=00 Cnt=01 Dev#= 10 Spd=480 MxCh= 0\n'
exit 0
MOCK
    chmod +x "$tmp/usb-devices"
}

run_capture() {
    local tmp="$1"
    SYSFS_USB_ROOT="$tmp/sys/bus/usb/devices" \
    LSUSB="$tmp/lsusb" \
    USB_DEVICES="$tmp/usb-devices" \
    PI_CAPTURE_DIR="$tmp/capture" \
    bash "$repo_root/backport-4.9/env/capture-pi-usb.sh" 3-1
}

# Test 1: success — valid 1d50:614d gadget produces expected evidence files
(
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    make_fixture "$tmp"

    run_capture "$tmp"

    ok=1
    test -f "$tmp/capture/lsusb-v.txt"                                    || { printf 'FAIL: missing lsusb-v.txt\n' >&2;                    ok=0; }
    test -f "$tmp/capture/usb-devices.txt"                                 || { printf 'FAIL: missing usb-devices.txt\n' >&2;                ok=0; }
    test -f "$tmp/capture/device-descriptors.bin"                          || { printf 'FAIL: missing device-descriptors.bin\n' >&2;         ok=0; }
    test -f "$tmp/capture/interface-3-1:1.0-descriptors.bin"               || { printf 'FAIL: missing interface descriptor\n' >&2;           ok=0; }
    grep -qx 'GUD_USB_VENDOR_ID=0x1d50'  "$tmp/capture/identity.env"      || { printf 'FAIL: vendor not in identity.env\n' >&2;             ok=0; }
    grep -qx 'GUD_USB_PRODUCT_ID=0x614d' "$tmp/capture/identity.env"      || { printf 'FAIL: product not in identity.env\n' >&2;            ok=0; }
    grep -qx 'GUD_USB_BUSNUM=003'        "$tmp/capture/identity.env"      || { printf 'FAIL: busnum not in identity.env\n' >&2;             ok=0; }
    grep -qx 'GUD_USB_DEVNUM=010'        "$tmp/capture/identity.env"      || { printf 'FAIL: devnum not in identity.env\n' >&2;             ok=0; }
    grep -qx 'GUD_USB_SYSFS_DEVICE=3-1' "$tmp/capture/identity.env"      || { printf 'FAIL: sysfs device not in identity.env\n' >&2;       ok=0; }
    [ "$ok" -eq 1 ] && printf 'PASS [success]\n' || exit 1
) || failures=$((failures + 1))

# Test 2: wrong VID/PID → exit 2 with exact message
(
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    make_fixture "$tmp" "1d50" "a4a0"   # wrong product ID

    rc=0
    actual_output=$(run_capture "$tmp" 2>&1) || rc=$?
    if [ "$rc" -ne 2 ]; then
        printf 'FAIL [wrong-pid]: expected exit 2, got %s\n' "$rc" >&2
        exit 1
    fi
    expected='selected device is not the configured Pi GUD gadget: expected 1d50:614d'
    if ! printf '%s' "$actual_output" | grep -qF "$expected"; then
        printf 'FAIL [wrong-pid]: expected message not found\nOutput: %s\n' "$actual_output" >&2
        exit 1
    fi
    printf 'PASS [wrong-pid]\n'
) || failures=$((failures + 1))

# Test 3: missing device directory → exit 2
(
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    mkdir -p "$tmp/sys/bus/usb/devices"
    # do NOT create the device directory

    rc=0
    SYSFS_USB_ROOT="$tmp/sys/bus/usb/devices" \
    LSUSB="$tmp/lsusb" \
    USB_DEVICES="$tmp/usb-devices" \
    PI_CAPTURE_DIR="$tmp/capture" \
    bash "$repo_root/backport-4.9/env/capture-pi-usb.sh" 3-1 2>/dev/null || rc=$?
    if [ "$rc" -ne 2 ]; then
        printf 'FAIL [missing-dev]: expected exit 2, got %s\n' "$rc" >&2
        exit 1
    fi
    printf 'PASS [missing-dev]\n'
) || failures=$((failures + 1))

printf 'capture-pi-usb tests: %s failures\n' "$failures"
exit "$failures"
