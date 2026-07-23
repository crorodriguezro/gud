# OnePlus 6 External Module Environment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reproducibly build and ADB-load/unload a no-op `gud.ko` against the currently installed OnePlus 6 Ubuntu Touch Linux 4.9 kernel.

**Architecture:** Versioned shell scripts under `backport-4.9/env/` collect immutable target facts, prepare a pinned public kernel source tree, and deploy a module over ADB USB. All device dumps, source/build outputs, artifacts, and runtime evidence stay in ignored `env/local/`; only scripts, templates, and documentation are committed. A no-op module proves ABI compatibility before USB or DRM code is introduced.

**Tech Stack:** Bash, ADB over USB, Git, GNU Make/Kbuild, arm64 cross compiler, Linux 4.9 external modules, `modinfo`, `nm`, and the OnePlus 6 Ubuntu Touch shell.

## Global Constraints

- Do not modify the phone boot image, installed kernel, or device configuration.
- Do not patch Linux DRM core or any kernel source to make Ticket 1 build.
- Target the current OnePlus 6 Ubuntu Touch Linux 4.9 kernel, not a generic OnePlus 6 kernel tree.
- Use ADB over USB as the only documented deployment transport.
- Keep raw phone capture, source trees, kernel build output, modules, and dmesg evidence under ignored `backport-4.9/env/local/`.
- Reject instead of guessing when module support, signing, configuration, source ABI, toolchain ABI, load, or unload requirements are unmet.
- `gud.ko` must only log module init/exit; it must not register USB, DRM, workqueue, sysfs, or device-node state.
- Runtime success requires a real-phone `insmod` and `rmmod` evidence log. A successful compile is not completion.

---

## File Map

| File | Responsibility |
| --- | --- |
| `backport-4.9/Makefile` | Convenience entry point that requires a manifest and delegates the external-module Kbuild invocation. |
| `backport-4.9/Kbuild` | Declares the `gud` module and its single object. |
| `backport-4.9/gud_stub.c` | No-op GPL module with stable load/unload messages. |
| `backport-4.9/README.md` | Prerequisites, workflow, failure interpretation, and evidence locations. |
| `backport-4.9/env/.gitignore` | Ignores local configuration, captures, kernel trees, artifacts, and logs. |
| `backport-4.9/env/target-manifest.env.example` | Tracked non-secret target/build input template. |
| `backport-4.9/env/capture-phone.sh` | Captures target kernel and module-policy facts over ADB USB. |
| `backport-4.9/env/prepare-kernel.sh` | Clones a pinned source candidate and produces a prepared external-module build tree. |
| `backport-4.9/env/deploy-test.sh` | Performs guarded ADB deployment, module metadata capture, load/unload, and dmesg capture. |
| `backport-4.9/tests/env/test-env-scripts.sh` | Hermetic mock-ADB/script tests that do not need a phone or kernel tree. |
| `BACKLOG.md` | Marks Ticket 1 only after real-device acceptance evidence exists. |

## Task 1: Create the Isolated Module and Environment Skeleton

**Files:**
- Create: `backport-4.9/Makefile`
- Create: `backport-4.9/Kbuild`
- Create: `backport-4.9/gud_stub.c`
- Create: `backport-4.9/env/.gitignore`
- Create: `backport-4.9/env/target-manifest.env.example`
- Create: `backport-4.9/tests/env/test-env-scripts.sh`

**Consumes:** The committed specification at `docs/superpowers/specs/2026-07-23-oneplus6-external-module-environment-design.md`.

**Produces:** A buildable external-module layout, a local-artifact boundary, a target manifest contract, and a test harness for later shell scripts.

- [ ] **Step 1: Write the failing structure test**

Create `backport-4.9/tests/env/test-env-scripts.sh` with a POSIX-compatible test runner that locates the repository root, then asserts the future files exist:

```bash
#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
failures=0

assert_file() {
    if [ ! -f "$repo_root/$1" ]; then
        printf 'missing file: %s\n' "$1" >&2
        failures=$((failures + 1))
    fi
}

assert_file backport-4.9/Makefile
assert_file backport-4.9/Kbuild
assert_file backport-4.9/gud_stub.c
assert_file backport-4.9/env/.gitignore
assert_file backport-4.9/env/target-manifest.env.example

exit "$failures"
```

- [ ] **Step 2: Run the structure test to verify it fails**

