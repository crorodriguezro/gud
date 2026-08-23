#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
variant="$repo_root/backport-4.9/variants/xdisp-lz4-12800"
pipe="$repo_root/backport-4.9/gud_pipe.c"
driver="$repo_root/backport-4.9/gud_drv.c"
failures=0

require() {
	local file="$1" text="$2"

	grep -qF "$text" "$file" || {
		printf 'FAIL [required]: %s: %s\n' "$file" "$text" >&2
		failures=$((failures + 1))
	}
}

require "$variant/gud_xdisp_lz4.h" \
	'#define GUD_XDISP_DEFAULT_PAYLOAD_LIMIT 12800U'
require "$variant/gud_xdisp_lz4.h" \
	'#define GUD_XDISP_MAX_PAYLOAD_LIMIT (4U * 1024U * 1024U)'
require "$variant/gud_xdisp_lz4.c" \
	'gud_xdisp_plan_chunk_bounded_frame('
require "$pipe" 'struct gud_xdisp_bounded_frame bounded_frame'
require "$pipe" 'static unsigned int gud_bulk_timeout_ms = GUD_USB_TIMEOUT_MS;'
require "$pipe" 'module_param_named(bulk_timeout_ms, gud_bulk_timeout_ms, uint, 0644);'
require "$pipe" 'module_param_named(bulk_trace_limit, gud_bulk_trace_limit, uint, 0644);'
require "$pipe" 'module_param_named(xdisp_payload_timing, xdisp_payload_timing, bool, 0644);'
require "$pipe" 'msecs_to_jiffies(gud_bulk_timeout_ms)'
require "$pipe" 'GUD trace=%llu SET_BUFFER'
require "$pipe" 'GUD trace=%llu bulk attempt='
require "$pipe" 'xdisp_payload_timing frame_seq=%llu'
require "$pipe" 'bulk_completion_callback_ns=%llu'
require "$pipe" 'set_buffer_result=%d bulk_result=%d'
require "$pipe" 'xdisp_probe_payload_length'
require "$pipe" 'XDISP_PROBE complete'
require "$pipe" 'URB_ZERO_PACKET'
require "$pipe" 'raw_backoff_rectangles=%u'
require "$pipe" 'chunk.payload_length > gud_xdisp_payload_limit'
require "$pipe" 'request.length = cpu_to_le32(chunk.source_length);'
require "$pipe" 'request.compression = GUD_COMPRESSION_LZ4;'
require "$pipe" 'request.compressed_length ='
require "$pipe" 'transfer_length = chunk.payload_length;'
require "$pipe" 'transfer_length, &actual, trace, retries, &bulk_timing);'
require "$pipe" 'actual != transfer_length'
require "$pipe" 'compress_attempts=%llu'
require "$pipe" 'compress_rejected=%llu'
require "$pipe" 'compress_source_bytes=%llu'
require "$driver" 'XDISP diagnostic variant'
require "$driver" 'module_param_named(xdisp_payload_limit, gud_xdisp_payload_limit, uint, 0444);'
require "$variant/Kbuild" 'obj-m += gud.o'
require "$variant/Kbuild" 'gud_xdisp_lz4.o'

for forbidden in \
	'usb_bulk_msg(' \
	'lz4_compress('; do
	if grep -qF "$forbidden" "$pipe"; then
		printf 'FAIL [forbidden pipe path]: %s\n' "$forbidden" >&2
		failures=$((failures + 1))
	fi
done

printf 'xdisp-lz4-contract tests: %s failures\n' "$failures"
exit "$failures"
