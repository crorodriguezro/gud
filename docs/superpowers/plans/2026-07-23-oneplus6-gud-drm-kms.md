# OnePlus 6 GUD Linux 4.9 DRM/KMS Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Register the USB-bound GUD device as one Linux 4.9 DRM/KMS card with a fixed XRGB8888 1280x720@60 output, then validate dumb-buffer and atomic-modeset paths on the OnePlus 6.

**Architecture:** `gud_drv.c` continues to own USB probe/disconnect and gains DRM device lifetime ownership. `gud_connector.c` exposes one USB-bound virtual connector and its fixed mode; `gud_pipe.c` owns the simple pipe, local framebuffer wrapper, and no-transfer atomic callbacks. The Ticket 3 local GEM callbacks are wired directly into a 4.9-era `drm_driver`; no GUD display-control or bulk-transfer request is added.

**Tech Stack:** Downstream OnePlus 6 Linux 4.9.112, arm64 out-of-tree Kbuild module, Linux DRM atomic modesetting, `drm_simple_display_pipe`, Ticket 3 page-backed GEM, shell contract tests, libdrm-based phone smoke utility.

## Global Constraints

- Do not modify DRM core, replace the installed kernel, or flash a boot image.
- Use only interfaces exported by the exact target Linux 4.9 build; do not add `drmm_*`, `drm_gem_shmem_*`, damage helpers, or modern unplug helpers.
- Keep the USB match fixed to the validated Pi GUD gadget `1d50:614d`.
- Expose exactly one connector, one simple display pipe, one preferred `1280x720@60` mode, and `DRM_FORMAT_XRGB8888` only.
- Local page-backed Ticket 3 GEM is the only buffer storage; do not add PRIME or dma-buf import/export support.
- The connector is connected only while USB remains bound and `gud->disconnected` is false.
- Pipe callbacks must not send GUD control requests, bulk USB transfers, or map the framebuffer.
- Register `/dev/dri/cardX` only after every mode-config, connector, and pipe object has initialized.
- On disconnect, clear interface data, mark disconnected under `gud->lock`, unregister/release DRM, then release USB and free `gud_device`.
- Hardware completion requires both dumb-buffer map/write/destroy and atomic modeset evidence on the phone; it does not prove pixels are displayed.

---

## File Structure

- `backport-4.9/gud_compat_4_9.h`: concentrated Linux 4.9 DRM/KMS and framebuffer helper includes.
- `backport-4.9/gud_internal.h`: shared GUD device, pipe, connector, framebuffer, and DRM lifecycle declarations.
- `backport-4.9/gud_connector.c`: fixed 720p60 connector detect/mode enumeration.
- `backport-4.9/gud_pipe.c`: simple-pipe initialization, local framebuffer creation, and no-transfer atomic validation.
- `backport-4.9/gud_drv.c`: DRM driver/file-operations definitions plus probe and disconnect sequencing.
- `backport-4.9/Kbuild`: module object list.
- `backport-4.9/tests/test-drm-kms-contract.sh`: host-side source boundary regression test.
- `backport-4.9/tests/test-usb-probe-contract.sh`: preserve Ticket 2 requirements while allowing Ticket 4 DRM ownership in `gud_drv.c`.
- `backport-4.9/tests/gud-kms-smoke.c`: phone-side libdrm utility for the dumb-buffer and atomic-modeset acceptance path.
- `backport-4.9/README.md`: build, symbol audit, compile, deployment, and evidence procedure.

### Task 1: Define The Ticket 4 Contract And Target ABI Gate

**Files:**
- Create: `backport-4.9/tests/test-drm-kms-contract.sh`
- Modify: `backport-4.9/tests/test-usb-probe-contract.sh`
- Modify: `backport-4.9/gud_compat_4_9.h`
- Modify: `backport-4.9/README.md`