Run: `bash backport-4.9/tests/env/test-env-scripts.sh`

Expected: nonzero exit with one or more `missing file:` lines because `backport-4.9/` does not exist.

- [ ] **Step 3: Create the Kbuild module and manifest boundary**

Create `backport-4.9/Kbuild`:

```make
obj-m += gud.o
gud-y := gud_stub.o
```

Create `backport-4.9/gud_stub.c`:

```c
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int __init gud_init(void)
{
	pr_info("gud: build probe loaded\n");
	return 0;
}

static void __exit gud_exit(void)
{
	pr_info("gud: build probe unloaded\n");
}

module_init(gud_init);
module_exit(gud_exit);

MODULE_DESCRIPTION("OnePlus 6 GUD backport build probe");
MODULE_LICENSE("GPL");
```

Create `backport-4.9/Makefile`:

```make
ifneq ($(KERNELRELEASE),)
include Kbuild
else
MANIFEST ?= $(CURDIR)/env/target-manifest.env

ifneq (,$(wildcard $(MANIFEST)))
include $(MANIFEST)
endif

.PHONY: modules clean

modules:
	@test -f "$(MANIFEST)" || { echo "Missing $(MANIFEST); copy env/target-manifest.env.example first"; exit 2; }
	@test -n "$(KERNEL_BUILD_DIR)" || { echo "KERNEL_BUILD_DIR is not set in $(MANIFEST)"; exit 2; }
	$(MAKE) -C "$(KERNEL_BUILD_DIR)" M="$(CURDIR)" ARCH=arm64 CROSS_COMPILE="$(CROSS_COMPILE)" modules

clean:
	@test -n "$(KERNEL_BUILD_DIR)" || exit 0
	$(MAKE) -C "$(KERNEL_BUILD_DIR)" M="$(CURDIR)" clean
endif
```

Create `backport-4.9/env/target-manifest.env.example` with exactly these fields:

```sh
PHONE_KERNEL_RELEASE=
PHONE_CONFIG_SHA256=
PHONE_ARCH=arm64
KERNEL_SOURCE_URL=
KERNEL_SOURCE_COMMIT=
KERNEL_SOURCE_REF=
CROSS_COMPILE=
TOOLCHAIN_VERSION=
CONFIG_MODVERSIONS=
CONFIG_MODULE_SIG=
CONFIG_MODULE_SIG_FORCE=
KERNEL_BUILD_DIR=
```

Create `backport-4.9/env/.gitignore`:

```gitignore
/target-manifest.env
/local/
```

Mark the test runner executable:

```bash
chmod +x backport-4.9/tests/env/test-env-scripts.sh
```

- [ ] **Step 4: Extend the test to validate the non-runtime contract**

Append checks that the stub has both required `pr_info` messages and no forbidden registration APIs, and that the manifest has every required variable:

```bash
for text in 'gud: build probe loaded' 'gud: build probe unloaded' 'MODULE_LICENSE("GPL")'; do
    grep -Fq "$text" "$repo_root/backport-4.9/gud_stub.c" || failures=$((failures + 1))
done
if grep -Eq 'usb_register|usb_register_driver|drm_dev_register|alloc_workqueue|device_create' \
    "$repo_root/backport-4.9/gud_stub.c"; then
    printf 'stub contains forbidden driver registration\n' >&2
    failures=$((failures + 1))
fi
for key in PHONE_KERNEL_RELEASE PHONE_CONFIG_SHA256 PHONE_ARCH KERNEL_SOURCE_URL \
    KERNEL_SOURCE_COMMIT KERNEL_SOURCE_REF CROSS_COMPILE TOOLCHAIN_VERSION \
    CONFIG_MODVERSIONS CONFIG_MODULE_SIG CONFIG_MODULE_SIG_FORCE KERNEL_BUILD_DIR; do
    grep -Eq "^${key}=" "$repo_root/backport-4.9/env/target-manifest.env.example" || failures=$((failures + 1))
done
```

- [ ] **Step 5: Run the skeleton tests**

Run: `bash backport-4.9/tests/env/test-env-scripts.sh`

Expected: exit 0 and no output.

- [ ] **Step 6: Commit the skeleton**

```bash
git add backport-4.9/Makefile backport-4.9/Kbuild backport-4.9/gud_stub.c backport-4.9/env/.gitignore backport-4.9/env/target-manifest.env.example backport-4.9/tests/env/test-env-scripts.sh
git commit -m "build: add GUD external module skeleton"
```

