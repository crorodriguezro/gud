#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
local_dir="${CAPTURE_DIR:-$script_dir/local}"
capture_dir="$local_dir/capture"
adb_bin="${ADB:-adb}"

# Select device
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

mkdir -p "$capture_dir"

# Raw capture helpers
adb_capture() {
    local name="$1"; shift
    "$adb_bin" -s "$serial" shell "$@" > "$capture_dir/$name.txt" 2>&1 || true
}

adb_capture uname-a      'uname -a'
adb_capture uname-r      'uname -r'
adb_capture proc-version 'cat /proc/version'
adb_capture getprop      'getprop'
adb_capture proc-modules 'cat /proc/modules'
adb_capture lsmod        'lsmod'
adb_capture kallsyms     'cat /proc/kallsyms'
adb_capture lib-modules  'ls -la /lib/modules/$(uname -r)/ 2>/dev/null || echo "not found"'
adb_capture module-symvers-locations \
    'find /lib/modules/ /boot/ /usr/src/ -name "Module.symvers" 2>/dev/null || echo "none found"'

# Extract kernel release
phone_kernel_release=$("$adb_bin" -s "$serial" shell 'uname -r' 2>/dev/null | tr -d '\r\n')

# Config extraction
if "$adb_bin" -s "$serial" shell 'test -r /proc/config.gz' 2>/dev/null; then
    "$adb_bin" -s "$serial" pull /proc/config.gz "$capture_dir/config.gz"
    gzip -cd "$capture_dir/config.gz" > "$capture_dir/phone.config"
    sha256sum "$capture_dir/phone.config" | awk '{print $1}' > "$capture_dir/phone.config.sha256"
else
    printf 'missing /proc/config.gz; obtain matching config from selected source artifacts\n' >&2
fi

# Module policy extraction
if [ -f "$capture_dir/phone.config" ]; then
    {
        for key in CONFIG_MODULES CONFIG_MODVERSIONS CONFIG_MODULE_SIG \
                   CONFIG_MODULE_SIG_FORCE CONFIG_MODULE_COMPRESS; do
            grep -E "^${key}[=]" "$capture_dir/phone.config" || printf '%s is not set\n' "$key"
        done
    } > "$capture_dir/module-policy.txt"

    modules_val=$(grep -E '^CONFIG_MODULES=' "$capture_dir/phone.config" | cut -d= -f2 | tr -d '"' || true)
    if [ "$modules_val" != "y" ]; then
        printf 'CONFIG_MODULES is not y; module loading is not supported on this kernel\n' >&2
        exit 2
    fi
fi

# Print manifest summary
phone_config_sha256=""
if [ -f "$capture_dir/phone.config.sha256" ]; then
    phone_config_sha256=$(cat "$capture_dir/phone.config.sha256")
fi

printf 'PHONE_KERNEL_RELEASE=%s\n' "$phone_kernel_release"
printf 'PHONE_CONFIG_SHA256=%s\n' "$phone_config_sha256"
printf 'capture written to: %s\n' "$capture_dir"
