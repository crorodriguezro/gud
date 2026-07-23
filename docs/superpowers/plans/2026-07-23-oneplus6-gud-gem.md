# OnePlus 6 GUD Linux 4.9 GEM Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a page-backed, CPU-readable local GEM layer to `gud.ko` that Ticket 4 can attach to a Linux 4.9 DRM device.

**Architecture:** `gud_gem_4_9.c` owns Linux 4.9 GEM object allocation, lazy shmem-page pinning, a cached kernel `vmap()`, and future DRM callbacks. The layer owns local GEM objects only and does not add a DRM device, KMS object, USB transfer, PRIME integration, or a runtime module parameter. Ticket 4 attaches the callbacks and performs the first userspace dumb-buffer test.

**Tech Stack:** Downstream OnePlus 6 Linux 4.9.112-g6b190d86b arm64, out-of-tree Kbuild module, DRM GEM core, `vmap()`, Linux VM fault operations, POSIX shell contract tests.

## Global Constraints

- Do not patch DRM core or replace/flash the phone kernel.
- Use only APIs exported by the exact target kernel; `CONFIG_MODVERSIONS=y` requires matching `Module.symvers`.
- Keep all GUD host-driver source in `backport-4.9/` and use explicit cleanup rather than `drmm_*` or `drm_gem_shmem_*`.
- Use page-backed, GUD-owned storage through `drm_gem_get_pages()` and `drm_gem_put_pages()`; do not introduce CMA, scatter-gather, PRIME, or dma-buf import/export.
- Keep the first buffer allocation convention to 32 bpp. Ticket 4 alone advertises and validates XRGB8888.
- Ticket 3 must not register a DRM device, connector, pipe, framebuffer, mode, workqueue, USB transfer, or userspace DRM node.
- The cached kernel `vmap()` remains valid until the GEM object free callback; individual callers do not release it.
- Do not claim phone runtime validation for Ticket 3. Ticket 4 owns the first userspace create/map/write/destroy test after `/dev/dri/cardX` exists.
- Preserve unrelated untracked files and generated `.gud*.cmd` artifacts.

---

## File Structure

- `backport-4.9/gud_compat_4_9.h`: one boundary for Linux 4.9 DRM/MM headers; it includes `drmP.h` for the legacy DRM driver callback types.
- `backport-4.9/gud_internal.h`: exports the GUD GEM object layout and callback/helper declarations used by future driver code.
- `backport-4.9/gud_gem_4_9.c`: owns allocation, page lifetime, cached CPU mapping, dumb-buffer creation, mmap-offset handling, file mmap setup, VM fault insertion, and free callback.
- `backport-4.9/Kbuild`: links `gud_gem_4_9.o` into `gud.ko`.
- `backport-4.9/tests/test-gem-contract.sh`: hermetic structural contract test for Ticket 3 source and scope boundaries.
- `backport-4.9/tests/test-usb-probe-contract.sh`: relaxes only the Ticket 2 header prohibition so the shared internal header may declare GEM types; it must continue prohibiting DRM/KMS in `gud_drv.c`.
- `backport-4.9/README.md`: records the exact Ticket 3 build and symbol-audit commands plus the deferred Ticket 4 runtime test.

## Required Interfaces

`backport-4.9/gud_internal.h` exports these interfaces for Ticket 4 and Ticket 5:

```c
struct gud_gem_object {
	struct drm_gem_object base;
	struct page **pages;
	void *vaddr;
};

struct gud_gem_object *gud_gem_create(struct drm_device *dev, size_t size);
int gud_gem_get_pages(struct gud_gem_object *obj);
int gud_gem_vmap(struct gud_gem_object *obj, void **vaddr, size_t *size);
void gud_gem_free_object(struct drm_gem_object *gem);
int gud_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
			struct drm_mode_create_dumb *args);
int gud_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
				    u32 handle, u64 *offset);
int gud_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma);
extern const struct vm_operations_struct gud_gem_vm_ops;
```

`gud_gem_vmap()` returns `-EOPNOTSUPP` for `base.import_attach != NULL`,
`-ENOMEM` when page pinning or `vmap()` fails, and otherwise returns the cached
mapping and page-rounded `base.size`. The free callback owns all cleanup.

### Task 1: Define the Linux 4.9 GEM Boundary