## Task 2: Implement Device-First ADB Capture

**Files:**
- Create: `backport-4.9/env/capture-phone.sh`
- Modify: `backport-4.9/tests/env/test-env-scripts.sh`

**Consumes:** `ADB_SERIAL` optional environment variable, `ADB` optional command path override, and the ignored `env/local/capture/` location from Task 1.

**Produces:** `capture-phone.sh`, an executable that selects exactly one ADB device, writes raw target facts under `local/capture/`, extracts `phone.config` when available, and prints a manifest summary.

- [ ] **Step 1: Add a mock-ADB failure test**

Add a test helper that creates a temporary `adb` executable. Its `devices` response lists two connected devices. Invoke the capture script with `ADB` pointing to the mock and assert nonzero status plus this message:

```bash
expected='multiple ADB devices found; set ADB_SERIAL'
```

The test must remove its temporary directory with `trap 'rm -rf "$tmp"' EXIT`.

- [ ] **Step 2: Run the capture test to verify it fails**

Run: `bash backport-4.9/tests/env/test-env-scripts.sh`

Expected: nonzero because `capture-phone.sh` has not been created.

- [ ] **Step 3: Implement strict ADB selection and raw capture**

Create `capture-phone.sh` with `#!/usr/bin/env bash` and `set -euo pipefail`. Resolve `script_dir`, set `local_dir="$script_dir/local"`, and use `adb_bin="${ADB:-adb}"`. Implement selection as:

```bash
mapfile -t serials < <("$adb_bin" devices | awk 'NR > 1 && $2 == "device" { print $1 }')
if [ -n "${ADB_SERIAL:-}" ]; then
    serial="$ADB_SERIAL"
    "$adb_bin" -s "$serial" get-state | grep -qx device || {
        printf 'ADB_SERIAL is not an authorized device: %s\n' "$serial" >&2
        exit 2
    }
elif [ "${#serials[@]}" -eq 1 ]; then
    serial="${serials[0]}"
elif [ "${#serials[@]}" -eq 0 ]; then
    printf 'no authorized ADB device found\n' >&2
    exit 2
else
    printf 'multiple ADB devices found; set ADB_SERIAL\n' >&2
    exit 2
fi
```

Create `local/capture/`, capture every command with `"$adb_bin" -s "$serial" shell '<command>' > "$capture_dir/<name>.txt" 2>&1 || true`, and capture `uname -a`, `uname -r`, `/proc/version`, `getprop`, `/proc/modules`, `lsmod`, `/proc/kallsyms`, `/lib/modules/$(uname -r)` listing, and device `Module.symvers` locations.

Mark the completed script executable:

```bash
chmod +x backport-4.9/env/capture-phone.sh
```

- [ ] **Step 4: Implement configuration extraction and mandatory policy checks**

Use an ADB shell existence check for `/proc/config.gz`. When present, pull it and unpack it locally:

```bash
if "$adb_bin" -s "$serial" shell 'test -r /proc/config.gz'; then
    "$adb_bin" -s "$serial" pull /proc/config.gz "$capture_dir/config.gz"
    gzip -cd "$capture_dir/config.gz" > "$capture_dir/phone.config"
    sha256sum "$capture_dir/phone.config" | awk '{print $1}' > "$capture_dir/phone.config.sha256"
else
    printf 'missing /proc/config.gz; obtain matching config from selected source artifacts\n' >&2
fi
```

When `phone.config` exists, write the exact values of `CONFIG_MODULES`, `CONFIG_MODVERSIONS`, `CONFIG_MODULE_SIG`, `CONFIG_MODULE_SIG_FORCE`, and `CONFIG_MODULE_COMPRESS` to `module-policy.txt`. Exit nonzero when `CONFIG_MODULES` is not `y`; do not create a substitute defconfig.

- [ ] **Step 5: Add a mock-ADB success test and run all capture tests**

Extend the mock to return one device, a Linux 4.9 `uname -r`, readable `config.gz`, and configuration containing `CONFIG_MODULES=y`. Assert that the script writes `local/capture/phone.config`, `phone.config.sha256`, and `module-policy.txt`; assert stdout contains `PHONE_KERNEL_RELEASE=` and `PHONE_CONFIG_SHA256=`.