**Interfaces:**
- Consumes: Ticket 2 USB state, Ticket 3 GEM callbacks, and the exact prepared target tree at `env/local/kernel/build/`.
- Produces: a hermetic Ticket 4 source contract and an explicit list of target exports required before module runtime testing.

- [ ] **Step 1: Write the failing DRM/KMS contract test**

Create `backport-4.9/tests/test-drm-kms-contract.sh` with these assertions:

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
    '#include <drm/drm_atomic_helper.h>' \
    '#include <drm/drm_fourcc.h>' \
    '#include <drm/drm_simple_kms_helper.h>'; do
    require backport-4.9/gud_compat_4_9.h "$text"
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
    'drm_dev_alloc(&gud_drm_driver, &gud->intf->dev)' \
    '.gem_free_object_unlocked = gud_gem_free_object,' \
    '.gem_vm_ops = &gud_gem_vm_ops,' \
    '.dumb_create = gud_gem_dumb_create,' \
    '.dumb_map_offset = gud_gem_dumb_map_offset,' \
    '.mmap = gud_drm_gem_mmap,' \
    'drm_dev_register(gud->drm, 0)' \
    'drm_dev_unregister(gud->drm);' \
    'drm_dev_unref(gud->drm);'; do
    require backport-4.9/gud_drv.c "$text"
done

for text in \
    'DRM_FORMAT_XRGB8888' \
    'drm_simple_display_pipe_init' \
    'if (plane_state->fb &&' \
    'plane_state->fb->pixel_format != DRM_FORMAT_XRGB8888' \
    'drm_framebuffer_init' \
    'drm_gem_object_lookup' \
    'drm_atomic_helper_check' \
    'drm_atomic_helper_commit'; do
    require backport-4.9/gud_pipe.c "$text"
done

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

for file in backport-4.9/gud_pipe.c backport-4.9/gud_connector.c; do
    if grep -qE 'usb_(control_msg|bulk_msg)|GUD_REQ_|gud_gem_vmap' "$repo_root/$file"; then
        printf 'FAIL [forbidden transfer]: %s\n' "$file" >&2
        failures=$((failures + 1))
    fi
done

printf 'drm-kms-contract tests: %s failures\n' "$failures"
exit "$failures"
```

- [ ] **Step 2: Run the new test and verify it fails before implementation**

Run: `bash backport-4.9/tests/test-drm-kms-contract.sh`

Expected: nonzero exit with missing Ticket 4 files and symbols.

- [ ] **Step 3: Narrow the Ticket 2 contract to its lasting USB responsibilities**

In `backport-4.9/tests/test-usb-probe-contract.sh`, remove only `'drm_'` from the `gud_drv.c` forbidden-pattern list. Retain the bans on workqueues, URBs, module parameters, and `GUD_REQ_SET_BUFFER`, so Ticket 4 may add DRM lifetime code without beginning Ticket 5 transfer work.

```bash
for text in \
    'alloc_workqueue' \
    'INIT_WORK' \
    'queue_work' \
    'usb_submit_urb' \
    'module_param' \
    'GUD_REQ_SET_BUFFER'; do
```

- [ ] **Step 4: Add the required Linux 4.9 compatibility includes and ABI audit instructions**

Extend `gud_compat_4_9.h` with exactly these includes after the existing GEM include:

```c
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_simple_kms_helper.h>
```

Add a Ticket 4 README command that checks these symbols in the matching target `Module.symvers` before the first build:

```bash
grep -E 'drm_(dev_alloc|dev_register|dev_unregister|dev_unref|mode_config_init|mode_config_cleanup|connector_init|connector_cleanup|simple_display_pipe_init|atomic_helper_check|atomic_helper_commit|framebuffer_init|framebuffer_cleanup|gem_object_lookup|gem_handle_create)' \
    env/local/kernel/build/Module.symvers
