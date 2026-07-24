# OnePlus 6 GUD KMS Stage Diagnostic Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Identify the first userspace DRM operation that resets the OnePlus 6 during Ticket 4 validation without changing the GUD kernel driver.

**Architecture:** Add a small libdrm diagnostic program with explicit `caps`, `dumb`, `fb`, and `atomic` stages. Each stage owns and cleans up only the resources introduced at that boundary. A host-side runner streams phone `dmesg` directly to ignored evidence files and executes exactly one stage with a bounded SSH timeout.

**Tech Stack:** OnePlus 6 Ubuntu Touch/Halium Linux 4.9.112, SSH over Wi-Fi, libdrm, DRM ioctls, existing `gud-kms-smoke.c`, shell contract tests.

## Global Constraints

- Do not change `gud.ko`, DRM/KMS code, USB role handling, or Pi firmware in this diagnostic plan.
- The Pi must enumerate as `1d50:614d`, `gud.ko` must probe, and `/dev/dri/card1` must exist before a stage runs.
- Force USB host recovery with `device` then `host` writes to `/sys/bus/platform/devices/a600000.ssusb/mode` before declaring the Pi absent.
- Run one stage per phone boot; stop after the first stage that resets, hangs, or fails.
- Each stage has a 20-second timeout and streams its stdout, stderr, exit code, and `sudo dmesg -w` output to host-side ignored evidence files.
- Do not rely on phone-local evidence surviving a reset.
- After a reset, capture `/sys/fs/pstore` and PMIC boot/power-off reason before testing another stage.
- A successful diagnostic stage is not Ticket 4 completion evidence.

---

## File Structure

- `backport-4.9/tests/gud-kms-stage.c`: stage-selectable libdrm diagnostic program.
- `backport-4.9/tests/test-gud-kms-stage-contract.sh`: hermetic source contract for the stage boundaries and cleanup requirements.
- `backport-4.9/env/kms-stage-test.sh`: host-side deployment, streaming capture, timeout, and evidence wrapper.
- `backport-4.9/tests/env/test-kms-stage-test.sh`: mock-SSH shell coverage for the wrapper's required safety and evidence behavior.
- `backport-4.9/README.md`: exact build and invocation procedure for a single staged run.

### Task 1: Specify The Stage Utility Contract

**Files:**
- Create: `backport-4.9/tests/test-gud-kms-stage-contract.sh`
- Create: `backport-4.9/tests/gud-kms-stage.c`

**Interfaces:**
- Consumes: `<stage> <card-path>` arguments, where stage is one of `caps`, `dumb`, `fb`, or `atomic`.
- Produces: zero only after its named boundary completed and all acquired local resources were released; nonzero with a diagnostic on failure.

- [ ] **Step 1: Write the failing source contract**

Create `test-gud-kms-stage-contract.sh` requiring the stage names, the four progressively gated code boundaries, and reverse cleanup:

```bash
#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
file=backport-4.9/tests/gud-kms-stage.c
failures=0

for text in \
    '"caps"' '"dumb"' '"fb"' '"atomic"' \
    'DRM_CLIENT_CAP_UNIVERSAL_PLANES' \
    'DRM_CLIENT_CAP_ATOMIC' \
    'DRM_IOCTL_MODE_CREATE_DUMB' \
    'DRM_IOCTL_MODE_MAP_DUMB' \
    'DRM_IOCTL_MODE_DESTROY_DUMB' \
    'drmModeAddFB2' \
    'drmModeRmFB' \
    'drmModeAtomicCommit' \
    'munmap(pixels, create.size)' \
    'drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy)' \
    'printf("stage=%s complete\\n", stage);'; do
    grep -qF "$text" "$repo_root/$file" || {
        printf 'FAIL [required]: %s\n' "$text" >&2
        failures=$((failures + 1))
    }
done

printf 'gud-kms-stage-contract tests: %s failures\n' "$failures"
exit "$failures"
```

- [ ] **Step 2: Run the contract before the utility exists**

