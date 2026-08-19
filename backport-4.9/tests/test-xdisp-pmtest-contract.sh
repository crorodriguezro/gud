#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
production="$repo_root/backport-4.9/variants/xdisp-lz4-12800"
pmtest="$repo_root/backport-4.9/variants/xdisp-lz4-12800-pmtest"
driver="$repo_root/backport-4.9/gud_drv.c"
pipe="$repo_root/backport-4.9/gud_pipe.c"
internal="$repo_root/backport-4.9/gud_internal.h"
failures=0

require() {
	local file="$1" text="$2"

	grep -qF "$text" "$file" || {
		printf 'FAIL [required]: %s: %s\n' "$file" "$text" >&2
		failures=$((failures + 1))
	}
}

require "$pmtest/variant_drv.c" '#define GUD_XDISP_PM_TEST 1'
require "$pmtest/Kbuild" 'variant_lz4.o variant_lz4_upstream.o'
require "$driver" 'GUD_PM_TEST suspend result=-EBUSY active_transfer=1'
require "$driver" 'GUD_PM_TEST suspend result=0 active_transfer=0'
require "$driver" 'GUD_PM_TEST resume result=0'
require "$driver" 'GUD_PM_TEST reset_resume observed'
require "$driver" '.supports_autosuspend = 1'
require "$pipe" 'gud_pm_submission_allowed(gud)'
require "$internal" 'return gud->pm_suspended ? -EHOSTUNREACH : 0;'

if grep -qF 'GUD_XDISP_PM_TEST' "$production/variant_drv.c"; then
	printf 'FAIL [production isolation]: PM test define leaked into production variant\n' >&2
	failures=$((failures + 1))
fi

printf 'xdisp-pmtest-contract tests: %s failures\n' "$failures"
exit "$failures"
