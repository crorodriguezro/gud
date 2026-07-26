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
	'#define GUD_XDISP_PAYLOAD_LIMIT 12800U'
require "$pipe" 'chunk.payload_length > GUD_XDISP_PAYLOAD_LIMIT'
require "$pipe" 'request.length = cpu_to_le32(chunk.source_length);'
require "$pipe" 'request.compression = GUD_COMPRESSION_LZ4;'
require "$pipe" 'request.compressed_length ='
require "$pipe" 'transfer_length = chunk.payload_length;'
require "$pipe" 'transfer_length, &actual);'
require "$pipe" 'actual != transfer_length'
require "$driver" 'XDISP diagnostic variant'
require "$variant/Kbuild" 'obj-m += gud.o'
require "$variant/Kbuild" 'gud_xdisp_lz4.o'

for forbidden in \
	'usb_bulk_msg(' \
	'URB_ZERO_PACKET' \
	'lz4_compress('; do
	if grep -qF "$forbidden" "$pipe"; then
		printf 'FAIL [forbidden pipe path]: %s\n' "$forbidden" >&2
		failures=$((failures + 1))
	fi
done

printf 'xdisp-lz4-contract tests: %s failures\n' "$failures"
exit "$failures"