Run: `bash backport-4.9/tests/env/test-env-scripts.sh`

Expected: exit 0. The test uses only its temporary mock directory, never a physical device.

- [ ] **Step 6: Commit the capture workflow**

```bash
git add backport-4.9/env/capture-phone.sh backport-4.9/tests/env/test-env-scripts.sh
git commit -m "build: capture OnePlus 6 module ABI inputs"
```

## Task 3: Implement Pinned Kernel Preparation

**Files:**
- Create: `backport-4.9/env/prepare-kernel.sh`
- Modify: `backport-4.9/tests/env/test-env-scripts.sh`

**Consumes:** A populated ignored `backport-4.9/env/target-manifest.env`, `local/capture/phone.config`, a pinned Git URL/40-character commit, and an arm64 cross compiler named by `CROSS_COMPILE`.

**Produces:** A source checkout at `local/kernel/source/`, a prepared output tree at `local/kernel/build/`, generated headers, a release check, and `local/evidence/prepare-kernel.log`.

- [ ] **Step 1: Add validation tests for an incomplete manifest**

Create a temporary manifest with an empty `KERNEL_SOURCE_COMMIT`, set `MANIFEST` to it, run the future script, and assert status 2 and output:

```text
KERNEL_SOURCE_COMMIT must be a full 40-character Git SHA
```

- [ ] **Step 2: Run the preparation validation test to verify it fails**

Run: `bash backport-4.9/tests/env/test-env-scripts.sh`

Expected: nonzero because `prepare-kernel.sh` has not been created.

- [ ] **Step 3: Implement manifest validation and deterministic checkout**

Create `prepare-kernel.sh` with Bash strict mode. It loads `MANIFEST="${MANIFEST:-$script_dir/target-manifest.env}"`, rejects a missing manifest, and validates these fields are nonempty: `PHONE_KERNEL_RELEASE`, `PHONE_CONFIG_SHA256`, `KERNEL_SOURCE_URL`, `KERNEL_SOURCE_COMMIT`, `CROSS_COMPILE`, and `TOOLCHAIN_VERSION`.

Mark the completed script executable:

```bash
chmod +x backport-4.9/env/prepare-kernel.sh
```

Reject a non-immutable commit with:

```bash
if ! [[ "$KERNEL_SOURCE_COMMIT" =~ ^[0-9a-f]{40}$ ]]; then
    printf 'KERNEL_SOURCE_COMMIT must be a full 40-character Git SHA\n' >&2
    exit 2
fi
```

Require `local/capture/phone.config`, verify its SHA-256 against `PHONE_CONFIG_SHA256`, and reject a mismatch. Clone only if `local/kernel/source/.git` is absent; otherwise use the existing checkout. Run:

```bash
git -C "$source_dir" fetch --depth=1 origin "$KERNEL_SOURCE_COMMIT"
git -C "$source_dir" checkout --detach "$KERNEL_SOURCE_COMMIT"
test "$(git -C "$source_dir" rev-parse HEAD)" = "$KERNEL_SOURCE_COMMIT"
```

- [ ] **Step 4: Implement the external-module preparation sequence**

Create `local/kernel/build/`; copy `phone.config` as its `.config`; then invoke:

```bash
make -C "$source_dir" O="$build_dir" ARCH=arm64 CROSS_COMPILE="$CROSS_COMPILE" olddefconfig
make -C "$source_dir" O="$build_dir" ARCH=arm64 CROSS_COMPILE="$CROSS_COMPILE" prepare modules_prepare
```

Read the release with `make -s -C "$source_dir" O="$build_dir" kernelrelease` and require exact equality with `PHONE_KERNEL_RELEASE`. Require `include/generated/autoconf.h`; if `CONFIG_MODVERSIONS=y`, also require `Module.symvers`. Write all commands, Git commit, compiler `"${CROSS_COMPILE}gcc" --version`, kernel release, and policy values to `local/evidence/prepare-kernel.log`.

Update the local manifest's `KERNEL_BUILD_DIR` only when the source, configuration hash, and release checks all pass.

- [ ] **Step 5: Add a hermetic make/git fixture test**

