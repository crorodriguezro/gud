#!/usr/bin/env bash
set -euo pipefail

# One atomic commit invokes exactly one test-only XDISP SET_BUFFER/bulk pair.
# This script must only run after the Pi journal proves a fresh Idle receiver.
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
phone_host=${PHONE_HOST:?PHONE_HOST is required}
phone_password=${PHONE_SUDO_PASSWORD:?PHONE_SUDO_PASSWORD is required}
module_path=${MODULE_PATH:-$repo_dir/variants/xdisp-lz4-12800/gud.ko}
stage_binary=${STAGE_BINARY:-$repo_dir/tests/gud-kms-stage}
payload_length=${PAYLOAD_LENGTH:?PAYLOAD_LENGTH is required}
zero_packet=${ZERO_PACKET:-0}
evidence_dir=${EVIDENCE_DIR:-$script_dir/local/evidence/single-payload-$(date -u +%Y%m%d-%H%M%S)}

case "$payload_length" in
	*[!0-9]*|'') exit 2 ;;
esac
if (( payload_length < 1 || payload_length > 12800 )); then
	printf 'PAYLOAD_LENGTH must be in 1..12800\n' >&2
	exit 2
fi
if [[ ! -f "$module_path" || ! -x "$stage_binary" ]]; then
	printf 'module or atomic stage binary is missing\n' >&2
	exit 2
fi
mkdir -p "$evidence_dir"

scp -o BatchMode=yes "$module_path" "$stage_binary" "$phone_host:/tmp/"
ssh -o BatchMode=yes "$phone_host" "printf '%s\\n' '$phone_password' | sudo -S sh -c '
echo XDISP_SINGLE_PAYLOAD_${payload_length}_START > /dev/kmsg
printf host > /sys/bus/platform/devices/a600000.ssusb/mode
for i in \\$(seq 1 15); do
  for path in /sys/bus/usb/devices/*/idVendor; do
    test -r \\"\\$path\\" || continue
    test \\"\\$(cat \\"\\$path\\")\\" = 1d50 || continue
    test \\"\\$(cat \\"\\${path%/*}/idProduct\\")\\" = 614d || continue
    found=1; break 2
  done
  sleep 2
done
test \\"\\${found:-}\\" = 1 || exit 2
rmmod gud 2>/dev/null || true
insmod /tmp/gud.ko bulk_timeout_ms=3000 bulk_trace_limit=1 xdisp_payload_timing=1 xdisp_probe_payload_length=$payload_length xdisp_probe_zero_packet=$zero_packet
timeout 10s /tmp/gud-kms-stage atomic-commit /dev/dri/card1
result=\\$?
echo XDISP_SINGLE_PAYLOAD_${payload_length}_END > /dev/kmsg
exit \\$result
'" >"$evidence_dir/phone-stdout.log" 2>"$evidence_dir/phone-stderr.log" || true
ssh -o BatchMode=yes "$phone_host" "printf '%s\\n' '$phone_password' | sudo -S dmesg" >"$evidence_dir/phone-kernel-full.log"
printf '%s\n' "$evidence_dir"
