#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
failures=0

require() {
	local file="$1" text="$2"
	grep -qF "$text" "$repo_root/$file" || {
		printf 'FAIL [required]: %s: %s\n' "$file" "$text" >&2
		failures=$((failures + 1))
	}
}

for text in \
	'#include <drm/drm_atomic_helper.h>' \
	'#include <drm/drm_fourcc.h>' \
	'#include <drm/drm_simple_kms_helper.h>'; do
	require backport-4.9/gud_compat_4_9.h "$text"
done

require backport-4.9/gud_pipe.c 'drm_mode_config_reset(gud->drm);'

if grep -qF 'current_format' "$repo_root/backport-4.9/gud_pipe.c" ||
   grep -qF 'current_format' "$repo_root/backport-4.9/gud_internal.h"; then
    printf 'FAIL [stale state format cache remains in host driver]\n' >&2
    failures=$((failures + 1))
fi

pipe_file="$repo_root/backport-4.9/gud_pipe.c"
pipe_init_line=$(grep -nF 'ret = drm_simple_display_pipe_init' "$pipe_file" | cut -d: -f1)
reset_line=$(grep -nF 'drm_mode_config_reset(gud->drm);' "$pipe_file" | cut -d: -f1)
return_line=$(grep -nF 'return 0;' "$pipe_file" | tail -n 1 | cut -d: -f1)
if [ -z "$pipe_init_line" ] || [ -z "$reset_line" ] || [ -z "$return_line" ] ||
   [ "$reset_line" -le "$pipe_init_line" ] || [ "$reset_line" -ge "$return_line" ]; then
    printf 'FAIL [pipe reset ordering]\n' >&2
    failures=$((failures + 1))
fi

for text in \
    'GUD connector: detect enter' \
    'GUD connector: detect status=' \
    'GUD connector: get_modes enter' \
    'GUD connector: mode created' \
    'GUD connector: mode probed' \
    'GUD connector: get_modes complete'; do
    require backport-4.9/gud_connector.c "$text"
done

for text in \
	'struct drm_device *drm;' \
	'struct drm_simple_display_pipe pipe;' \
	'struct drm_connector connector;' \
	'int gud_drm_init(struct gud_device *gud);' \
	'void gud_drm_fini(struct gud_device *gud);' \
	'int gud_connector_init(struct gud_device *gud);' \
	'int gud_pipe_init(struct gud_device *gud);'; do
	require backport-4.9/gud_internal.h "$text"
done

for text in \
	'.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,' \
	'drm_dev_alloc(&gud_drm_driver, &gud->intf->dev)' \
	'.gem_free_object_unlocked = gud_gem_free_object,' \
	'.gem_vm_ops = &gud_gem_vm_ops,' \
	'.dumb_create = gud_gem_dumb_create,' \
	'.dumb_map_offset = gud_gem_dumb_map_offset,' \
	'.mmap = gud_drm_gem_mmap,' \
	'drm_unplug_dev(gud->drm);' \
	'drm_dev_register(gud->drm, 0)' \
	'drm_dev_unregister(drm);' \
	'drm_dev_unref(drm);' \
	'ret = gud_drm_init(gud);' \
	'gud->disconnected = true;'; do
	require backport-4.9/gud_drv.c "$text"
done

for text in \
	'DRM_FORMAT_RGB565' \
	'DRM_FORMAT_XRGB8888' \
	'drm_simple_display_pipe_init' \
	'if (plane_state->fb &&' \
	'plane_state->fb->pixel_format != DRM_FORMAT_RGB565' \
	'min_size + line_bytes > obj->size' \
	'drm_framebuffer_init' \
	'drm_gem_object_lookup' \
	'drm_atomic_helper_check' \
	'drm_atomic_helper_commit' \
	'GUD_REQ_SET_STATE_CHECK' \
	'GUD_REQ_SET_STATE_COMMIT' \
	'GUD_REQ_SET_CONTROLLER_ENABLE' \
	'GUD_REQ_SET_DISPLAY_ENABLE' \
	'GUD_REQ_SET_BUFFER' \
	'gud_gem_vmap' \
	'gud_usb_bulk_write' \
	'URB_NO_TRANSFER_DMA_MAP' \
	'usb_submit_urb' \
	'GUD_DISPLAY_FLAG_STATUS_ON_SET' \
	'drm_crtc_send_vblank_event' \
	'crtc->state->event = NULL;' \
	'spin_lock_irqsave(&crtc->dev->event_lock, flags);'; do
	require backport-4.9/gud_pipe.c "$text"
done

if grep -qE 'usb_bulk_msg[[:space:]]*\(' "$repo_root/backport-4.9/gud_pipe.c"; then
	printf 'FAIL [coherent bulk transfer bypasses its DMA address]: %s\n' \
		backport-4.9/gud_pipe.c >&2
	failures=$((failures + 1))
fi

for text in \
	'connector_status_connected' \
	'drm_connector_init' \
	'DRM_MODE_CONNECTOR_VIRTUAL' \
	'1280' \
	'720' \
	'74250' \
	'DRM_MODE_TYPE_PREFERRED'; do
	require backport-4.9/gud_connector.c "$text"
done

for file in backport-4.9/gud_connector.c; do
	if grep -qE 'usb_(control_msg|bulk_msg)|GUD_REQ_|gud_gem_vmap' "$repo_root/$file"; then
		printf 'FAIL [forbidden transfer]: %s\n' "$file" >&2
		failures=$((failures + 1))
	fi
done

printf 'drm-kms-contract tests: %s failures\n' "$failures"
exit "$failures"
