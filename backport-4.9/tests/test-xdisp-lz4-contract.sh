#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
variant="$repo_root/backport-4.9/variants/xdisp-lz4-12800"
pipe="$repo_root/backport-4.9/gud_pipe.c"
driver="$repo_root/backport-4.9/gud_drv.c"
internal="$repo_root/backport-4.9/gud_internal.h"
failures=0

require() {
	local file="$1" text="$2"

	grep -qF "$text" "$file" || {
		printf 'FAIL [required]: %s: %s\n' "$file" "$text" >&2
		failures=$((failures + 1))
	}
}

forbid() {
	local file="$1" text="$2"

	if grep -qF "$text" "$file"; then
		printf 'FAIL [forbidden]: %s: %s\n' "$file" "$text" >&2
		failures=$((failures + 1))
	fi
}

require "$variant/gud_xdisp_lz4.h" \
	'#define GUD_XDISP_MAX_PAYLOAD_LIMIT (64U * 1024U * 1024U)'
require "$pipe" 'max_rows = min_t(size_t, gud->xdisp_max_source_length, length) /'
require "$pipe" 'gud_xdisp_lz4_compress_limited('
require "$pipe" 'compressed_length && compressed_length < source_length'
require "$pipe" 'payload_length = source_length;'
require "$pipe" 'request.length = cpu_to_le32(source_length);'
require "$pipe" 'request.compressed_length = cpu_to_le32(payload_length);'
require "$pipe" 'source_length > gud->xdisp_max_source_length'
require "$pipe" 'payload_length > gud->xdisp_bulk_buffer_size'
require "$pipe" 'gud_set_buffer_should_retry(ret,'
require "$pipe" 'URB_NO_TRANSFER_DMA_MAP'
require "$pipe" 'actual != transfer_length'
require "$driver" 'XDISP full-update variant:'
require "$driver" 'gud_xdisp_full_update'
require "$internal" 'size_t xdisp_bulk_buffer_size;'
require "$variant/Kbuild" 'gud_xdisp_lz4_upstream.o'

for symbol in \
	xdisp_payload_limit xdisp_target_policy xdisp_ratio_cache \
	xdisp_bounded_discovery xdisp_predictive_bounded \
	gud_xdisp_plan_chunk row_hint predictive_rows; do
	forbid "$pipe" "$symbol"
	done
forbid "$variant/Kbuild" 'gud_xdisp_lz4.o'
forbid "$pipe" 'usb_bulk_msg('

printf 'xdisp full-update contract tests: %s failures\n' "$failures"
exit "$failures"