In the test runner, create mock `git`, `make`, and `${CROSS_COMPILE}gcc` commands. The mock `make` creates `include/generated/autoconf.h`, emits the supplied release on a `kernelrelease` invocation, and creates `Module.symvers` when requested. Assert a success run writes `local/evidence/prepare-kernel.log` and records the pinned SHA. Add a second run whose mocked `kernelrelease` differs from `PHONE_KERNEL_RELEASE`; assert nonzero and the exact message `prepared kernel release does not match phone release`.

- [ ] **Step 6: Run all environment-script tests**

Run: `bash backport-4.9/tests/env/test-env-scripts.sh`

Expected: exit 0.

- [ ] **Step 7: Commit the preparation workflow**

```bash
git add backport-4.9/env/prepare-kernel.sh backport-4.9/tests/env/test-env-scripts.sh
git commit -m "build: prepare pinned OnePlus 6 kernel tree"
```

## Task 4: Implement Guarded Module Deployment and Evidence Collection

**Files:**
- Create: `backport-4.9/env/deploy-test.sh`
- Modify: `backport-4.9/tests/env/test-env-scripts.sh`

**Consumes:** A populated manifest with `KERNEL_BUILD_DIR`, an externally built `backport-4.9/gud.ko`, one authorized ADB USB device, and the init/exit messages from Task 1.

**Produces:** A guarded deployment sequence and timestamped logs in `local/evidence/` for module metadata, undefined symbols, load, unload, and dmesg output.

- [ ] **Step 1: Add a mock test for an already-loaded module**

Configure the mock ADB `shell` command to return a `/proc/modules` line beginning with `gud `. Invoke the future deploy script and assert it exits 2 with:

```text
refusing to replace an already-loaded gud module
```

- [ ] **Step 2: Run the deployment test to verify it fails**

Run: `bash backport-4.9/tests/env/test-env-scripts.sh`

Expected: nonzero because `deploy-test.sh` has not been created.

- [ ] **Step 3: Implement pre-deployment validation and metadata evidence**

Create `deploy-test.sh` with Bash strict mode and the same one-device selection behavior as `capture-phone.sh`. Require `gud.ko` at `MODULE_PATH="${MODULE_PATH:-$script_dir/../gud.ko}"` and require the manifest's `KERNEL_BUILD_DIR` is a directory.

Mark the completed script executable:

```bash
chmod +x backport-4.9/env/deploy-test.sh
```

Create `local/evidence/`. Before ADB deployment, write:

```bash
{
    printf 'manifest=%s\n' "$manifest"
    printf 'module=%s\n' "$module_path"
    modinfo "$module_path"
    nm -u "$module_path"
} > "$evidence_dir/module-metadata.txt" 2>&1
```

Reject the attempt when the phone reports an existing `gud` line in `/proc/modules`; do not run `adb push` in this path.

- [ ] **Step 4: Implement load/unload sequencing and dmesg assertions**

Push the module to `/home/phablet/gud.ko`, record the pre-load dmesg sequence number when `/dev/kmsg`/`dmesg` supports it, and run exactly:

```bash
"$adb_bin" -s "$serial" shell 'sudo insmod /home/phablet/gud.ko'
"$adb_bin" -s "$serial" shell 'sudo rmmod gud'
```

Capture full dmesg after each operation in `load-dmesg.txt` and `unload-dmesg.txt`. Require `gud: build probe loaded` after load and `gud: build probe unloaded` after unload. Reject output matching `Invalid module format`, `Required key not available`, `module verification failed`, `BUG:`, `Oops`, `WARNING:`, `lockdep`, or `use-after-free`. Preserve all evidence before returning nonzero.

- [ ] **Step 5: Add mock success and signature-failure tests**

Mock successful `push`, `insmod`, `rmmod`, and dmesg output containing both expected messages. Assert script success and all three evidence files exist. Add a second run with `Required key not available` in load dmesg; assert nonzero and preservation of `load-dmesg.txt`.

- [ ] **Step 6: Run all environment-script tests**

Run: `bash backport-4.9/tests/env/test-env-scripts.sh`

Expected: exit 0.

- [ ] **Step 7: Commit deployment support**

```bash
git add backport-4.9/env/deploy-test.sh backport-4.9/tests/env/test-env-scripts.sh
git commit -m "build: add guarded GUD module deployment"
```

## Task 5: Document the Reproducible Workflow and Perform Hardware Acceptance

**Files:**
- Create: `backport-4.9/README.md`
- Modify: `BACKLOG.md`

