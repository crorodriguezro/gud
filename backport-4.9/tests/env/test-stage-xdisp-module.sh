#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
runner="$repo_root/backport-4.9/env/stage-xdisp-module.sh"
fixture="$repo_root/backport-4.9/tests/fixtures/fake-stage-command.sh"
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
system_path="$PATH"

fake_bin="$test_dir/bin"
mkdir -p "$fake_bin"
for command_name in modinfo nm ssh scp; do
	ln -s "$fixture" "$fake_bin/$command_name"
done

module="$test_dir/gud.ko"
printf 'diagnostic-module-fixture\n' > "$module"
local_sha=$(sha256sum "$module" | awk '{print $1}')
normal_sha=bd15c2c1bc4cd941bcac88bb13276b67620d9e2eec515973ff815add68f3630c
remote_path=/home/phablet/gud.xdisp-p0.1-adaptive-12800-abcdef1.ko
export PATH="$fake_bin:$PATH"
export FAKE_LOCAL_SHA="$local_sha"
export FAKE_NORMAL_SHA="$normal_sha"
export FAKE_SCP_CALLED="$test_dir/scp-called"
export FAKE_MV_CALLED="$test_dir/mv-called"

PATH="$fake_bin:$PATH" \
MODULE_PATH="$module" \
REMOTE_MODULE_PATH="$remote_path" \
EVIDENCE_DIR="$test_dir/evidence-success" \
"$runner" > "$test_dir/success.out"

test -e "$FAKE_SCP_CALLED"
test -e "$FAKE_MV_CALLED"
grep -qF 'staged_only=true' "$test_dir/evidence-success/stage-result.txt"
grep -qF "diagnostic_sha256=$local_sha" \
	"$test_dir/evidence-success/stage-result.txt"
grep -qF "normal_sha256_after=$normal_sha" \
	"$test_dir/evidence-success/stage-result.txt"

if MODULE_PATH="$module" \
   REMOTE_MODULE_PATH=/home/phablet/gud.ko \
   EVIDENCE_DIR="$test_dir/evidence-unsafe" \
   "$runner" > "$test_dir/unsafe.out" 2>&1; then
	printf 'unsafe normal-module target was accepted\n' >&2
	exit 1
fi
grep -qF 'refusing unsafe diagnostic path' "$test_dir/unsafe.out"

rm -f "$FAKE_SCP_CALLED" "$FAKE_MV_CALLED"
if FAKE_NORMAL_SHA=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff \
   MODULE_PATH="$module" \
   REMOTE_MODULE_PATH="$remote_path" \
   EVIDENCE_DIR="$test_dir/evidence-hash" \
   "$runner" > "$test_dir/hash.out" 2>&1; then
	printf 'normal-module hash mismatch was accepted\n' >&2
	exit 1
fi
grep -qF 'normal module hash mismatch' "$test_dir/hash.out"
test ! -e "$FAKE_SCP_CALLED"

if FAKE_REMOTE_EXISTS=1 \
   MODULE_PATH="$module" \
   REMOTE_MODULE_PATH="$remote_path" \
   EVIDENCE_DIR="$test_dir/evidence-exists" \
   "$runner" > "$test_dir/exists.out" 2>&1; then
	printf 'existing diagnostic path was overwritten\n' >&2
	exit 1
fi
grep -qF 'already exists' "$test_dir/exists.out"

rm -f "$FAKE_SCP_CALLED" "$FAKE_MV_CALLED"
if FAKE_INCOMING_SHA=eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee \
   MODULE_PATH="$module" \
   REMOTE_MODULE_PATH="$remote_path" \
   EVIDENCE_DIR="$test_dir/evidence-incoming" \
   "$runner" > "$test_dir/incoming.out" 2>&1; then
	printf 'incoming diagnostic hash mismatch was accepted\n' >&2
	exit 1
fi
grep -qF 'staged incoming hash mismatch' "$test_dir/incoming.out"
test -e "$FAKE_SCP_CALLED"
test ! -e "$FAKE_MV_CALLED"

real_module="$repo_root/backport-4.9/variants/xdisp-lz4-12800/gud.ko"
if [ -f "$real_module" ]; then
	network_bin="$test_dir/network-bin"
	mkdir -p "$network_bin"
	ln -s "$fixture" "$network_bin/ssh"
	ln -s "$fixture" "$network_bin/scp"
	real_sha=$(sha256sum "$real_module" | awk '{print $1}')
	rm -f "$FAKE_SCP_CALLED" "$FAKE_MV_CALLED"
	FAKE_LOCAL_SHA="$real_sha" \
	PATH="$network_bin:$system_path" \
	MODULE_PATH="$real_module" \
	REMOTE_MODULE_PATH=/home/phablet/gud.xdisp-p0.1-adaptive-12800-abcdef2.ko \
	EVIDENCE_DIR="$test_dir/evidence-real-module" \
	"$runner" > "$test_dir/real-module.out"
	test -e "$FAKE_SCP_CALLED"
	test -e "$FAKE_MV_CALLED"
fi

printf 'stage-xdisp-module tests: passed\n'
