#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
adb_bin="${ADB:-adb}"

MANIFEST="${MANIFEST:-$script_dir/target-manifest.env}"
if [ ! -f "$MANIFEST" ]; then
    printf 'manifest not found: %s\n' "$MANIFEST" >&2
    exit 2
fi

# shellcheck source=/dev/null
. "$MANIFEST"

module_path="${MODULE_PATH:-$script_dir/../gud.ko}"
if [ ! -f "$module_path" ]; then
    printf 'gud.ko not found at %s\n' "$module_path" >&2
    exit 2
fi

if [ -z "${KERNEL_BUILD_DIR:-}" ] || [ ! -d "$KERNEL_BUILD_DIR" ]; then
    printf 'KERNEL_BUILD_DIR is not set or not a directory in %s\n' "$MANIFEST" >&2
    exit 2
fi

# Require reviewed Pi USB identity evidence before any ADB operation
pi_identity="${PI_IDENTITY:-$script_dir/local/pi-usb/identity.env}"
if [ ! -f "$pi_identity" ] ||
   ! grep -qx 'GUD_USB_VENDOR_ID=0x1d50' "$pi_identity" ||
   ! grep -qx 'GUD_USB_PRODUCT_ID=0x614d' "$pi_identity"; then
    printf 'validated Pi USB identity evidence is required: %s\n' "$pi_identity" >&2
    exit 2
fi

evidence_dir="${PROBE_DIR:-$script_dir/local/probe}"
mkdir -p "$evidence_dir"

# Select exactly one ADB device
mapfile -t serials < <("$adb_bin" devices | awk 'NR > 1 && $2 == "device" { print $1 }')
if [ -n "${ADB_SERIAL:-}" ]; then
    serial="$ADB_SERIAL"
    "$adb_bin" -s "$serial" get-state 2>/dev/null | grep -qx device || {
        printf 'ADB_SERIAL is not an authorized device: %s\n' "$serial" >&2
        exit 2
    }
elif [ "${#serials[@]}" -eq 1 ]; then
    serial="${serials[0]}"
elif [ "${#serials[@]}" -eq 0 ]; then
    printf 'no authorized ADB device found\n' >&2
    exit 2
else
    printf 'multiple ADB devices found; set ADB_SERIAL\n' >&2
    exit 2
fi

# Write module metadata before any ADB operation
{
    printf 'manifest=%s\n' "$MANIFEST"
    printf 'module=%s\n' "$module_path"
    modinfo "$module_path" || true
    nm -u "$module_path" || true
} > "$evidence_dir/module-metadata.txt" 2>&1

# Guard: refuse to replace an already-loaded module
modules_output=$("$adb_bin" -s "$serial" shell 'cat /proc/modules' 2>/dev/null) || {
    printf 'failed to read /proc/modules from device; cannot confirm module state\n' >&2
    exit 2
}
if printf '%s' "$modules_output" | grep -q '^gud '; then
    printf 'refusing to replace an already-loaded gud module\n' >&2
    exit 2
fi

# Always capture dmesg to evidence even if subsequent steps fail.
cleanup_on_error() {
    local rc=$?
    "$adb_bin" -s "$serial" shell 'dmesg' > "$evidence_dir/probe-load-dmesg.txt" 2>&1 || true
    printf 'probe-test.sh failed (exit %s)\n' "$rc" >&2
    printf 'evidence preserved in: %s\n' "$evidence_dir" >&2
    exit "$rc"
}
trap cleanup_on_error ERR

# Deploy and load
"$adb_bin" -s "$serial" push "$module_path" /home/phablet/gud.ko
"$adb_bin" -s "$serial" shell 'sudo insmod /home/phablet/gud.ko'
"$adb_bin" -s "$serial" shell 'dmesg' > "$evidence_dir/probe-load-dmesg.txt" 2>&1

trap - ERR

# Require successful probe message
if ! grep -qF 'GUD probe complete for 1d50:614d' "$evidence_dir/probe-load-dmesg.txt"; then
    printf 'load dmesg missing expected probe message\n' >&2
    exit 2
fi

# Reject fatal patterns
for pattern in \
    'Invalid module format' \
    'Required key not available' \
    'module verification failed' \
    'BUG:' \
    'Oops' \
    'WARNING:' \
    'lockdep' \
    'use-after-free'; do
    if grep -qF "$pattern" "$evidence_dir/probe-load-dmesg.txt"; then
        printf 'fatal pattern found in probe-load-dmesg.txt: %s\n' "$pattern" >&2
        exit 2
    fi
done

printf 'probe accepted\n'
printf 'evidence written to: %s\n' "$evidence_dir"
printf '\nThe module is left loaded. To remove it:\n'
printf '  adb shell sudo rmmod gud\n'