**Files:**
- Create: `backport-4.9/gud_compat_4_9.h`
- Modify: `backport-4.9/gud_internal.h`
- Create: `backport-4.9/tests/test-gem-contract.sh`
- Modify: `backport-4.9/tests/test-usb-probe-contract.sh`

**Consumes:** Ticket 2's `struct gud_device` in `gud_internal.h`; target headers `include/drm/drm_gem.h` and `include/linux/mm.h`.

**Produces:** The public GUD GEM object and exact Ticket 4/5 function declarations, without changing USB probe behavior.

- [ ] **Step 1: Write the failing shared-header contract checks**

Add these checks to `backport-4.9/tests/test-gem-contract.sh`:

```bash
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

printf 'gem-contract tests: %s failures\n' "$failures"
exit "$failures"
```

Update the shared-header forbidden loop in `test-usb-probe-contract.sh` to remove only `drm_`, because `gud_internal.h` now legitimately declares DRM GEM types. Keep its existing `gud_drv.c` `drm_` prohibition unchanged.

- [ ] **Step 2: Run the contract test to verify it fails**

Run: `bash backport-4.9/tests/test-gem-contract.sh`

Expected: failure reporting missing `gud_compat_4_9.h` and GUD GEM declarations.

- [ ] **Step 3: Add the compatibility boundary and public declarations**

Create `backport-4.9/gud_compat_4_9.h`:

```c
#ifndef __GUD_COMPAT_4_9_H__
#define __GUD_COMPAT_4_9_H__

#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#endif
```

Add the compatibility include and these declarations after `struct gud_device` in `gud_internal.h`:

```c
#include "gud_compat_4_9.h"

struct gud_gem_object {
	struct drm_gem_object base;
	struct page **pages;
	void *vaddr;
};

#define to_gud_gem(gem) container_of(gem, struct gud_gem_object, base)

struct gud_gem_object *gud_gem_create(struct drm_device *dev, size_t size);
int gud_gem_get_pages(struct gud_gem_object *obj);
int gud_gem_vmap(struct gud_gem_object *obj, void **vaddr, size_t *size);
void gud_gem_free_object(struct drm_gem_object *gem);
int gud_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
			struct drm_mode_create_dumb *args);
int gud_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
				    u32 handle, u64 *offset);
int gud_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma);
extern const struct vm_operations_struct gud_gem_vm_ops;
```

Do not declare PRIME callbacks or a per-transfer unmap helper.

- [ ] **Step 4: Run the shared-header and existing USB contract tests**

Run: `bash backport-4.9/tests/test-gem-contract.sh && bash backport-4.9/tests/test-usb-probe-contract.sh`

Expected: both scripts exit `0`; the USB probe contract still finds no `drm_` text in `gud_drv.c`.

- [ ] **Step 5: Commit the interface boundary**

```bash
git add backport-4.9/gud_compat_4_9.h backport-4.9/gud_internal.h \
  backport-4.9/tests/test-gem-contract.sh \
  backport-4.9/tests/test-usb-probe-contract.sh
git commit -m "feat: declare GUD GEM interface"
```

### Task 2: Implement Local Object, Page, And CPU Mapping Lifetime

**Files:**
- Create: `backport-4.9/gud_gem_4_9.c`
- Modify: `backport-4.9/Kbuild`
- Modify: `backport-4.9/tests/test-gem-contract.sh`

**Consumes:** `struct gud_gem_object`, `to_gud_gem()`, and allocation declarations from Task 1; exported target APIs `drm_gem_object_init`, `drm_gem_get_pages`, `drm_gem_put_pages`, `drm_gem_object_release`, `vmap`, and `vunmap`.

**Produces:** Local object allocation, lazy page pinning, one cached kernel mapping, and reverse-order free callback. Task 3 relies on `gud_gem_get_pages()` and `gud_gem_free_object()`.

- [ ] **Step 1: Extend the contract test with object-lifetime requirements**

Append these requirements to `test-gem-contract.sh` before its summary:

```bash
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
```

- [ ] **Step 2: Run the contract test to verify it fails**

Run: `bash backport-4.9/tests/test-gem-contract.sh`

Expected: failure reporting missing `gud_gem_4_9.c` lifetime operations.

- [ ] **Step 3: Implement the local object and CPU mapping helpers**

Create `gud_gem_4_9.c` with these functions before the mmap callbacks:

