# OnePlus 6 GUD Linux 4.9 GEM Design

## Purpose

Ticket 3 provides the GUD driver's CPU-readable, page-backed GEM buffer layer
for the OnePlus 6 Ubuntu Touch Linux 4.9 kernel. It is a prerequisite for
future framebuffer uploads, but does not register a DRM device, expose a DRM
node, enumerate KMS objects, or transfer display data.

## Scope

Add these files to `backport-4.9/`:

- `gud_gem_4_9.c`: GUD-owned GEM allocation, mapping, mmap-offset, and
  destruction implementation.
- `gud_compat_4_9.h`: Linux 4.9 DRM and VM includes plus narrowly scoped
  compatibility declarations used by the GEM layer.

Extend `gud_internal.h` with the GUD GEM object definition and public helper
prototypes. Extend `Kbuild` so `gud_gem_4_9.o` is linked into `gud.ko`.

The implementation must use only Linux 4.9 APIs available to an external
module. Before implementation, each DRM/GEM symbol used by the layer must be
audited in the captured target `Module.symvers`; the initial candidate set is
`drm_gem_object_init`, `drm_gem_get_pages`, `drm_gem_put_pages`,
`drm_gem_create_mmap_offset`, `drm_gem_free_mmap_offset`, `drm_gem_mmap`,
`drm_gem_handle_create`, `drm_gem_object_release`, and
`drm_gem_object_unreference_unlocked`. Generic VM symbols such as `vmap()`,
`vunmap()`, and `vm_insert_page()` must be separately confirmed as exported
from the target kernel.

## Object Model

`struct gud_gem_object` embeds `struct drm_gem_object base` and owns:

- a lazily allocated `struct page **pages` array from `drm_gem_get_pages()`;
- a lazily created, cached `void *vaddr` mapping from `vmap()`;
- no scatter-gather table, dma-buf attachment, imported storage, or export
  state.

`gud_gem_create()` rounds requested storage to `PAGE_SIZE`, initializes the
embedded GEM object, and returns a GUD-owned object. It does not create a DRM
handle. `gud_gem_dumb_create()` will later call it, create a file-private GEM
handle, then drop the creator reference.

Objects are local-only. A GUD GEM helper must reject an object whose
`base.import_attach` is non-NULL with `-EOPNOTSUPP`; Ticket 3 does not provide
PRIME import or export callbacks.

## Mapping And Lifetime

`gud_gem_get_pages()` gets and caches backing pages. `gud_gem_vmap()` gets
pages as needed and maps them with `vmap(..., PAGE_KERNEL)`. It returns the
cached address and object size. The mapping remains valid until object
destruction; callers do not unmap it after an individual transfer.

The GEM free callback releases resources in reverse acquisition order:

1. `vunmap()` the cached kernel mapping, if present.
2. `drm_gem_put_pages(..., false, false)` the backing pages, if present.
3. `drm_gem_free_mmap_offset()` the VMA offset, if allocated.
4. `drm_gem_object_release()` the embedded GEM state.
5. `kfree()` the wrapper.

The allocation and mapping helpers may be called only while the owning DRM
device remains valid. Ticket 3 introduces no asynchronous work and no new USB
lifetime interactions.

## DRM Callback Contract

Ticket 3 supplies callbacks and helpers that Ticket 4 attaches to the future
`struct drm_driver`:

- `gud_gem_free_object()` for `gem_free_object`.
- `gud_gem_dumb_create()` for `dumb_create`.
- `gud_gem_dumb_map_offset()` for `dumb_map_offset`; it looks up the handle,
  rejects imported buffers, ensures backing pages and a VMA offset exist, and
  returns `drm_vma_node_offset_addr()`.
- `gud_drm_gem_mmap()` for the driver's file mmap path; it delegates access
  checks to `drm_gem_mmap()`, clears `VM_PFNMAP`, and sets `VM_MIXEDMAP` so
  the GEM fault path inserts local backing pages.
- `gud_gem_vm_ops`, built from DRM's VM open/close hooks and a GUD page-fault
  callback that inserts the requested cached page. Invalid offsets, absent
  pages, and out-of-range faults return `VM_FAULT_SIGBUS`.
- `gud_gem_vmap()` for Ticket 5 full-frame USB transfer code.

The dumb-buffer callback accepts 32-bpp storage only, which is the allocation
convention for the later XRGB8888-only KMS path. It checks width
multiplication, height multiplication, and page rounding for overflow; sets a
four-byte-aligned pitch; creates a handle; and returns the page-rounded object
size. Pixel-format validation remains with Ticket 4's framebuffer/KMS code
because the legacy dumb-create ioctl supplies bpp, not a DRM format code.

## Validation And Ticket Boundary

Ticket 3 must build against the captured exact phone kernel output directory.
The build review must confirm all new DRM/GEM references appear in the target
`Module.symvers` as exported symbols, and `nm -u gud.ko` must not introduce
undefined non-exported dependencies.

There is intentionally no userspace test in Ticket 3 because no DRM node
exists. Ticket 4 owns runtime acceptance: after it registers `/dev/dri/cardX`,
a phone-side DRM utility must create, map, write, and destroy a 1280x720
XRGB8888 dumb buffer without kernel warnings. That test exercises allocation,
CPU mapping, and destruction; Ticket 4 also attaches all GEM callbacks to the
DRM driver before registration.

## Non-Goals

- DRM/KMS device, connector, pipe, framebuffer, or mode registration.
- USB transfers, display state requests, damage tracking, compression, or
  pixel output.
- PRIME or dma-buf import/export, scanout DMA, CMA allocation, or
  scatter-gather optimization.
- DRM core modifications, modern `drm_gem_shmem_*`, or managed DRM helpers.

## Acceptance

Ticket 3 is complete when `gud.ko` links the page-backed local GEM layer
against the exact target ABI, all required symbols are exported by that kernel,
and the code provides the documented callback contract for Ticket 4. The first
userspace dumb-buffer create/map/write/destroy proof is explicitly deferred to
Ticket 4.
