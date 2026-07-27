#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
decoder="$repo_root/backport-4.9/tests/decode-gud-usbmon-descriptor.py"
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT

valid_hex=4d61501d0100000000010000200020030000000f00005802000070080000
forbidden_hex=4d61501d0101000000010000200020030000000f00005802000070080000
failures=0

write_capture() {
	local path=$1
	local request=$2
	local payload=$3

	{
		printf 'ffff0001 1.000000 S Ci:1:002:0 s c1 %s 0000 0000 001e 30 <\n' "$request"
		printf 'ffff0001 1.000100 C Ci:1:002:0 0 30 = %s\n' "$payload"
	} >"$path"
}

write_capture "$test_dir/valid.usbmon" 01 "$valid_hex"
actual=$("$decoder" "$test_dir/valid.usbmon")
expected='{"magic":"0x1d50614d","version":1,"flags":0,"compression":1,"max_buffer_size":2097152,"min_width":800,"max_width":3840,"min_height":600,"max_height":2160}'
if [[ "$actual" != "$expected" ]]; then
	printf 'FAIL [valid-json]: %s\n' "$actual" >&2
	failures=$((failures + 1))
fi

expect_failure() {
	local name=$1
	local pattern=$2
	shift 2

	set +e
	"$decoder" "$@" >"$test_dir/$name.stdout" 2>"$test_dir/$name.stderr"
	local rc=$?
	set -e
	if [[ $rc -eq 0 ]] || ! grep -qF "$pattern" "$test_dir/$name.stderr"; then
		printf 'FAIL [%s]: rc=%s stderr=%s\n' \
			"$name" "$rc" "$(tr '\n' ' ' <"$test_dir/$name.stderr")" >&2
		failures=$((failures + 1))
	fi
}

write_capture "$test_dir/forbidden.usbmon" 01 "$forbidden_hex"
expect_failure forbidden-status-on-set \
	'forbidden GUD_DISPLAY_FLAG_STATUS_ON_SET' "$test_dir/forbidden.usbmon"

write_capture "$test_dir/truncated.usbmon" 01 "${valid_hex:0:40}"
expect_failure truncated 'is truncated' "$test_dir/truncated.usbmon"

write_capture "$test_dir/wrong-request.usbmon" 02 "$valid_hex"
expect_failure wrong-request 'no GUD_REQ_GET_DESCRIPTOR' "$test_dir/wrong-request.usbmon"

printf 'ffff0002 2.000000 S Ci:1:002:0 s c1 01 0000 0000 001e 30 <\n' \
	>"$test_dir/missing-completion.usbmon"
expect_failure missing-completion 'found 0: occurrence 1 has no completion' \
	"$test_dir/missing-completion.usbmon"

{
	cat "$test_dir/valid.usbmon"
	sed 's/ffff0001/ffff0003/g; s/1\\.000/3.000/g' "$test_dir/valid.usbmon"
} >"$test_dir/multiple.usbmon"
expect_failure multiple 'found 2: multiple usable descriptors' "$test_dir/multiple.usbmon"

selected=$("$decoder" --occurrence 2 "$test_dir/multiple.usbmon")
if [[ "$selected" != "$expected" ]]; then
	printf 'FAIL [explicit-occurrence]: %s\n' "$selected" >&2
	failures=$((failures + 1))
fi

PYTHONPYCACHEPREFIX="$test_dir/pycache" python3 -m py_compile "$decoder"
printf 'gud-usbmon-descriptor-contract tests: %s failures\n' "$failures"
exit "$failures"
