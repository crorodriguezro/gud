#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT

cc -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror -O2 \
	"$repo_root/backport-4.9/tests/test-gud-kms-pattern.c" \
	$(pkg-config --cflags --libs libdrm) \
	-o "$build_dir/test-gud-kms-pattern"
"$build_dir/test-gud-kms-pattern"
