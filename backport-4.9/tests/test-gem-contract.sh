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
    '#include <drm/drmP.h>' \
    '#include <drm/drm_gem.h>' \
    '#include <linux/mm.h>'; do
    require backport-4.9/gud_compat_4_9.h "$text"
done

for text in \
    '#include "gud_compat_4_9.h"' \
    'struct gud_gem_object {' \
    'struct drm_gem_object base;' \
    'struct page **pages;' \
    'void *vaddr;' \
    'struct gud_gem_object *gud_gem_create' \
    'int gud_gem_get_pages' \
    'int gud_gem_vmap' \
    'void gud_gem_free_object' \
    'int gud_gem_dumb_create' \
    'int gud_gem_dumb_map_offset' \
    'int gud_drm_gem_mmap' \
    'extern const struct vm_operations_struct gud_gem_vm_ops;'; do
    require backport-4.9/gud_internal.h "$text"
done

for text in \
    'drm_simple_display_pipe' \
    'drm_dev_register' \
    'drm_mode_config' \
    'GUD_REQ_SET_BUFFER' \
    'dma_buf' \
    'module_param'; do
    if grep -qF "$text" "$repo_root/backport-4.9/gud_gem_4_9.c" 2>/dev/null; then
        printf 'FAIL [forbidden]: gud_gem_4_9.c: %s\n' "$text" >&2
        failures=$((failures + 1))
    fi
done

for text in \
    'drm_gem_object_init(dev, &obj->base, size)' \
    'obj->pages = drm_gem_get_pages(&obj->base);' \
    'obj->vaddr = vmap(obj->pages' \
    'PAGE_KERNEL);' \
    'if (obj->base.import_attach)' \
    'return -EOPNOTSUPP;' \
    'vunmap(obj->vaddr);' \
    'drm_gem_put_pages(&obj->base, obj->pages, false, false);' \
    'drm_gem_object_release(gem);' \
    'kfree(obj);'; do
    require backport-4.9/gud_gem_4_9.c "$text"
done

for text in \
    'if (args->bpp != 16 && args->bpp != 32)' \
    'if (!args->width || !args->height)' \
    'bytes_per_pixel = args->bpp / 8;' \
    'if (args->width > U32_MAX / bytes_per_pixel)' \
    'if (args->height > SIZE_MAX / pitch)' \
    'ret = drm_gem_handle_create(file, &obj->base, &args->handle);' \
    'drm_gem_object_unreference_unlocked(&obj->base);' \
    'obj = drm_gem_object_lookup(file, handle);' \
    'ret = drm_gem_create_mmap_offset(obj);' \
    '*offset = drm_vma_node_offset_addr(&obj->vma_node);' \
    'ret = drm_gem_mmap(filp, vma);' \
    'vma->vm_flags &= ~VM_PFNMAP;' \
    'vma->vm_flags |= VM_MIXEDMAP;' \
    'vm_insert_page(vma, (unsigned long)vmf->virtual_address, page);' \
    '.fault = gud_gem_fault,' \
    '.open = drm_gem_vm_open,' \
    '.close = drm_gem_vm_close,'; do
    require backport-4.9/gud_gem_4_9.c "$text"
done

for text in \
    'gud_gem_4_9.o' \
    'drm_gem_get_pages' \
    'drm_gem_put_pages' \
    'drm_gem_mmap' \
    'drm_gem_handle_create' \
    'vmap' \
    'vm_insert_page' \
    'Ticket 4' \
    'create, map, write, and destroy'; do
    require backport-4.9/README.md "$text"
done

printf 'gem-contract tests: %s failures\n' "$failures"
exit "$failures"