```

- [ ] **Step 5: Run the existing and new host contracts**

Run:

```bash
bash backport-4.9/tests/test-usb-probe-contract.sh
bash backport-4.9/tests/test-gem-contract.sh
bash backport-4.9/tests/test-drm-kms-contract.sh
```

Expected: existing contracts pass; the new DRM/KMS contract still fails only for unimplemented Ticket 4 source.

- [ ] **Step 6: Commit the contract boundary**

```bash
git add backport-4.9/gud_compat_4_9.h backport-4.9/tests/test-drm-kms-contract.sh backport-4.9/tests/test-usb-probe-contract.sh backport-4.9/README.md
git commit -m "test: define GUD DRM KMS contract"
```

### Task 2: Add The Fixed Connector And Local Framebuffer/Pipe

**Files:**
- Create: `backport-4.9/gud_connector.c`
- Create: `backport-4.9/gud_pipe.c`
- Modify: `backport-4.9/gud_internal.h`
- Modify: `backport-4.9/Kbuild`

**Interfaces:**
- Consumes: `struct gud_device`, Ticket 3 GEM objects, and Task 1’s DRM/KMS compatibility headers.
- Produces: `gud_connector_init(struct gud_device *gud)` and `gud_pipe_init(struct gud_device *gud)`, both returning zero on successful initialization and negative errno otherwise.

- [ ] **Step 1: Extend the shared state before implementation**

Add this state and declarations to `gud_internal.h`:

```c
struct gud_framebuffer {
    struct drm_framebuffer base;
    struct drm_gem_object *obj;
};

struct gud_device {
    /* Existing USB fields remain unchanged. */
    struct drm_device *drm;
    struct drm_simple_display_pipe pipe;
    struct drm_connector connector;
};

#define to_gud_framebuffer(fb) \
    container_of(fb, struct gud_framebuffer, base)

int gud_connector_init(struct gud_device *gud);
int gud_pipe_init(struct gud_device *gud);
```

Place the DRM fields after the existing mutex so the established Ticket 2 fields retain their order. Include no workqueue, USB-transfer, EDID, or additional connector state.

- [ ] **Step 2: Implement the fixed connector**

Create `gud_connector.c` with a connector helper whose detect function returns `connector_status_connected` only when `!gud->disconnected`. Its mode function allocates exactly one mode with these standard 720p60 values:

```c
mode->clock = 74250;
mode->hdisplay = 1280;
mode->hsync_start = 1390;
mode->hsync_end = 1430;
mode->htotal = 1650;
mode->vdisplay = 720;
mode->vsync_start = 725;
mode->vsync_end = 730;
mode->vtotal = 750;
mode->flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
drm_mode_set_name(mode);
drm_mode_probed_add(connector, mode);
```

Use `DRM_MODE_CONNECTOR_VIRTUAL`, `drm_atomic_helper_connector_dpms`, `drm_helper_probe_single_connector_modes`, `drm_atomic_helper_connector_reset`, `drm_atomic_helper_connector_duplicate_state`, and `drm_atomic_helper_connector_destroy_state`. Its destroy hook calls `drm_connector_cleanup()` only; `drm_mode_config_cleanup()` owns object teardown. Set `connector.polled = 0` and do not call descriptor, EDID, or USB APIs.

- [ ] **Step 3: Implement local framebuffer ownership and mode-config functions**

In `gud_pipe.c`, implement `gud_fb_create()` for `mode_config.funcs.fb_create`. It must reject any format other than `DRM_FORMAT_XRGB8888`, any modifier other than linear, more than one plane, zero dimensions, and a missing handle. Look up `mode_cmd->handles[0]`, allocate `struct gud_framebuffer`, fill metadata with `drm_helper_mode_fill_fb_struct()`, and initialize with framebuffer funcs that:

```c
static void gud_fb_destroy(struct drm_framebuffer *fb)
{
    struct gud_framebuffer *gfb = to_gud_framebuffer(fb);

    drm_framebuffer_cleanup(fb);
    drm_gem_object_unreference_unlocked(gfb->obj);
    kfree(gfb);
}