```c
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "gud_internal.h"

struct gud_gem_object *gud_gem_create(struct drm_device *dev, size_t size)
{
	struct gud_gem_object *obj;
	int ret;

	if (!size || size > SIZE_MAX - (PAGE_SIZE - 1))
		return ERR_PTR(-EINVAL);

	size = ALIGN(size, PAGE_SIZE);
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	ret = drm_gem_object_init(dev, &obj->base, size);
	if (ret) {
		kfree(obj);
		return ERR_PTR(ret);
	}

	return obj;
}

int gud_gem_get_pages(struct gud_gem_object *obj)
{
	if (obj->base.import_attach)
		return -EOPNOTSUPP;
	if (obj->pages)
		return 0;

	obj->pages = drm_gem_get_pages(&obj->base);
	if (IS_ERR(obj->pages)) {
		int ret = PTR_ERR(obj->pages);

		obj->pages = NULL;
		return ret;
	}

	return 0;
}

int gud_gem_vmap(struct gud_gem_object *obj, void **vaddr, size_t *size)
{
	int ret;

	if (obj->base.import_attach)
		return -EOPNOTSUPP;
	ret = gud_gem_get_pages(obj);
	if (ret)
		return ret;
	if (!obj->vaddr) {
		obj->vaddr = vmap(obj->pages, obj->base.size >> PAGE_SHIFT, 0,
				  PAGE_KERNEL);
		if (!obj->vaddr)
			return -ENOMEM;
	}

	*vaddr = obj->vaddr;
	*size = obj->base.size;
	return 0;
}

void gud_gem_free_object(struct drm_gem_object *gem)
{
	struct gud_gem_object *obj = to_gud_gem(gem);

	if (obj->vaddr)
		vunmap(obj->vaddr);
	if (obj->pages)
		drm_gem_put_pages(&obj->base, obj->pages, false, false);
	drm_gem_object_release(gem);
	kfree(obj);
}
```

Update `Kbuild` to link the object:

```make
gud-y := gud_drv.o gud_gem_4_9.o
```

Do not call `drm_gem_free_mmap_offset()` separately: the captured Linux 4.9
`drm_gem_object_release()` already performs that cleanup.

- [ ] **Step 4: Run source contracts and build the exact ABI module**

Run: `bash backport-4.9/tests/test-gem-contract.sh && make -C backport-4.9 MANIFEST="$PWD/backport-4.9/env/target-manifest.env" modules`

Expected: contract script exits `0`; Kbuild links `gud_gem_4_9.o` and produces `backport-4.9/gud.ko` without compiler errors.

- [ ] **Step 5: Inspect the new undefined symbols**

Run:

```bash
nm -u backport-4.9/gud.ko | sort
grep -E 'drm_gem_(object_init|get_pages|put_pages|object_release)|\b(vmap|vunmap)\b' \
  backport-4.9/env/local/kernel/build/Module.symvers
grep -E 'drm_gem_(object_init|get_pages|put_pages|object_release)|\b(vmap|vunmap)\b' \
  backport-4.9/env/local/capture/kallsyms.txt
```

Expected: each new DRM/GEM symbol appears in `Module.symvers`; `vmap` and
`vunmap` appear as exported (`T`) symbols in the captured phone `kallsyms`.
No unresolved private GUD or non-exported DRM helper is introduced.

- [ ] **Step 6: Commit the backing-store implementation**

```bash
git add backport-4.9/Kbuild backport-4.9/gud_gem_4_9.c \
  backport-4.9/tests/test-gem-contract.sh
git commit -m "feat: add GUD page-backed GEM storage"
```

### Task 3: Add Dumb Buffer And Userspace Mapping Callbacks

**Files:**
- Modify: `backport-4.9/gud_gem_4_9.c`
- Modify: `backport-4.9/tests/test-gem-contract.sh`

**Consumes:** Task 2 allocation, page, map, and free helpers; Linux 4.9 callbacks defined in `struct drm_driver`: `dumb_create`, `dumb_map_offset`, file `mmap`, and `gem_vm_ops`.

**Produces:** Ticket 4-ready callbacks for 32-bpp dumb buffers and page-fault-based userspace mappings. Ticket 4 will register `gud_gem_free_object` as `gem_free_object_unlocked`, plus the remaining callbacks, in its `struct drm_driver`.

