#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
failures=0

# ── Required symbols in the two headers ──────────────────────────────────────
for text in \
    'GUD_DISPLAY_MAGIC' \
    'GUD_REQ_GET_DESCRIPTOR' \
    'struct gud_display_descriptor_req' \
    '} __packed;' \
    'struct gud_device' \
    'struct usb_device *usb;' \
    'struct usb_interface *intf;' \
    'struct mutex lock;' \
    'bool disconnected;'; do
    grep -qF "$text" \
        "$repo_root/backport-4.9/gud_protocol.h" \
        "$repo_root/backport-4.9/gud_internal.h" 2>/dev/null || {
        printf 'FAIL [required-symbol]: "%s" not found in protocol/internal headers\n' "$text" >&2
        failures=$((failures + 1))
    }
done

# ── Forbidden patterns in the two headers ────────────────────────────────────
for text in \
    'drm_' \
    'work_struct' \
    'usb_anchor' \
    'module_param'; do
    if grep -qF "$text" \
           "$repo_root/backport-4.9/gud_protocol.h" \
           "$repo_root/backport-4.9/gud_internal.h" 2>/dev/null; then
        printf 'FAIL [forbidden-symbol]: "%s" found in headers\n' "$text" >&2
        failures=$((failures + 1))
    fi
done

# ── Kbuild must name gud_drv.o (always checked) ──────────────────────────────
# Check that both required lines are present (allow ccflags-y additions)
kbuild_file="$repo_root/backport-4.9/Kbuild"
for kbuild_line in 'obj-m += gud.o' 'gud-y := gud_drv.o'; do
    grep -qxF "$kbuild_line" "$kbuild_file" 2>/dev/null || {
        printf 'FAIL [kbuild]: "%s" not found in Kbuild\n' "$kbuild_line" >&2
        failures=$((failures + 1))
    }
done

# ── Required symbols in gud_drv.c (only checked when it exists) ──────────────
if [ -f "$repo_root/backport-4.9/gud_drv.c" ]; then
    for text in \
        'USB_DEVICE(0x1d50, 0x614d)' \
        'MODULE_DEVICE_TABLE(usb, gud_id_table)' \
        'usb_endpoint_is_bulk_out' \
        'GUD_REQ_GET_DESCRIPTOR' \
        'USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_IN' \
        'le32_to_cpu(desc.magic) != GUD_DISPLAY_MAGIC' \
        'desc.version != GUD_PROTOCOL_VERSION' \
        'usb_set_intfdata(intf, gud);' \
        'usb_set_intfdata(intf, NULL);' \
        'usb_put_dev(gud->usb);' \
        'kfree(gud);' \
        'module_usb_driver(gud_usb_driver)'; do
        grep -qF "$text" "$repo_root/backport-4.9/gud_drv.c" || {
            printf 'FAIL [driver-required]: "%s" not in gud_drv.c\n' "$text" >&2
            failures=$((failures + 1))
        }
    done

    # Forbidden patterns in gud_drv.c
    for text in \
        'drm_' \
        'alloc_workqueue' \
        'INIT_WORK' \
        'queue_work' \
        'usb_submit_urb' \
        'module_param' \
        'GUD_REQ_SET_BUFFER'; do
        if grep -qF "$text" "$repo_root/backport-4.9/gud_drv.c"; then
            printf 'FAIL [driver-forbidden]: "%s" found in gud_drv.c\n' "$text" >&2
            failures=$((failures + 1))
        fi
    done

fi

printf 'usb-probe-contract tests: %s failures\n' "$failures"
exit "$failures"