static int gud_fb_create_handle(struct drm_framebuffer *fb,
                                struct drm_file *file, unsigned int *handle)
{
    return drm_gem_handle_create(file, to_gud_framebuffer(fb)->obj, handle);
}
```

On every error after lookup, drop the GEM reference with `drm_gem_object_unreference_unlocked(obj)`; on every error after framebuffer allocation, also free the wrapper. Do not map the object.

- [ ] **Step 4: Implement the XRGB8888 no-transfer simple pipe**

In the same file, use a one-element format array and a pipe check callback:

```c
static const u32 gud_formats[] = {
    DRM_FORMAT_XRGB8888,
};

static int gud_pipe_check(struct drm_simple_display_pipe *pipe,
                          struct drm_plane_state *plane_state,
                          struct drm_crtc_state *crtc_state)
{
    struct gud_device *gud = container_of(pipe, struct gud_device, pipe);

    if (gud->disconnected)
        return -ENODEV;
    if (plane_state->fb &&
        plane_state->fb->pixel_format != DRM_FORMAT_XRGB8888)
        return -EINVAL;
    return 0;
}
```

Provide empty enable, disable, and update callbacks. Set mode-config dimensions to `1280` for both min/max width and `720` for both min/max height, assign `fb_create`, `drm_atomic_helper_check`, and `drm_atomic_helper_commit`, call `drm_mode_config_init()`, then call `gud_connector_init()` followed by `drm_simple_display_pipe_init()` with `&gud->connector`. On pipe initialization failure, call `drm_mode_config_cleanup()` and return the error. Add `gud_pipe.c` and `gud_connector.c` to `gud-y` in Kbuild.

- [ ] **Step 5: Run contract tests and build before wiring registration**

Run:

```bash
bash backport-4.9/tests/test-gem-contract.sh
bash backport-4.9/tests/test-usb-probe-contract.sh
bash backport-4.9/tests/test-drm-kms-contract.sh
make -C backport-4.9 MANIFEST="$PWD/backport-4.9/env/target-manifest.env" modules
```

Expected: the DRM/KMS contract fails only for the still-missing Task 3 driver lifecycle markers; the other contracts pass and the module builds.

- [ ] **Step 6: Commit pipe and connector implementation**

```bash
git add backport-4.9/gud_connector.c backport-4.9/gud_pipe.c backport-4.9/gud_internal.h backport-4.9/Kbuild
git commit -m "feat: add fixed GUD DRM pipe and connector"
```

### Task 3: Register And Tear Down The DRM Device From USB Lifetime

**Files:**
- Modify: `backport-4.9/gud_drv.c`
- Modify: `backport-4.9/gud_internal.h`

**Interfaces:**
- Consumes: `gud_connector_init()`, `gud_pipe_init()`, Ticket 3 GEM callbacks, and the existing `gud_probe()`/`gud_disconnect()` USB lifecycle.
- Produces: `gud_drm_init(struct gud_device *gud)` and `gud_drm_fini(struct gud_device *gud)`, called only from USB probe/disconnect.

- [ ] **Step 1: Define the failing lifecycle expectations**

Extend `test-drm-kms-contract.sh` with these ordered text requirements in `gud_drv.c`:

```bash
require backport-4.9/gud_drv.c 'ret = gud_drm_init(gud);'
require backport-4.9/gud_drv.c 'gud_drm_fini(gud);'
require backport-4.9/gud_drv.c 'gud->disconnected = true;'
```

Run: `bash backport-4.9/tests/test-drm-kms-contract.sh`

Expected: failure because neither lifecycle function exists yet.

- [ ] **Step 2: Define the 4.9 DRM driver and file operations**

In `gud_drv.c`, add standard DRM file operations with `gud_drm_gem_mmap` as the mmap handler:

```c
static const struct file_operations gud_drm_fops = {
    .owner = THIS_MODULE,
    .open = drm_open,
    .mmap = gud_drm_gem_mmap,
    .poll = drm_poll,
    .read = drm_read,
    .unlocked_ioctl = drm_ioctl,
    .release = drm_release,
#ifdef CONFIG_COMPAT
    .compat_ioctl = drm_compat_ioctl,
#endif
    .llseek = noop_llseek,
};

