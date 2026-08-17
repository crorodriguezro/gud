#!/usr/bin/env bash
set -euo pipefail

# E1-T02 host half: one module load and exactly two ordered atomic commits.
# Each commit produces one 12,800-byte STATUS_ON_SET probe transfer.  This is
# intentionally not a retry loop and must not be split into independent shell
# invocations: the first commit's disable statuses are part of the exercised
# sequential lifecycle.
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
phone_host=${PHONE_HOST:?PHONE_HOST is required}
phone_password=${PHONE_SUDO_PASSWORD:?PHONE_SUDO_PASSWORD is required}
module_path=${MODULE_PATH:-$repo_dir/variants/xdisp-lz4-12800/gud.ko}
stage_binary=${STAGE_BINARY:-$repo_dir/tests/gud-kms-stage}
evidence_dir=${EVIDENCE_DIR:-$script_dir/local/evidence/status-on-set-two-transaction-$(date -u +%Y%m%d-%H%M%S)}
readonly payload_length=12800
readonly transaction_limit=2

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
transaction_limit=$transaction_limit
echo XDISP_T02_BEGIN payload_bytes=\$payload_length transaction_limit=\$transaction_limit > /dev/kmsg
printf host > /sys/bus/platform/devices/a600000.ssusb/mode
found=
for i in \$(seq 1 15); do
  for path in /sys/bus/usb/devices/*/idVendor; do
    test -r "\$path" || continue
    test "\$(cat "\$path")" = 1d50 || continue
    test "\$(cat "\${path%/*}/idProduct")" = 614d || continue
    found=\${path%/*}
    break 2
  done
  sleep 2
done
test -n "\$found" || exit 2
rmmod gud 2>/dev/null || true
insmod /tmp/gud.ko bulk_timeout_ms=3000 bulk_trace_limit=2 xdisp_payload_timing=1 xdisp_probe_payload_length=\$payload_length
overall=0
for transaction in 1 2; do
  start_ns=\$(date +%s%N)
  echo XDISP_T02_TRANSACTION_\${transaction}_START payload_bytes=\$payload_length start_ns=\$start_ns > /dev/kmsg
  timeout 10s /tmp/gud-kms-stage atomic-commit /dev/dri/card1
  command_result=\$?
  end_ns=\$(date +%s%N)
  echo XDISP_T02_TRANSACTION_\${transaction}_END command_result=\$command_result end_ns=\$end_ns > /dev/kmsg
  printf 'XDISP_T02_HOST_RESULT transaction=%s payload_bytes=%s command_result=%s start_ns=%s end_ns=%s\n' \\
    "\$transaction" "\$payload_length" "\$command_result" "\$start_ns" "\$end_ns"
  test "\$command_result" -eq 0 || overall=\$command_result
done
echo XDISP_T02_END result=\$overall > /dev/kmsg
exit "\$overall"
EOF

scp -o BatchMode=yes "$module_path" "$stage_binary" "$remote_script" "$phone_host:/tmp/"
set +e
ssh -o BatchMode=yes "$phone_host" "printf '%s\\n' '$phone_password' | sudo -S sh /tmp/$(basename "$remote_script")" \
    >"$evidence_dir/phone-stdout.log" 2>"$evidence_dir/phone-stderr.log"
remote_runner_result=$?
set -e
ssh -o BatchMode=yes "$phone_host" "printf '%s\\n' '$phone_password' | sudo -S dmesg" \
    >"$evidence_dir/phone-kernel-full.log"
{
    printf 'host_runner=xdisp-two-status-on-set-probe\n'
    printf 'configured_transaction_limit=%s\n' "$transaction_limit"
    printf 'configured_payload_bytes=%s\n' "$payload_length"
    printf 'remote_runner_result=%s\n' "$remote_runner_result"
    rg 'XDISP_T02_HOST_RESULT' "$evidence_dir/phone-stdout.log" || true
    rg 'XDISP_T02_(BEGIN|TRANSACTION_[12]_(START|END)|END)|XDISP_PROBE (start|complete)' \
        "$evidence_dir/phone-kernel-full.log" || true
} >"$evidence_dir/HOST_SEQUENCE.txt"
printf '%s\n' "$evidence_dir"
exit "$remote_runner_result"