Run: `bash backport-4.9/tests/test-gud-kms-stage-contract.sh`

Expected: nonzero exit, reporting all missing stage utility requirements.

- [ ] **Step 3: Implement the caps and dumb stages**

Create `gud-kms-stage.c` using the existing smoke utility's includes and `fail()` helper. Require exactly two arguments after the executable name:

```c
if (argc != 3) {
    fprintf(stderr, "usage: %s <caps|dumb|fb|atomic> <card-path>\n", argv[0]);
    return 2;
}
```

For every stage, open the card read/write. For `caps` and all later stages, call:

```c
if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0)
    return fail("DRM_CLIENT_CAP_UNIVERSAL_PLANES");
if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0)
    return fail("DRM_CLIENT_CAP_ATOMIC");
```

For `caps`, print `stage=caps complete` and close the card. For `dumb` and later stages, allocate exactly `1280x720` at 32 bpp, map it, fill it with `0x5a`, unmap it, and destroy it. Print `stage=dumb complete` before returning for the `dumb` stage.

- [ ] **Step 4: Compile and run the contract**

Run:

```bash
cc -Wall -Wextra -Werror -O2 -o /tmp/opencode/gud-kms-stage \
   backport-4.9/tests/gud-kms-stage.c $(pkg-config --cflags --libs libdrm)
bash backport-4.9/tests/test-gud-kms-stage-contract.sh
```

Expected: compilation succeeds and the contract passes. Do not run against the phone until Tasks 2 and 3 provide streamed capture.

- [ ] **Step 5: Commit the first diagnostic boundary**

```bash
git add backport-4.9/tests/gud-kms-stage.c backport-4.9/tests/test-gud-kms-stage-contract.sh
git commit -m "test: add GUD KMS stage diagnostic"
```

### Task 2: Add Framebuffer And Atomic Stages

**Files:**
- Modify: `backport-4.9/tests/gud-kms-stage.c`
- Modify: `backport-4.9/tests/test-gud-kms-stage-contract.sh`

**Interfaces:**
- Consumes: Task 1 dumb object with `create.handle`, `create.pitch`, mapped storage, and the selected card FD.
- Produces: `fb` stage, which stops after AddFB2, and `atomic` stage, which performs the existing smoke utility's property setup and commit.

- [ ] **Step 1: Extend the failing contract for explicit stage gates**

Append these requirements to the contract before implementation:

```bash
for text in \
    'if (strcmp(stage, "dumb") == 0)' \
    'if (strcmp(stage, "fb") == 0)' \
    'if (strcmp(stage, "atomic") != 0)' \
    'drmModeCreatePropertyBlob' \
    'drmModeAtomicAddProperty' \
    'DRM_MODE_ATOMIC_ALLOW_MODESET'; do
    grep -qF "$text" "$repo_root/$file" || {
        printf 'FAIL [missing stage gate]: %s\n' "$text" >&2
        failures=$((failures + 1))
    }
done
```

- [ ] **Step 2: Run the contract and confirm the gate requirements fail**

Run: `bash backport-4.9/tests/test-gud-kms-stage-contract.sh`

Expected: nonzero exit identifying missing framebuffer/atomic stage gates.

- [ ] **Step 3: Add the fb stage**

For `fb` and `atomic`, call `drmModeAddFB2()` with XRGB8888 and arrays constructed from the dumb handle and pitch. For `fb`, print `stage=fb complete` immediately after AddFB2; cleanup must then call `drmModeRmFB(fd, fb_id)`, unmap, and destroy dumb storage.

- [ ] **Step 4: Add the atomic stage without changing its semantics**

Move the resource/connector/encoder/primary-plane/property lookup logic from `gud-kms-smoke.c` unchanged into the atomic-only path. It must execute only after `fb` has completed. Create a mode blob and use the same `CRTC_ID`, `MODE_ID`, `ACTIVE`, `FB_ID`, source, and CRTC property values as the smoke utility. Print `stage=atomic complete` only after `drmModeAtomicCommit()` returns zero.