static struct drm_driver gud_drm_driver = {
    .driver_features = DRIVER_MODESET | DRIVER_GEM,
    .gem_free_object_unlocked = gud_gem_free_object,
    .gem_vm_ops = &gud_gem_vm_ops,
    .dumb_create = gud_gem_dumb_create,
    .dumb_map_offset = gud_gem_dumb_map_offset,
    .dumb_destroy = drm_gem_dumb_destroy,
    .fops = &gud_drm_fops,
    .name = "gud",
    .desc = "OnePlus 6 GUD DRM backport",
    .date = "20260723",
    .major = 1,
    .minor = 0,
};
```

Do not set any `prime_*` callback and do not use `DRIVER_PRIME`.

- [ ] **Step 3: Implement initialization and exact reverse-order cleanup**

Add these functions to `gud_drv.c` and declarations to `gud_internal.h`:

```c
int gud_drm_init(struct gud_device *gud)
{
    int ret;

    gud->drm = drm_dev_alloc(&gud_drm_driver, &gud->intf->dev);
    if (IS_ERR(gud->drm)) {
        ret = PTR_ERR(gud->drm);
        gud->drm = NULL;
        return ret;
    }
    gud->drm->dev_private = gud;

    ret = gud_pipe_init(gud);
    if (ret)
        goto err_unref;

    ret = drm_dev_register(gud->drm, 0);
    if (ret)
        goto err_mode_config;

    return 0;

err_mode_config:
    drm_mode_config_cleanup(gud->drm);
err_unref:
    drm_dev_unref(gud->drm);
    gud->drm = NULL;
    return ret;
}

void gud_drm_fini(struct gud_device *gud)
{
    if (!gud->drm)
        return;

    drm_dev_unregister(gud->drm);
    drm_mode_config_cleanup(gud->drm);
    drm_dev_unref(gud->drm);
    gud->drm = NULL;
}
```

The implementation must call `gud_drm_init(gud)` after `gud_get_display_descriptor(gud)` succeeds and before `usb_set_intfdata(intf, gud)`. On the new probe failure path, use the existing `err_put_usb` cleanup. In disconnect, retain the required order: clear interface data, mark disconnected while holding `gud->lock`, call `gud_drm_fini(gud)`, then release USB and free memory.

- [ ] **Step 4: Compile and audit the module dependencies**

Run:

```bash
make -C backport-4.9 MANIFEST="$PWD/backport-4.9/env/target-manifest.env" modules
nm -u backport-4.9/gud.ko | sort
grep -E 'drm_(dev_alloc|dev_register|dev_unregister|dev_unref|mode_config_init|mode_config_cleanup|connector_init|connector_cleanup|simple_display_pipe_init|atomic_helper_check|atomic_helper_commit|framebuffer_init|framebuffer_cleanup|gem_object_lookup|gem_handle_create)' \
    backport-4.9/env/local/kernel/build/Module.symvers
