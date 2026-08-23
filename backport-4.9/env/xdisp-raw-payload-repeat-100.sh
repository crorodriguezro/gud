#!/usr/bin/env bash
set -euo pipefail

# Test-only raw payload repeat gate, separate from E2-T04 managed-frame tests.
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
phone_host=${PHONE_HOST:?PHONE_HOST is required}
phone_password=${PHONE_SUDO_PASSWORD:?PHONE_SUDO_PASSWORD is required}
module_path=${MODULE_PATH:-$repo_dir/variants/xdisp-lz4-12800/gud.ko}
stage_binary=${STAGE_BINARY:-$repo_dir/tests/gud-kms-stage}
payload_length=${PAYLOAD_LENGTH:?PAYLOAD_LENGTH is required}
payload_cap=${PAYLOAD_CAP:-$payload_length}
transaction_count=${TRANSACTION_COUNT:-100}
ssh_bin=${SSH:-ssh}
scp_bin=${SCP:-scp}
batch_mode=${BATCH_MODE:-yes}
evidence_dir=${EVIDENCE_DIR:-$script_dir/local/evidence/raw-payload-repeat-$(date -u +%Y%m%d-%H%M%S)}

case "$payload_length" in *[!0-9]*|'') exit 2 ;; esac
case "$payload_cap" in *[!0-9]*|'') exit 2 ;; esac
case "$transaction_count" in *[!0-9]*|'') exit 2 ;; esac
if (( payload_length < 1 || payload_length > 4194304 ||
      payload_cap < payload_length || payload_cap > 4194304 ||
      transaction_count < 1 )); then
    printf 'payload length/cap/count out of range\n' >&2
    exit 2
fi
if [[ ! -f "$module_path" || ! -x "$stage_binary" ]]; then
    printf 'module or atomic stage binary is missing\n' >&2
    exit 2
fi
mkdir -p "$evidence_dir"
remote_script=$(mktemp)
trap 'rm -f "$remote_script"' EXIT

cat >"$remote_script" <<EOF
set -u
payload_length=$payload_length
payload_cap=$payload_cap
transaction_count=$transaction_count
echo XDISP_RAW_REPEAT_BEGIN payload_bytes=\$payload_length payload_cap=\$payload_cap transaction_count=\$transaction_count > /dev/kmsg
printf host > /sys/bus/platform/devices/a600000.ssusb/mode
found=
for i in \$(seq 1 15); do
  for path in /sys/bus/usb/devices/*/idVendor; do
    test -r "\$path" || continue
    test "\$(cat "\$path")" = 1d50 || continue
    test "\$(cat "\${path%/*}/idProduct")" = 614d || continue
    found=1
    break 2
  done
  sleep 2
done
test -n "\$found" || exit 2
rmmod gud 2>/dev/null || true
insmod /tmp/gud.ko bulk_timeout_ms=3000 bulk_trace_limit=100 xdisp_payload_limit=\$payload_cap xdisp_payload_timing=1 xdisp_probe_payload_length=\$payload_length
overall=0
for transaction in \$(seq 1 \$transaction_count); do
  start_ns=\$(date +%s%N)
  echo XDISP_RAW_REPEAT_TRANSACTION_\${transaction}_START payload_bytes=\$payload_length start_ns=\$start_ns > /dev/kmsg
  timeout 10s /tmp/gud-kms-stage atomic-commit /dev/dri/card1
  command_result=\$?
  end_ns=\$(date +%s%N)
  echo XDISP_RAW_REPEAT_TRANSACTION_\${transaction}_END command_result=\$command_result end_ns=\$end_ns > /dev/kmsg
  printf 'XDISP_RAW_REPEAT_RESULT transaction=%s payload_bytes=%s command_result=%s start_ns=%s end_ns=%s\n' \
    "\$transaction" "\$payload_length" "\$command_result" "\$start_ns" "\$end_ns"
  test "\$command_result" -eq 0 || { overall=\$command_result; break; }
done
echo XDISP_RAW_REPEAT_END result=\$overall > /dev/kmsg
exit "\$overall"
EOF

"$scp_bin" -F /dev/null -o BatchMode="$batch_mode" "$module_path" "$stage_binary" "$remote_script" "$phone_host:/tmp/"
set +e
"$ssh_bin" -F /dev/null -o BatchMode="$batch_mode" "$phone_host" "printf '%s\\n' '$phone_password' | sudo -S sh /tmp/$(basename "$remote_script")" \
    >"$evidence_dir/phone-stdout.log" 2>"$evidence_dir/phone-stderr.log"
remote_runner_result=$?
set -e
"$ssh_bin" -F /dev/null -o BatchMode="$batch_mode" "$phone_host" "printf '%s\\n' '$phone_password' | sudo -S dmesg" \
    >"$evidence_dir/phone-kernel-full.log"
{
    printf 'host_runner=xdisp-raw-payload-repeat-100\n'
    printf 'configured_transaction_count=%s\n' "$transaction_count"
    printf 'configured_payload_bytes=%s\n' "$payload_length"
    printf 'configured_payload_cap=%s\n' "$payload_cap"
    printf 'remote_runner_result=%s\n' "$remote_runner_result"
    grep -E 'XDISP_RAW_REPEAT_RESULT' "$evidence_dir/phone-stdout.log" || true
    grep -E 'XDISP_RAW_REPEAT_(BEGIN|TRANSACTION_[0-9]+_(START|END)|END)|XDISP_PROBE (start|complete)' \
        "$evidence_dir/phone-kernel-full.log" || true
} >"$evidence_dir/HOST_SEQUENCE.txt"
printf '%s\n' "$evidence_dir"
exit "$remote_runner_result"