- [ ] **Step 1: Add failing callback-contract checks**

Append these checks to `test-gem-contract.sh`:

```bash
for text in \
    'if (args->bpp != 32)' \
    'if (!args->width || !args->height)' \
    'if (args->width > U32_MAX / 4)' \
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
```

- [ ] **Step 2: Run the contract test to verify it fails**

Run: `bash backport-4.9/tests/test-gem-contract.sh`

Expected: failure listing the missing dumb-buffer, mmap, fault, and VM-ops patterns.

- [ ] **Step 3: Implement dumb-create and mmap-offset callbacks**

Append these functions to `gud_gem_4_9.c`:

```c
int gud_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
			struct drm_mode_create_dumb *args)
{
	struct gud_gem_object *obj;
	size_t pitch, size;
	int ret;

	if (args->bpp != 32)
		return -EINVAL;
	if (!args->width || !args->height)
		return -EINVAL;
	if (args->width > U32_MAX / 4)
		return -EINVAL;

	pitch = ALIGN((size_t)args->width * 4, 4);
	if (args->height > SIZE_MAX / pitch)
		return -EINVAL;
	size = pitch * args->height;

	obj = gud_gem_create(dev, size);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	ret = drm_gem_handle_create(file, &obj->base, &args->handle);
	if (ret) {
		gud_gem_free_object(&obj->base);
		return ret;
	}

	drm_gem_object_unreference_unlocked(&obj->base);
	args->pitch = pitch;
	args->size = obj->base.size;
	return 0;
}

int gud_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
				    u32 handle, u64 *offset)
{
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;
	if (obj->import_attach) {
		ret = -EOPNOTSUPP;
		goto out_unref;
	}

	ret = gud_gem_get_pages(to_gud_gem(obj));
	if (ret)
		goto out_unref;
	ret = drm_gem_create_mmap_offset(obj);
	if (!ret)
		*offset = drm_vma_node_offset_addr(&obj->vma_node);

out_unref:
	drm_gem_object_unreference_unlocked(obj);
	return ret;
}
```

- [ ] **Step 4: Implement the file mmap, fault, and VM operations**

Append these definitions after the preceding callbacks:

```c
static int gud_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct gud_gem_object *obj = to_gud_gem(vma->vm_private_data);
	unsigned long page_offset;
	struct page *page;
	int ret;

	page_offset = ((unsigned long)vmf->virtual_address - vma->vm_start) >>
		PAGE_SHIFT;
	if (!obj->pages || page_offset >= obj->base.size >> PAGE_SHIFT)
		return VM_FAULT_SIGBUS;

	page = obj->pages[page_offset];
	ret = vm_insert_page(vma, (unsigned long)vmf->virtual_address, page);
	switch (ret) {
	case 0:
	case -EAGAIN:
	case -ERESTARTSYS:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}

const struct vm_operations_struct gud_gem_vm_ops = {
	.fault = gud_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

int gud_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;
	return 0;
}
```

`gud_drm_gem_mmap()` does not call `gud_gem_get_pages()` itself: dumb-map-offset
ensures pages before handing userspace the offset, and a missing page array in
the fault handler is a safe `VM_FAULT_SIGBUS` rather than an allocation during
a fault.

- [ ] **Step 5: Run contract tests and the exact-ABI build**

Run:

```bash
bash backport-4.9/tests/test-gem-contract.sh
bash backport-4.9/tests/test-usb-probe-contract.sh
make -C backport-4.9 MANIFEST="$PWD/backport-4.9/env/target-manifest.env" modules
```

Expected: both contract tests exit `0`; the module builds and links all GEM
callbacks. There is no phone deployment in this task because no DRM device is
registered.

- [ ] **Step 6: Commit the Ticket 4 callback contract**

```bash
git add backport-4.9/gud_gem_4_9.c backport-4.9/tests/test-gem-contract.sh
git commit -m "feat: add GUD dumb buffer callbacks"
```

### Task 4: Audit Target ABI Dependencies And Document the Ticket Boundary

**Files:**
- Modify: `backport-4.9/README.md`
- Modify: `backport-4.9/tests/test-gem-contract.sh`

**Consumes:** The fully linked Ticket 3 module from Tasks 1-3 and the captured `kallsyms.txt`, `Module.symvers`, and target manifest.