- [ ] **Step 5: Verify compilation and complete contract**

Run:

```bash
cc -Wall -Wextra -Werror -O2 -o /tmp/opencode/gud-kms-stage \
   backport-4.9/tests/gud-kms-stage.c $(pkg-config --cflags --libs libdrm)
bash backport-4.9/tests/test-gud-kms-stage-contract.sh
```

Expected: both commands exit zero.

- [ ] **Step 6: Commit the staged DRM boundaries**

```bash
git add backport-4.9/tests/gud-kms-stage.c backport-4.9/tests/test-gud-kms-stage-contract.sh
git commit -m "test: stage GUD DRM smoke boundaries"
```

### Task 3: Add Reset-Safe Host Capture Runner

**Files:**
- Create: `backport-4.9/env/kms-stage-test.sh`
- Create: `backport-4.9/tests/env/test-kms-stage-test.sh`
- Modify: `backport-4.9/README.md`

**Interfaces:**
- Consumes: `STAGE`, `PHONE_HOST`, `PHONE_SUDO_PASSWORD`, `MODULE_PATH`, `STAGE_BINARY`, and a Pi already enumerated as `1d50:614d`.
- Produces: `drm-kms-stage-<stage>-{stdout,stderr,exit,dmesg}.txt` in `env/local/evidence/`.

- [ ] **Step 1: Write the failing runner contract test**

Create `test-kms-stage-test.sh` that copies `kms-stage-test.sh` into a temporary directory, invokes it with mock `ssh`, and asserts it rejects an invalid stage before any SSH call:

```bash
output=$(STAGE=invalid PHONE_HOST=phablet@example.invalid \
    PHONE_SUDO_PASSWORD=x SSH="$tmp/ssh" \
    bash "$repo_root/backport-4.9/env/kms-stage-test.sh" 2>&1) || status=$?
test "${status:-0}" -eq 2
printf '%s' "$output" | grep -qF 'STAGE must be caps, dumb, fb, or atomic'
test ! -e "$tmp/ssh-called"
```

Also assert the script contains `dmesg -w`, `timeout --signal=TERM --kill-after=2s 20s`, four local evidence filenames, and does not use `adb`.

- [ ] **Step 2: Run the runner test before implementation**

Run: `bash backport-4.9/tests/env/test-kms-stage-test.sh`

Expected: nonzero exit because the runner does not exist.

- [ ] **Step 3: Implement the runner**

Implement `kms-stage-test.sh` with these rules:

```bash
case "${STAGE:-}" in
caps|dumb|fb|atomic) ;;
*) printf 'STAGE must be caps, dumb, fb, or atomic\n' >&2; exit 2 ;;
esac
```

It must create `env/local/evidence/`, remove only prior files for the selected stage, start an SSH `sudo dmesg -w` capture in the background, run exactly one remote stage with the 20-second timeout, write the SSH exit code locally, stop the watcher, and preserve output even if the SSH connection resets. It must not clear pstore or reboot the phone.

- [ ] **Step 4: Verify the runner test passes**

Run: `bash backport-4.9/tests/env/test-kms-stage-test.sh`

Expected: zero exit with the invalid-stage and source-safety checks passing.

- [ ] **Step 5: Document the stage order and reset recovery**

Add this README procedure:

```bash
cd backport-4.9
cc -Wall -Wextra -Werror -O2 -o tests/gud-kms-stage \
   tests/gud-kms-stage.c $(pkg-config --cflags --libs libdrm)
export PHONE_HOST=phablet@192.168.1.120
export PHONE_SUDO_PASSWORD='<phone sudo password>'
export STAGE_BINARY="$PWD/tests/gud-kms-stage"
for STAGE in caps dumb fb atomic; do
    export STAGE
    ./env/kms-stage-test.sh || break
done
```

