#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT

cc -std=c11 -Wall -Wextra -Werror -O2 \
	"$repo_root/backport-4.9/tests/test-gud-transfer-retry.c" \
	-o "$build_dir/test-gud-transfer-retry"
"$build_dir/test-gud-transfer-retry"
