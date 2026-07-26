#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
source_file="$repo_root/backport-4.9/tests/gud-kms-animate.c"
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT

cc -Wall -Wextra -Werror -O2 $(pkg-config --cflags libdrm) \
	-o "$build_dir/gud-kms-animate" "$source_file" \
	$(pkg-config --libs libdrm)

failures=0
require_source() {
	local text="$1"

	grep -qF "$text" "$source_file" || {
		printf 'FAIL [required source]: %s\n' "$text" >&2
		failures=$((failures + 1))
	}
}

require_source '#define BUFFER_COUNT 2U'
require_source 'WORKLOAD_SCROLL'
require_source 'WORKLOAD_DESKTOP'
require_source 'WORKLOAD_NOISE'
require_source 'WORKLOAD_RAW'
require_source 'raw clip ended before requested frame'
require_source 'drmModeAtomicAddProperty(request, plane_id,'
require_source 'props.plane_fb_id'
require_source 'commit_avg_ms='

set +e
"$build_dir/gud-kms-animate" >"$build_dir/stdout" 2>"$build_dir/stderr"
rc=$?
set -e
if [[ $rc -ne 2 ]] ||
   ! grep -qF '<scroll|desktop|noise|raw>' "$build_dir/stderr"; then
	printf 'FAIL [usage]: rc=%s\n' "$rc" >&2
	failures=$((failures + 1))
fi

printf 'gud-kms-animate-contract tests: %s failures\n' "$failures"
exit "$failures"
