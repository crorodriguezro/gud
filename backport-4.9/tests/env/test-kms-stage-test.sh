#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/ssh" <<'EOF'
#!/usr/bin/env bash
printf 'ssh %s\n' "$*" >>"$(dirname "$0")/calls.log"
touch "$(dirname "$0")/ssh-called"
if [ -n "${MOCK_STAGE_OUTPUT:-}" ] && printf '%s\n' "$*" | grep -q '/home/phablet/gud-kms-stage'; then
    printf '%s\n' "$MOCK_STAGE_OUTPUT"
fi
exit 0
EOF
chmod +x "$tmp/ssh"

cat > "$tmp/scp" <<'EOF'
#!/usr/bin/env bash
printf 'scp %s\n' "$*" >>"$(dirname "$0")/calls.log"
touch "$(dirname "$0")/scp-called"
exit 0
EOF
chmod +x "$tmp/scp"

set +e
output=$(STAGE=invalid PHONE_HOST=phablet@example.invalid PHONE_SUDO_PASSWORD=x \
	SSH="$tmp/ssh" SCP="$tmp/scp" EVIDENCE_DIR="$tmp/evidence" \
	bash "$repo_root/backport-4.9/env/kms-stage-test.sh" 2>&1)
status=$?
set -e
test "$status" -eq 2
printf '%s' "$output" | grep -qF 'STAGE must be caps, dumb, fb, resources, connector, encoder-crtc, planes, properties, atomic-build, atomic-test, or atomic-commit'
test ! -e "$tmp/ssh-called"
test ! -e "$tmp/scp-called"

printf '# mock gud module\n' >"$tmp/gud.ko"
cat > "$tmp/gud-kms-stage" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
chmod +x "$tmp/gud-kms-stage"

set +e
output=$(MOCK_STAGE_OUTPUT='stage=resources complete' \
	STAGE=resources PHONE_HOST=phablet@example.invalid PHONE_SUDO_PASSWORD=x \
	MODULE_PATH="$tmp/gud.ko" STAGE_BINARY="$tmp/gud-kms-stage" \
	SSH="$tmp/ssh" SCP="$tmp/scp" EVIDENCE_DIR="$tmp/evidence" \
	bash "$repo_root/backport-4.9/env/kms-stage-test.sh" 2>&1)
status=$?
set -e
test "$status" -eq 0
if printf '%s' "$output" | grep -qF 'STAGE must be'; then
	printf 'runner rejected resources unexpectedly\n' >&2
	exit 1
fi
test -e "$tmp/ssh-called"
test -e "$tmp/scp-called"
grep -qF 'idVendor' "$tmp/calls.log"
grep -qF 'GUD probe complete for 1d50:614d' "$tmp/calls.log"

for text in \
	'dmesg -w' \
	'timeout --signal=TERM --kill-after=2s 20s' \
	'drm-kms-stage-$stage-stdout.txt' \
	'drm-kms-stage-$stage-stderr.txt' \
	'drm-kms-stage-$stage-exit.txt' \
	'drm-kms-stage-$stage-dmesg.txt'; do
	grep -qF "$text" "$repo_root/backport-4.9/env/kms-stage-test.sh"
done
if grep -qF 'adb ' "$repo_root/backport-4.9/env/kms-stage-test.sh"; then
	printf 'runner must not use adb\n' >&2
	exit 1
fi

printf 'kms-stage-test tests: 0 failures\n'
