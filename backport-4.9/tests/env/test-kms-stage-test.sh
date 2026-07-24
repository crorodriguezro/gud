#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/ssh" <<'EOF'
#!/usr/bin/env bash
touch "$(dirname "$0")/ssh-called"
exit 99
EOF
chmod +x "$tmp/ssh"

set +e
output=$(STAGE=invalid PHONE_HOST=phablet@example.invalid PHONE_SUDO_PASSWORD=x \
	SSH="$tmp/ssh" SCP="$tmp/ssh" EVIDENCE_DIR="$tmp/evidence" \
	bash "$repo_root/backport-4.9/env/kms-stage-test.sh" 2>&1)
status=$?
set -e
test "$status" -eq 2
printf '%s' "$output" | grep -qF 'STAGE must be caps, dumb, fb, or atomic'
test ! -e "$tmp/ssh-called"

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
