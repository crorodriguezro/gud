#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pipe=$root/gud_pipe.c
internal=$root/gud_internal.h
driver=$root/gud_drv.c
failures=0

require()
{
	pattern=$1
	file=$2
	label=$3
	if ! grep -Eq "$pattern" "$file"; then
		printf 'FAIL: %s\n' "$label" >&2
		failures=$((failures + 1))
	fi
}

require 'static bool gud_async_flush;' "$pipe" 'async_flush defaults off'
require 'module_param_named\(async_flush, gud_async_flush, bool, 0644\)' "$pipe" 'async_flush is optional at runtime'
require 'struct work_struct async_work;' "$internal" 'one embedded work item'
require 'struct mutex damage_lock;' "$internal" 'pending damage is serialized'
require 'struct drm_framebuffer \*async_fb;' "$internal" 'one pending framebuffer reference'
require 'void \*shadow_buf;' "$internal" 'one shadow framebuffer'
require 'drm_framebuffer_reference\(fb\)' "$pipe" 'queued framebuffer is referenced'
require 'drm_framebuffer_unreference\(fb\)' "$pipe" 'queued framebuffer is released'
require 'queue_work\(system_long_wq, &gud->async_work\)' "$pipe" 'upstream long workqueue is used'
require 'cancel_work_sync\(&gud->async_work\)' "$pipe" 'pending work is synchronously quiesced'
require 'if \(ret != -ENOMEM\)' "$pipe" 'only allocation failure falls back synchronously'
require 'gud_async_stop\(gud\)' "$driver" 'disconnect/unload stop async work'
require 'vzalloc\(length\)' "$pipe" '4.9 shadow allocation backport'

if [ "$failures" -ne 0 ]; then
	printf 'xdisp async contract tests: %d failures\n' "$failures" >&2
	exit 1
fi

printf 'xdisp async contract tests: 0 failures\n'
