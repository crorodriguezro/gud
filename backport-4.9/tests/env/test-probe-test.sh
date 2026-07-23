#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
failures=0

# Build a minimal fixture for probe-test.sh
make_fixture() {
    local tmp="$1"
    local modules_output="${2:-}"
    local load_dmesg_line="${3:-GUD probe complete for 1d50:614d}"

    # Fake gud.ko
    touch "$tmp/gud.ko"

    # Fake kernel build dir
    mkdir -p "$tmp/kbuild"

    # Fake Pi USB identity
    mkdir -p "$tmp/pi-usb"
    cat > "$tmp/pi-usb/identity.env" <<EOF
GUD_USB_SYSFS_DEVICE=3-1
GUD_USB_VENDOR_ID=0x1d50
GUD_USB_PRODUCT_ID=0x614d
GUD_USB_BUSNUM=003
GUD_USB_DEVNUM=010
EOF

    # Manifest
    cat > "$tmp/manifest.env" <<EOF
PHONE_KERNEL_RELEASE=4.9.337-OnePlus
PHONE_CONFIG_SHA256=abc
PHONE_ARCH=arm64
KERNEL_SOURCE_URL=https://example.com/kernel.git
KERNEL_SOURCE_COMMIT=abcdef1234567890abcdef1234567890abcdef12
KERNEL_SOURCE_REF=refs/heads/test
CROSS_COMPILE=aarch64-linux-gnu-
TOOLCHAIN_VERSION=mock
CONFIG_MODVERSIONS=n
CONFIG_MODULE_SIG=n
CONFIG_MODULE_SIG_FORCE=n
KERNEL_BUILD_DIR=$tmp/kbuild
EOF

    cat > "$tmp/adb" <<MOCK
#!/usr/bin/env bash
if [ "\$1" = "-s" ]; then shift 2; fi
subcmd="\$1"; shift
case "\$subcmd" in
  devices)   printf 'List of devices attached\ntest123\tdevice\n'; exit 0 ;;
  get-state) printf 'device\n'; exit 0 ;;
  push)      exit 0 ;;
  shell)
    cmd="\$*"
    case "\$cmd" in
      'cat /proc/modules')
        printf '%s\n' "$modules_output"
        exit 0 ;;
      'sudo insmod /home/phablet/gud.ko')
        exit 0 ;;
      dmesg)
        printf '%s\n' "$load_dmesg_line"
        exit 0 ;;
      *) exit 0 ;;
    esac ;;
  *) exit 0 ;;
esac
MOCK
    chmod +x "$tmp/adb"
}

run_probe_test() {
    local tmp="$1"
    ADB="$tmp/adb" \
    MANIFEST="$tmp/manifest.env" \
    MODULE_PATH="$tmp/gud.ko" \
    PROBE_DIR="$tmp/probe" \
    PI_IDENTITY="$tmp/pi-usb/identity.env" \
    bash "$repo_root/backport-4.9/env/probe-test.sh"
}

# Test 1: already-loaded module → exit 2 with message
(
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    make_fixture "$tmp" "gud 12345 0 - Live 0x0000000000000000"

    rc=0
    actual_output=$(run_probe_test "$tmp" 2>&1) || rc=$?
    if [ "$rc" -ne 2 ]; then
        printf 'FAIL [already-loaded]: expected exit 2, got %s\n' "$rc" >&2
        exit 1
    fi
    if ! printf '%s' "$actual_output" | grep -qF 'refusing to replace an already-loaded gud module'; then
        printf 'FAIL [already-loaded]: message not found\nOutput: %s\n' "$actual_output" >&2
        exit 1
    fi
    printf 'PASS [already-loaded]\n'
) || failures=$((failures + 1))

# Test 2: success — dmesg contains probe line → exit 0 with evidence files
(
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    make_fixture "$tmp" "" "GUD probe complete for 1d50:614d"

    rc=0
    actual_output=$(run_probe_test "$tmp" 2>&1) || rc=$?
    if [ "$rc" -ne 0 ]; then
        printf 'FAIL [success]: expected exit 0, got %s\nOutput:\n%s\n' "$rc" "$actual_output" >&2
        exit 1
    fi
    for f in module-metadata.txt probe-load-dmesg.txt; do
        if [ ! -f "$tmp/probe/$f" ]; then
            printf 'FAIL [success]: missing evidence file %s\n' "$f" >&2
            exit 1
        fi
    done
    printf 'PASS [success]\n'
) || failures=$((failures + 1))

# Test 3: probe-failure — dmesg contains unsupported protocol version → nonzero, dmesg preserved
(
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    make_fixture "$tmp" "" "unsupported GUD protocol version: 99"

    rc=0
    run_probe_test "$tmp" 2>/dev/null || rc=$?
    if [ "$rc" -eq 0 ]; then
        printf 'FAIL [probe-failure]: expected nonzero exit\n' >&2
        exit 1
    fi
    if [ ! -f "$tmp/probe/probe-load-dmesg.txt" ]; then
        printf 'FAIL [probe-failure]: probe-load-dmesg.txt not preserved\n' >&2
        exit 1
    fi
    printf 'PASS [probe-failure]\n'
) || failures=$((failures + 1))

# Test 4: missing Pi identity → exit 2
(
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    make_fixture "$tmp" "" "GUD probe complete for 1d50:614d"
    rm "$tmp/pi-usb/identity.env"

    rc=0
    actual_output=$(run_probe_test "$tmp" 2>&1) || rc=$?
    if [ "$rc" -ne 2 ]; then
        printf 'FAIL [no-identity]: expected exit 2, got %s\n' "$rc" >&2
        exit 1
    fi
    printf 'PASS [no-identity]\n'
) || failures=$((failures + 1))

printf 'probe-test tests: %s failures\n' "$failures"
exit "$failures"
