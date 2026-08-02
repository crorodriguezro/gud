#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
stage=${STAGE:-}
phone_host=${PHONE_HOST:-}
phone_password=${PHONE_SUDO_PASSWORD:-}
module_path=${MODULE_PATH:-$script_dir/../gud.ko}
stage_binary=${STAGE_BINARY:-$script_dir/../tests/gud-kms-stage}
ssh_bin=${SSH:-ssh}
scp_bin=${SCP:-scp}
evidence_dir=${EVIDENCE_DIR:-$script_dir/local/evidence}

case "$stage" in
caps|dumb|fb|resources|connector|encoder-crtc|planes|properties|atomic-build|atomic-test|atomic-commit) ;;
*) printf 'STAGE must be caps, dumb, fb, resources, connector, encoder-crtc, planes, properties, atomic-build, atomic-test, or atomic-commit\n' >&2; exit 2 ;;
esac

if [ -z "$phone_host" ] || [ -z "$phone_password" ]; then
	printf 'PHONE_HOST and PHONE_SUDO_PASSWORD are required\n' >&2
	exit 2
fi
if [ ! -f "$module_path" ] || [ ! -x "$stage_binary" ]; then
	printf 'MODULE_PATH and executable STAGE_BINARY are required\n' >&2
	exit 2
fi

mkdir -p "$evidence_dir"
stdout="$evidence_dir/drm-kms-stage-$stage-stdout.txt"
stderr="$evidence_dir/drm-kms-stage-$stage-stderr.txt"
status="$evidence_dir/drm-kms-stage-$stage-exit.txt"
dmesg="$evidence_dir/drm-kms-stage-$stage-dmesg.txt"
rm -f "$stdout" "$stderr" "$status" "$dmesg"

remote_module=/home/phablet/gud.ko
remote_stage=/home/phablet/gud-kms-stage

"$scp_bin" -o BatchMode=yes "$module_path" "$stage_binary" \
	"$phone_host:/home/phablet/"

"$ssh_bin" -o BatchMode=yes "$phone_host" \
	"printf '%s\\n' '$phone_password' | sudo -S dmesg -C; \
	 printf '%s\\n' '$phone_password' | sudo -S sh -c 'printf host > /sys/bus/platform/devices/a600000.ssusb/mode'; \
	 i=0; found_pi=0; \
	 while [ \"\$i\" -lt 15 ]; do \
	   for path in /sys/bus/usb/devices/*/idVendor; do \
	     test -r \"\$path\" || continue; \
	     test \"\$(cat \"\$path\")\" = 1d50 || continue; \
	     test \"\$(cat \"\${path%/*}/idProduct\")\" = 614d || continue; \
	     printf 'FOUND: %s\\n' \"\${path%/*}\"; found_pi=1; break; \
	   done; \
	   test \$found_pi = 1 && break; \
	   i=\$((i + 1)); sleep 2; \
	 done; \
	 test \$found_pi = 1 || { echo 'Pi GUD 1d50:614d is not enumerated' >&2; exit 2; }; \
	 printf '%s\\n' '$phone_password' | sudo -S rmmod gud 2>/dev/null || true; \
	 printf '%s\\n' '$phone_password' | sudo -S insmod $remote_module; \
	 i=0; probed=0; \
	 while [ "\$i" -lt 10 ]; do \
	   if printf '%s\\n' '$phone_password' | sudo -S dmesg | grep -qF 'GUD probe complete for 1d50:614d' && \
	      test -e /dev/dri/card1; then \
	     probed=1; break; \
	   fi; \
	 i=\$((i + 1)); sleep 1; \
	 done; \
	 test \$probed = 1 || { echo 'fresh GUD probe/card1 did not complete' >&2; exit 2; }"

"$ssh_bin" -o BatchMode=yes -o ServerAliveInterval=2 -o ServerAliveCountMax=3 \
	"$phone_host" "printf '%s\\n' '$phone_password' | sudo -S dmesg -w" \
	>"$dmesg" 2>&1 &
watcher=$!
sleep 2
set +e
timeout 25s "$ssh_bin" -o BatchMode=yes -o ServerAliveInterval=2 \
	-o ServerAliveCountMax=3 "$phone_host" \
	"timeout --signal=TERM --kill-after=2s 20s $remote_stage $stage /dev/dri/card1" \
	>"$stdout" 2>"$stderr"
rc=$?
set -e
printf '%s\n' "$rc" >"$status"
kill "$watcher" 2>/dev/null || true
wait "$watcher" 2>/dev/null || true

if [ "$rc" -eq 0 ] && ! grep -qFx "stage=$stage complete" "$stdout"; then
	printf 'missing completion marker for stage %s\n' "$stage" >>"$stderr"
	rc=1
	printf '%s\n' "$rc" >"$status"
fi

exit "$rc"