```

Expected: the module builds; every new DRM unresolved symbol has a matching target export; no PRIME, dma-buf, GUD request, or USB bulk symbol is newly introduced.

- [ ] **Step 5: Run all source contracts**

Run:

```bash
bash backport-4.9/tests/test-usb-probe-contract.sh
bash backport-4.9/tests/test-gem-contract.sh
bash backport-4.9/tests/test-drm-kms-contract.sh
bash backport-4.9/tests/env/test-env-scripts.sh
bash backport-4.9/tests/env/test-prepare-kernel.sh
bash backport-4.9/tests/env/test-deploy-test.sh
bash backport-4.9/tests/env/test-capture-pi-usb.sh
bash backport-4.9/tests/env/test-probe-test.sh
```

Expected: every command exits zero.

- [ ] **Step 6: Commit USB-to-DRM lifecycle integration**

```bash
git add backport-4.9/gud_drv.c backport-4.9/gud_internal.h backport-4.9/tests/test-drm-kms-contract.sh
git commit -m "feat: register GUD DRM device"
```

### Task 4: Add The Phone-Side Dumb-Buffer And Atomic KMS Smoke Test

**Files:**
- Create: `backport-4.9/tests/gud-kms-smoke.c`
- Modify: `backport-4.9/README.md`

**Interfaces:**
- Consumes: a loaded `gud.ko`, the GUD `/dev/dri/cardX` primary node, libdrm and libdrm_mode headers on the build environment or phone.
- Produces: `gud-kms-smoke <card-path>`, exiting zero only after create/map/write/unmap, AddFB2, atomic connector/CRTC/plane setup, atomic commit, and cleanup.

- [ ] **Step 1: Write the smoke utility with explicit failure behavior**

Create `gud-kms-smoke.c`. It must use `drmModeGetResources()`, select the first connected connector with a `1280x720` mode, use the connector’s first encoder CRTC, and locate the primary plane compatible with that CRTC. It must create and map the dumb buffer through libdrm:

```c
struct drm_mode_create_dumb create = {
    .width = 1280,
    .height = 720,
    .bpp = 32,
};
struct drm_mode_map_dumb map = { 0 };

if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0)
    fail("DRM_IOCTL_MODE_CREATE_DUMB");
map.handle = create.handle;
if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0)
    fail("DRM_IOCTL_MODE_MAP_DUMB");
pixels = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
              map.offset);
if (pixels == MAP_FAILED)
    fail("mmap");
memset(pixels, 0x5a, create.size);
```

It must add an XRGB8888 framebuffer with `drmModeAddFB2()`, create an atomic request setting `CRTC_ID`, `MODE_ID`, and `ACTIVE` on the connector/CRTC plus `FB_ID`, `CRTC_ID`, `SRC_*`, and `CRTC_*` on the primary plane, then call `drmModeAtomicCommit(fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL)`. It must print the selected card, connector, CRTC, plane, dumb handle, framebuffer ID, and `atomic modeset succeeded` after the commit succeeds.

Every failure path must remove a created framebuffer, destroy a created property blob, unmap mapped storage, issue `DRM_IOCTL_MODE_DESTROY_DUMB` for a created handle, free libdrm objects, and close the file descriptor. It must not claim any display output.

- [ ] **Step 2: Compile the utility and verify expected pre-hardware behavior**

Run on an environment with libdrm development files:

```bash
cc -Wall -Wextra -Werror -O2 -o backport-4.9/tests/gud-kms-smoke \
   backport-4.9/tests/gud-kms-smoke.c -ldrm