**Produces:** Reproducible build/symbol audit instructions and a contract check that prevents accidental Ticket 3 scope expansion.

- [ ] **Step 1: Add a failing audit-contract check**

Add these assertions to `test-gem-contract.sh`:

```bash
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
```

- [ ] **Step 2: Run the contract test to verify it fails**

Run: `bash backport-4.9/tests/test-gem-contract.sh`

Expected: failure listing missing Ticket 3 build/audit guidance in `README.md`.

- [ ] **Step 3: Document the build, symbol audit, and deferred runtime acceptance**

Append this section to `backport-4.9/README.md`:

````markdown
## Ticket 3 GEM Build And Symbol Audit

Ticket 3 adds page-backed local GEM helpers but does not register a DRM device.
Build against the captured target ABI:

```bash
cd backport-4.9
make MANIFEST="$PWD/env/target-manifest.env" modules
nm -u gud.ko | sort
grep -E 'drm_gem_(object_init|get_pages|put_pages|mmap|handle_create|object_release|object_unreference_unlocked|create_mmap_offset)|\b(vmap|vunmap|vm_insert_page)\b' \
  env/local/kernel/build/Module.symvers
grep -E '\b(vmap|vunmap|vm_insert_page)\b' env/local/capture/kallsyms.txt
```

The generated module must reference only target-exported GEM and VM symbols.
The `gud_gem_4_9.o` object is expected in the module link.

Ticket 3 has no `/dev/dri/cardX`, so it has no phone-side userspace test.
Ticket 4 registers the DRM device and must create, map, write, and destroy a
1280x720 XRGB8888 dumb buffer while retaining `dmesg` evidence with no kernel
warning.
````

Keep the existing Ticket 2 USB probe instructions unchanged.

- [ ] **Step 4: Run the full local verification set**

Run:

```bash
bash backport-4.9/tests/env/test-env-scripts.sh
bash backport-4.9/tests/env/test-prepare-kernel.sh
bash backport-4.9/tests/env/test-deploy-test.sh
bash backport-4.9/tests/env/test-capture-pi-usb.sh
bash backport-4.9/tests/env/test-probe-test.sh
bash backport-4.9/tests/test-usb-probe-contract.sh
bash backport-4.9/tests/test-gem-contract.sh
make -C backport-4.9 MANIFEST="$PWD/backport-4.9/env/target-manifest.env" modules
git diff --check
```

Expected: every shell test exits `0`, the module builds against the exact
target tree, and `git diff --check` produces no output.

- [ ] **Step 5: Inspect the final module dependency set**

Run:

```bash
nm -u backport-4.9/gud.ko | sort
grep -E 'drm_gem_(object_init|get_pages|put_pages|mmap|handle_create|object_release|object_unreference_unlocked|create_mmap_offset)|\b(vmap|vunmap|vm_insert_page)\b' \
  backport-4.9/env/local/kernel/build/Module.symvers
grep -E '\b(vmap|vunmap|vm_insert_page)\b' \
  backport-4.9/env/local/capture/kallsyms.txt
```

Expected: every DRM/GEM unresolved reference is in the matching
`Module.symvers`; each core VM reference has a matching `T` and `__ksymtab_`
entry in the captured phone symbols. Record this command output in the normal
ignored `env/local/evidence/` area; do not claim runtime buffer allocation
until Ticket 4.

- [ ] **Step 6: Commit the audit documentation**

```bash
git add backport-4.9/README.md backport-4.9/tests/test-gem-contract.sh
git commit -m "docs: add GUD GEM validation workflow"
```

## Final Acceptance Checklist

- [ ] `gud.ko` links `gud_gem_4_9.o` against the exact OnePlus 6 Linux 4.9 build tree.
- [ ] Each GUD GEM and VM dependency is exported by the captured target kernel.
- [ ] Local GUD objects have lazy page-backed storage and one cached CPU `vmap()` released only during GEM destruction.
- [ ] 32-bpp dumb-create, dumb-map-offset, file mmap, VM fault, and `gem_free_object_unlocked` callbacks are declared for Ticket 4 registration.
- [ ] Imported dma-buf-backed objects are rejected with `-EOPNOTSUPP`.
- [ ] No DRM device, KMS object, USB transfer, PRIME callback, or userspace test is added in Ticket 3.
- [ ] Ticket 4 owns the first phone-side create/map/write/destroy validation.