**Consumes:** All scripts and the no-op module from Tasks 1-4; the physical phone connected through authorized ADB USB.

**Produces:** A start-to-finish operator guide, a target-specific local manifest/evidence bundle, and a documented runtime result.

- [ ] **Step 1: Document the exact operator workflow**

Write `backport-4.9/README.md` with these command blocks and their prerequisite meanings:

```bash
cd backport-4.9
./env/capture-phone.sh
cp env/target-manifest.env.example env/target-manifest.env
# Populate target-manifest.env with the capture output and the selected public source URL/40-character commit.
./env/prepare-kernel.sh
make MANIFEST="$PWD/env/target-manifest.env" modules
./env/deploy-test.sh
```

Document that `capture-phone.sh` does not select source; the operator must record source URL, ref, commit, expected release, selection rationale, and rejected candidates in the local manifest/evidence before preparation. Document all hard blockers from the specification and the location of every local artifact.

- [ ] **Step 2: Verify documentation references only implemented commands**

Add checks to `test-env-scripts.sh` that every documented `./env/<script>.sh` exists and is executable, and that README mentions both expected dmesg strings and `Invalid module format`.

Run: `bash backport-4.9/tests/env/test-env-scripts.sh`

Expected: exit 0.

- [ ] **Step 3: Run shell syntax verification**

Run:

```bash
bash -n backport-4.9/env/capture-phone.sh
bash -n backport-4.9/env/prepare-kernel.sh
bash -n backport-4.9/env/deploy-test.sh
bash backport-4.9/tests/env/test-env-scripts.sh
```

Expected: all commands exit 0.

- [ ] **Step 4: Run the real-phone capture and source-selection gate**

With the OnePlus 6 connected by USB, run:

```bash
cd backport-4.9
./env/capture-phone.sh
```

Expected: `env/local/capture/` contains the captured identity and module policy. Do not continue if `CONFIG_MODULES` is disabled, configuration cannot be established, module signing cannot be satisfied, or source matching remains unresolved.

- [ ] **Step 5: Prepare the selected source and build the real module**

Populate the ignored manifest with the selected immutable source commit and toolchain, then run:

```bash
./env/prepare-kernel.sh
make modules
modinfo gud.ko
nm -u gud.ko
```

Expected: preparation log records exact source/toolchain/config information, `modinfo` vermagic is compatible with captured `uname -r`, and the module build is warning-clean where practical. Stop on any source ABI mismatch.

- [ ] **Step 6: Perform real-device load/unload acceptance**

Run:

```bash
./env/deploy-test.sh
```

Expected: `env/local/evidence/module-metadata.txt`, `load-dmesg.txt`, and `unload-dmesg.txt` exist; the logs contain the stable load/unload messages and no forbidden error/warning pattern.

- [ ] **Step 7: Update the backlog only after acceptance succeeds**

In `BACKLOG.md`, check only these P0 target-build items once the Task 5 evidence passes:

```markdown
- [x] Capture `uname -a` from the phone.
- [x] Reproduce the matching `.config`.
- [x] Determine the compiler/toolchain used by the target build.
- [x] Generate or obtain matching `Module.symvers` and generated headers.
- [x] Build and load a trivial out-of-tree arm64 module as a sanity check.
```

Do not check “Identify the exact OnePlus 6 Ubuntu Touch kernel repository and commit” until the manifest and acceptance evidence include the selected immutable commit. Do not check any GUD USB/DRM backlog item.

- [ ] **Step 8: Commit documentation and verified backlog status**

```bash
git add backport-4.9/README.md BACKLOG.md
git commit -m "docs: document OnePlus 6 module environment"
```

If real-device acceptance is blocked, commit the README without changing backlog checkboxes and add the exact blocker plus evidence location to the local evidence bundle only.

## Plan Coverage Review

| Specification requirement | Covered by |
| --- | --- |
| Device-first ADB USB capture | Task 2 |
| Pinned source, configuration, compiler, generated headers, and `Module.symvers` | Task 3 |
| Local-only artifacts and tracked manifest template | Task 1 |
| No-op `gud.ko` module | Task 1 |
| Metadata inspection and guarded deployment | Task 4 |
| Real-phone `insmod`/`rmmod` evidence | Task 5 |
| Hard blockers and no kernel replacement | Global Constraints, Tasks 2-5 |
| Documented reproducible operation | Task 5 |
