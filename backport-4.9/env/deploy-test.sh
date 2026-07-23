#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
local_dir="${DEPLOY_DIR:-$script_dir/local}"
evidence_dir="$local_dir/evidence"
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

mkdir -p "$evidence_dir"

# Write module metadata before any ADB operation
# modinfo/nm may fail on cross-compiled or stripped objects; capture output regardless
{
    printf 'manifest=%s\n' "$MANIFEST"
    printf 'module=%s\n' "$module_path"
    modinfo "$module_path" || true
    nm -u "$module_path" || true
} > "$evidence_dir/module-metadata.txt" 2>&1

# Guard: refuse to replace an already-loaded module.
# Any ADB failure reading /proc/modules is itself a hard error — do not proceed
# in an unknown state.
modules_output=$("$adb_bin" -s "$serial" shell 'cat /proc/modules' 2>/dev/null) || {
    printf 'failed to read /proc/modules from device; cannot confirm module state\n' >&2
    exit 2
}
if printf '%s' "$modules_output" | grep -q '^gud '; then
    printf 'refusing to replace an already-loaded gud module\n' >&2
    exit 2
fi

# From here, always capture dmesg to evidence even if subsequent steps fail.
# Use a trap so that a set -e abort still preserves whatever dmesg is available.
_deploy_stage=pre-push
cleanup_on_error() {
    local rc=$?
    # Capture dmesg into whichever stage file is appropriate
    case "$_deploy_stage" in
        post-load|post-rmmod)
            "$adb_bin" -s "$serial" shell 'dmesg' > "$evidence_dir/load-dmesg.txt" 2>&1 || true ;;
    esac
    case "$_deploy_stage" in
        post-rmmod)
            "$adb_bin" -s "$serial" shell 'dmesg' > "$evidence_dir/unload-dmesg.txt" 2>&1 || true ;;
    esac
    printf 'deploy-test.sh failed at stage: %s (exit %s)\n' "$_deploy_stage" "$rc" >&2
    printf 'evidence preserved in: %s\n' "$evidence_dir" >&2
    exit "$rc"
}
trap cleanup_on_error ERR

# Deploy
_deploy_stage=push
"$adb_bin" -s "$serial" push "$module_path" /home/phablet/gud.ko

# Load
_deploy_stage=insmod
"$adb_bin" -s "$serial" shell 'sudo insmod /home/phablet/gud.ko'
_deploy_stage=post-load
"$adb_bin" -s "$serial" shell 'dmesg' > "$evidence_dir/load-dmesg.txt" 2>&1

# Unload
_deploy_stage=rmmod
"$adb_bin" -s "$serial" shell 'sudo rmmod gud'
_deploy_stage=post-rmmod
"$adb_bin" -s "$serial" shell 'dmesg' > "$evidence_dir/unload-dmesg.txt" 2>&1

trap - ERR

# Assert load message
if ! grep -qF 'gud: build probe loaded' "$evidence_dir/load-dmesg.txt"; then
    printf 'load dmesg missing expected message: gud: build probe loaded\n' >&2
    exit 2
fi

# Assert unload message
if ! grep -qF 'gud: build probe unloaded' "$evidence_dir/unload-dmesg.txt"; then
    printf 'unload dmesg missing expected message: gud: build probe unloaded\n' >&2
    exit 2
fi

# Reject error/warning patterns in both logs
for log_file in "$evidence_dir/load-dmesg.txt" "$evidence_dir/unload-dmesg.txt"; do
    if grep -Eq 'Invalid module format|Required key not available|module verification failed|BUG:|Oops|WARNING:|lockdep|use-after-free' \
            "$log_file"; then
        printf 'fatal pattern found in %s\n' "$log_file" >&2
        exit 2
    fi
done

printf 'deployment accepted\n'
printf 'evidence written to: %s\n' "$evidence_dir"
