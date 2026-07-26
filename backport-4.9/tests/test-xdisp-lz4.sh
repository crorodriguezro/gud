#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT

compiler_flags=(-std=c11 -Wall -Wextra -Werror -O2)
if [ "${SANITIZE:-0}" = 1 ]; then
	compiler_flags+=(
		-O1 -g -fsanitize=address,undefined
		-fno-omit-frame-pointer
	)
fi

cc "${compiler_flags[@]}" \
	-I"$repo_root/backport-4.9/variants/xdisp-lz4-12800" \
	"$repo_root/backport-4.9/tests/test-xdisp-lz4.c" \
	"$repo_root/backport-4.9/variants/xdisp-lz4-12800/gud_xdisp_lz4.c" \
	-Wl,-l:liblz4.so.1 \
	-o "$build_dir/test-xdisp-lz4"

if [ "${SANITIZE:-0}" = 1 ]; then
	ASAN_OPTIONS=detect_leaks=0 "$build_dir/test-xdisp-lz4"
else
	"$build_dir/test-xdisp-lz4"
fi