backport-4.9/tests/gud-kms-smoke /dev/dri/card0
```

Expected: compilation succeeds. The invocation may fail if `card0` is not the GUD driver; do not treat that as a Ticket 4 failure or as runtime evidence.

- [ ] **Step 3: Document exact phone deployment and evidence capture**

Add this Ticket 4 procedure to `README.md`:

```bash
# Build and load while attached over ADB, then switch to Wi-Fi SSH before
# attaching the Pi to the phone's only USB-C port.
cd backport-4.9
make MANIFEST="$PWD/env/target-manifest.env" modules
adb push gud.ko /home/phablet/
adb push tests/gud-kms-smoke /home/phablet/
ssh phablet@<phone-ip> 'sudo insmod /home/phablet/gud.ko'
ssh phablet@<phone-ip> 'ls -l /dev/dri && modetest -c -p'
ssh phablet@<phone-ip> '/home/phablet/gud-kms-smoke /dev/dri/cardX'
ssh phablet@<phone-ip> 'dmesg' > env/local/evidence/drm-kms-dmesg.txt
```

Document that `cardX` must be selected from `modetest` output as the card named `gud`, that the evidence must show one connector/CRTC/encoder, XRGB8888, the preferred 1280x720 mode, and the utility’s `atomic modeset succeeded` line. Reject logs containing `BUG:`, `Oops`, `WARNING:`, `lockdep`, or `use-after-free`.

- [ ] **Step 4: Build, deploy, and execute the real-device acceptance test**

Run the documented procedure with the Pi connected. Preserve these files under `backport-4.9/env/local/evidence/`:

```text
drm-kms-modetest.txt
drm-kms-smoke.txt
drm-kms-dmesg.txt
```

Expected: `/dev/dri/cardX` is present for `gud`; `modetest` reports one connected virtual connector and the preferred fixed 1280x720 mode; the smoke utility prints `atomic modeset succeeded`; the full dmesg interval has no forbidden kernel-failure pattern. No test pattern or visible pixel result is required or claimed.

- [ ] **Step 5: Commit the smoke test and hardware procedure**

```bash
git add backport-4.9/tests/gud-kms-smoke.c backport-4.9/README.md
git commit -m "test: add GUD DRM KMS smoke utility"
```

### Task 5: Record Evidence-Backed Ticket Completion

**Files:**
- Modify: `PROJECT-STATUS.md`
- Modify: `BACKLOG.md`

**Interfaces:**
- Consumes: Task 4’s retained phone evidence.
- Produces: evidence-backed project state declaring Ticket 4 complete without claiming pixel output.

- [ ] **Step 1: Verify all completion artifacts before editing status**

Run:

```bash
test -s backport-4.9/env/local/evidence/drm-kms-modetest.txt
test -s backport-4.9/env/local/evidence/drm-kms-smoke.txt
test -s backport-4.9/env/local/evidence/drm-kms-dmesg.txt
! grep -E 'BUG:|Oops|WARNING:|lockdep|use-after-free' backport-4.9/env/local/evidence/drm-kms-dmesg.txt
```

Expected: all commands exit zero. If any command fails, do not update project status; retain the actual output and diagnose it before changing source.

- [ ] **Step 2: Update the evidence-backed status documents**

Mark the “Register DRM simple display pipe” checklist entries complete in `BACKLOG.md` only after the Task 4 evidence exists. Update `PROJECT-STATUS.md` to record:

```text
Ticket 4 is hardware-validated: the Pi-bound module exposes one GUD DRM card,
one virtual connector, one 1280x720@60 XRGB8888 KMS topology, and passes the
local dumb-buffer plus atomic-modeset smoke test. It sends no GUD display state
or framebuffer transfer and does not yet claim pixels.
```

Leave connector/EDID enumeration, GUD state requests, full-frame transfer, first pixels, and hot-unplug stress entries incomplete.

- [ ] **Step 3: Commit only evidence-backed status changes**

```bash
git add PROJECT-STATUS.md BACKLOG.md
git commit -m "docs: record GUD DRM KMS validation"
```

## Plan Self-Review

- Spec coverage: Task 1 enforces the Linux 4.9 API boundary and no-transfer rule. Task 2 creates the one fixed connected XRGB8888 topology and local framebuffer required for atomic userspace use. Task 3 registers and tears down DRM in correct USB lifetime order. Task 4 validates real-device dumb-buffer and atomic modeset behavior. Task 5 prevents documentation from claiming pixels or completion before evidence exists.
- Placeholder scan: no incomplete requirements or unspecified error paths remain; all shell commands, files, functions, and fixed mode values are named.
- Type consistency: `struct gud_device`, `struct gud_framebuffer`, `gud_connector_init`, `gud_pipe_init`, `gud_drm_init`, and `gud_drm_fini` have one declared owner and the same signatures across tasks.