Document that a reset stops the loop; the operator must reconnect by ADB or SSH, capture pstore and PMIC reset reason, restore controller host mode, confirm `1d50:614d`, then resume only if all earlier stages have a recorded successful completion line.

- [ ] **Step 6: Commit capture tooling**

```bash
git add backport-4.9/env/kms-stage-test.sh backport-4.9/tests/env/test-kms-stage-test.sh backport-4.9/README.md
git commit -m "test: capture staged GUD KMS diagnostics"
```

### Task 4: Run The Hardware Boundary Search

**Files:**
- Evidence only: `backport-4.9/env/local/evidence/drm-kms-stage-*.txt`
- Modify after evidence: `PROJECT-STATUS.md`
- Modify after evidence: `docs/oneplus6-usb-host-gud-troubleshooting.md`

**Interfaces:**
- Consumes: the compiled utility and Task 3 runner.
- Produces: a fact-based diagnosis naming the first failing stage, or successful evidence for all four stages.

- [ ] **Step 1: Recover and verify the USB topology before each stage**

On the phone, reset the controller role and require the Pi identity:

```bash
echo "$PHONE_SUDO_PASSWORD" | sudo -S sh -c 'printf device > /sys/bus/platform/devices/a600000.ssusb/mode'
sleep 2
echo "$PHONE_SUDO_PASSWORD" | sudo -S sh -c 'printf host > /sys/bus/platform/devices/a600000.ssusb/mode'
sleep 8
for path in /sys/bus/usb/devices/*; do
    test -r "$path/idVendor" || continue
    printf '%s ' "${path##*/}"
    cat "$path/idVendor" "$path/idProduct"
done
```

Expected: an entry with `1d50` and `614d`. Do not run a stage otherwise.

- [ ] **Step 2: Run caps and inspect the evidence**

Run:

```bash
STAGE=caps ./env/kms-stage-test.sh
grep -qF 'stage=caps complete' env/local/evidence/drm-kms-stage-caps-stdout.txt
```

Expected: command exits zero, the completion line exists, and the dmesg evidence contains no kernel failure pattern.

- [ ] **Step 3: Run dumb only after caps succeeds**

Run:

```bash
STAGE=dumb ./env/kms-stage-test.sh
grep -qF 'stage=dumb complete' env/local/evidence/drm-kms-stage-dumb-stdout.txt
```

Expected: command exits zero. On reset or timeout, stop; this identifies dumb allocation/mapping as the first failing boundary.

- [ ] **Step 4: Run fb only after dumb succeeds**

Run:

```bash
STAGE=fb ./env/kms-stage-test.sh
grep -qF 'stage=fb complete' env/local/evidence/drm-kms-stage-fb-stdout.txt
```

Expected: command exits zero. On reset or timeout, stop; this identifies framebuffer creation as the first failing boundary.

- [ ] **Step 5: Run atomic only after fb succeeds**

Run:

```bash
STAGE=atomic ./env/kms-stage-test.sh
grep -qF 'stage=atomic complete' env/local/evidence/drm-kms-stage-atomic-stdout.txt
```

Expected: command exits zero. On reset or timeout, stop; this identifies atomic state setup/commit as the first failing boundary.

- [ ] **Step 6: Record only observed results**

If a stage fails, update `PROJECT-STATUS.md` and the troubleshooting document with the stage name, raw evidence paths, and the observed exit/reset behavior. Do not call it a driver defect until the streamed `dmesg` or stage isolation proves the driver boundary. If all stages pass, update Ticket 4 status with the real-device dumb-buffer and atomic-modeset result, while still making no claim of pixels.

## Plan Self-Review

- Spec coverage: Tasks 1 and 2 implement the four exact diagnostic boundaries. Task 3 captures reset-safe host evidence. Task 4 enforces ordered phone execution and evidence-backed reporting.
- Placeholder scan: every stage, file, command, success line, timeout, and recovery decision is specified.
- Type consistency: the utility always accepts `<stage> <card-path>`; the runner always receives `STAGE` and produces the same four selected-stage evidence paths.
