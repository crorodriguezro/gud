#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
source_file="$repo_root/backport-4.9/tests/gud-kms-mode-sequence.c"
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT

cc -Wall -Wextra -Werror -O2 $(pkg-config --cflags libdrm) \
	-o "$build_dir/gud-kms-mode-sequence" "$source_file" \
	$(pkg-config --libs libdrm)

failures=0
require_source() {
	local text=$1

	grep -qF -- "$text" "$source_file" || {
		printf 'FAIL [required source]: %s\n' "$text" >&2
		failures=$((failures + 1))
	}
}

for text in \
	'--device PATH --list-modes' \
	'--mode-key' \
	'--frames N --pattern row-id --json PATH' \
	'GUD_DISPLAY_MODE_FLAG_USER_MASK' \
	'expected exactly one' \
	'DRM_FORMAT_RGB565' \
	'DRM_IOCTL_MODE_CREATE_DUMB' \
	'drmModeSetCrtc' \
	'rows_covered_total' \
	'source_bytes_total'; do
	require_source "$text"
done

self_test=$("$build_dir/gud-kms-mode-sequence" --self-test)
if [[ "$self_test" != '{"self_test":"ok","rows":4,"bytes":64}' ]]; then
	printf 'FAIL [self-test]: %s\n' "$self_test" >&2
	failures=$((failures + 1))
fi

set +e
"$build_dir/gud-kms-mode-sequence" >"$build_dir/usage.out" 2>"$build_dir/usage.err"
usage_rc=$?
"$build_dir/gud-kms-mode-sequence" \
	--device /definitely/missing --mode-key invalid \
	--frames 1 --pattern row-id --json "$build_dir/result.json" \
	>"$build_dir/missing.out" 2>"$build_dir/missing.err"
missing_rc=$?
"$build_dir/gud-kms-mode-sequence" \
	--device /dev/null --mode-key invalid \
	--frames 0 --pattern row-id --json "$build_dir/result.json" \
	>"$build_dir/frames.out" 2>"$build_dir/frames.err"
frames_rc=$?
set -e

if [[ $usage_rc -ne 2 ]] || ! grep -qF -- '--list-modes' "$build_dir/usage.err"; then
	printf 'FAIL [usage]: rc=%s\n' "$usage_rc" >&2
	failures=$((failures + 1))
fi
if [[ $missing_rc -eq 0 ]] || ! grep -qF 'open DRM device' "$build_dir/missing.err"; then
	printf 'FAIL [missing-device]: rc=%s\n' "$missing_rc" >&2
	failures=$((failures + 1))
fi
if [[ $frames_rc -ne 2 ]]; then
	printf 'FAIL [zero-frames]: rc=%s\n' "$frames_rc" >&2
	failures=$((failures + 1))
fi

printf 'gud-kms-mode-sequence-contract tests: %s failures\n' "$failures"
exit "$failures"
