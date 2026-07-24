# OnePlus 6 GUD Atomic Substage Diagnostic Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Identify whether the Ticket 4 phone reset occurs during DRM resource discovery, atomic request construction, atomic validation, or real atomic state commit.

**Architecture:** Extend the existing `gud-kms-stage` utility after its validated framebuffer stage with four atomic substages. Each substage repeats the proven caps/dumb/framebuffer setup, advances through exactly one additional atomic boundary, prints completion only after that boundary succeeds, then releases every resource. The existing `kms-stage-test.sh` runner provides one-stage-per-run reset-safe evidence capture.

**Tech Stack:** OnePlus 6 Ubuntu Touch/Halium Linux 4.9.112, libdrm, DRM atomic ioctls, existing GUD local GEM/KMS driver, SSH-based evidence runner, shell contracts.

## Global Constraints

- Do not change `gud.ko`, DRM core, USB role handling, Pi firmware, GEM behavior, vblank behavior, or GUD protocol behavior.
- Run the new stages only after `caps`, `dumb`, and `fb` have recorded their successful completion lines.
- Require Pi `1d50:614d` enumeration, successful GUD probe, and `/dev/dri/card1` before every stage.
- Run one substage per phone boot with the existing 20-second runner timeout and streamed local evidence capture.
- Stop after the first reset, timeout, nonzero exit, or missing completion line.
- Do not depend on phone-local artifacts surviving reset; capture pstore and PMIC reset reason after every reset.
- No Ticket 4 completion or first-pixels claim is permitted from this diagnostic work.

---

## File Structure

- `backport-4.9/tests/gud-kms-stage.c`: add four atomic substages and shared completion gates.
- `backport-4.9/tests/test-gud-kms-stage-contract.sh`: require each new stage and its exact boundary calls.
- `backport-4.9/env/kms-stage-test.sh`: extend accepted `STAGE` values; retain existing capture behavior.
- `backport-4.9/tests/env/test-kms-stage-test.sh`: verify the runner accepts the four new values and rejects invalid input before SSH.
- `backport-4.9/README.md`: document substage order and interpretation.
- `PROJECT-STATUS.md`: record only the observed first failing substage after hardware execution.

### Task 1: Add Discovery And Request-Build Stage Gates

**Files:**
- Modify: `backport-4.9/tests/gud-kms-stage.c`
- Modify: `backport-4.9/tests/test-gud-kms-stage-contract.sh`

**Interfaces:**
- Consumes: the existing successful `fb` setup, including `fb_id`, DRM file descriptor, and 1280x720 buffer.
- Produces: `atomic-discover` and `atomic-build` stages, each invoked as `gud-kms-stage <stage> /dev/dri/card1`.

- [ ] **Step 1: Add failing contract requirements**

Append this loop to `test-gud-kms-stage-contract.sh`:

```bash
for text in \
    '"atomic-discover"' \
    '"atomic-build"' \
    'if (strcmp(stage, "atomic-discover") == 0)' \
    'if (strcmp(stage, "atomic-build") == 0)' \
    'drmModeGetResources(fd)' \
    'drmModeGetPlaneResources(fd)' \
    'get_prop_id(fd, connector_id' \
    'drmModeCreatePropertyBlob' \
    'drmModeAtomicAlloc()' \
    'drmModeAtomicAddProperty'; do
    grep -qF "$text" "$repo_root/$file" || {
        printf 'FAIL [atomic substage]: %s\n' "$text" >&2
        failures=$((failures + 1))
    }
done
```

- [ ] **Step 2: Confirm the extended contract fails**

Run: `bash backport-4.9/tests/test-gud-kms-stage-contract.sh`

Expected: nonzero exit identifying the new atomic-substage requirements.

- [ ] **Step 3: Extend the accepted stage set**

Replace the current stage validation with this exact accepted set:

```c
if (strcmp(stage, "caps") && strcmp(stage, "dumb") &&
    strcmp(stage, "fb") && strcmp(stage, "atomic-discover") &&
    strcmp(stage, "atomic-build") && strcmp(stage, "atomic-test") &&
    strcmp(stage, "atomic-commit")) {
    fprintf(stderr, "invalid stage: %s\n", stage);
    return 2;
}
```

The existing `atomic` stage is removed rather than retained, preventing an
ambiguous rerun of the unisolated reset path.

- [ ] **Step 4: Gate after resource/property discovery**

After a connector, encoder, CRTC, primary plane, and all twelve required
property IDs are found, add:

```c
if (strcmp(stage, "atomic-discover") == 0) {
    printf("stage=%s complete\n", stage);
    rc = 0;
    goto out;
}
```

This executes no mode-blob, atomic request, or atomic ioctl operation.

- [ ] **Step 5: Gate after mode blob and request construction**

Create the mode blob, allocate `request`, and add the same twelve property
values used by the existing full atomic stage. Immediately after the final
successful `drmModeAtomicAddProperty()`, add:

```c
if (strcmp(stage, "atomic-build") == 0) {
    printf("stage=%s complete\n", stage);
    rc = 0;
    goto out;
}
```

Do not call `drmModeAtomicCommit()` in this stage.

- [ ] **Step 6: Compile and verify the expanded contract**

Run:

```bash
cc -Wall -Wextra -Werror -O2 -o /tmp/opencode/gud-kms-stage \
   backport-4.9/tests/gud-kms-stage.c $(pkg-config --cflags --libs libdrm)
bash backport-4.9/tests/test-gud-kms-stage-contract.sh
```

Expected: both commands exit zero.

- [ ] **Step 7: Commit discovery/build stages**

```bash
git add backport-4.9/tests/gud-kms-stage.c backport-4.9/tests/test-gud-kms-stage-contract.sh
git commit -m "test: split GUD atomic discovery stages"
```

### Task 2: Add Atomic Test-Only And Commit Stages

**Files:**
- Modify: `backport-4.9/tests/gud-kms-stage.c`
- Modify: `backport-4.9/tests/test-gud-kms-stage-contract.sh`

**Interfaces:**
- Consumes: the fully constructed `drmModeAtomicReq` from Task 1.
- Produces: `atomic-test`, which invokes validation only, and `atomic-commit`, which applies KMS state.

- [ ] **Step 1: Add failing contract requirements for the two commit flags**

Add these required strings:

```bash
for text in \
    '"atomic-test"' \
    '"atomic-commit"' \
    'DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET' \
    'DRM_MODE_ATOMIC_ALLOW_MODESET' \
    'if (strcmp(stage, "atomic-test") == 0)' \
    'if (strcmp(stage, "atomic-commit") == 0)'; do
    grep -qF "$text" "$repo_root/$file" || {
        printf 'FAIL [atomic ioctl stage]: %s\n' "$text" >&2
        failures=$((failures + 1))
    }
done
```

- [ ] **Step 2: Run the contract and confirm it fails**

Run: `bash backport-4.9/tests/test-gud-kms-stage-contract.sh`

Expected: nonzero exit for missing atomic test-only and commit stage gates.

- [ ] **Step 3: Add the validation-only call**

After the build gate, implement exactly:

```c
if (strcmp(stage, "atomic-test") == 0) {
    if (drmModeAtomicCommit(fd, request,
                            DRM_MODE_ATOMIC_TEST_ONLY |
                            DRM_MODE_ATOMIC_ALLOW_MODESET, NULL)) {
        rc = fail("drmModeAtomicCommit(test)");
        goto out;
    }
    printf("stage=%s complete\n", stage);
    rc = 0;
    goto out;
}
```

The test-only path must not apply any display state.

- [ ] **Step 4: Add the real commit call**

Require the final stage explicitly and call:

```c
if (strcmp(stage, "atomic-commit") == 0) {
    if (drmModeAtomicCommit(fd, request,
                            DRM_MODE_ATOMIC_ALLOW_MODESET, NULL)) {
        rc = fail("drmModeAtomicCommit(commit)");
        goto out;
    }
    printf("stage=%s complete\n", stage);
    rc = 0;
}
```

For any otherwise accepted stage reaching this point, print an internal stage
error and retain nonzero `rc`.

- [ ] **Step 5: Verify warning-clean compilation and contract success**

Run:

```bash
cc -Wall -Wextra -Werror -O2 -o /tmp/opencode/gud-kms-stage \
   backport-4.9/tests/gud-kms-stage.c $(pkg-config --cflags --libs libdrm)
bash backport-4.9/tests/test-gud-kms-stage-contract.sh
```

Expected: both commands exit zero.

- [ ] **Step 6: Commit ioctl boundaries**

```bash
git add backport-4.9/tests/gud-kms-stage.c backport-4.9/tests/test-gud-kms-stage-contract.sh
git commit -m "test: split GUD atomic ioctl stages"
```

### Task 3: Extend The Reset-Safe Runner And Documentation

**Files:**
- Modify: `backport-4.9/env/kms-stage-test.sh`
- Modify: `backport-4.9/tests/env/test-kms-stage-test.sh`
- Modify: `backport-4.9/README.md`

**Interfaces:**
- Consumes: `STAGE` set to one of `caps`, `dumb`, `fb`, `atomic-discover`, `atomic-build`, `atomic-test`, or `atomic-commit`.
- Produces: the existing selected-stage evidence naming convention, including hyphenated stage names unchanged in filenames.

- [ ] **Step 1: Add a failing runner test for the new valid values**

Extend `test-kms-stage-test.sh` to invoke the runner's validation path with
`STAGE=atomic-discover` and a mock `MODULE_PATH`/`STAGE_BINARY` that exist.
The mock SSH must record invocation and return zero. Assert the runner does not
print `STAGE must be` for this new value.

- [ ] **Step 2: Run the runner test and confirm failure**

Run: `bash backport-4.9/tests/env/test-kms-stage-test.sh`

Expected: nonzero exit because the runner currently rejects `atomic-discover`.

- [ ] **Step 3: Extend the shell stage case**

Replace the runner case with:

```bash
case "$stage" in
caps|dumb|fb|atomic-discover|atomic-build|atomic-test|atomic-commit) ;;
*) printf 'STAGE must be caps, dumb, fb, atomic-discover, atomic-build, atomic-test, or atomic-commit\n' >&2; exit 2 ;;
esac
```

- [ ] **Step 4: Update the README run order**

Replace the prior stage loop with:

```bash
for STAGE in caps dumb fb atomic-discover atomic-build atomic-test atomic-commit; do
  export STAGE
  ./env/kms-stage-test.sh || break
done
```

Document the exact meaning of each atomic substage and that `atomic-test`
stopping identifies kernel validation before display-state application.

- [ ] **Step 5: Run all runner and stage tests**

Run:

```bash
bash backport-4.9/tests/test-gud-kms-stage-contract.sh
bash backport-4.9/tests/env/test-kms-stage-test.sh
bash backport-4.9/tests/test-drm-kms-contract.sh
```

Expected: every command exits zero.

- [ ] **Step 6: Commit runner support**

```bash
git add backport-4.9/env/kms-stage-test.sh backport-4.9/tests/env/test-kms-stage-test.sh backport-4.9/README.md
git commit -m "test: run GUD atomic substages"
```

### Task 4: Execute The Ordered Phone Boundary Search

**Files:**
- Evidence only: `backport-4.9/env/local/evidence/drm-kms-stage-atomic-*.txt`
- Modify after evidence: `PROJECT-STATUS.md`
- Modify after evidence: `docs/oneplus6-usb-host-gud-troubleshooting.md`

**Interfaces:**
- Consumes: Tasks 1-3, Pi enumeration, and a phone with SSH active.
- Produces: the first observed failing atomic substage, or success evidence through real commit.

- [ ] **Step 1: Recover host mode and verify Pi identity**

Run on the phone before each stage:

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

Expected: `1d50` and `614d` appear. Do not run a substage otherwise.

- [ ] **Step 2: Run atomic-discover**

Run:

```bash
STAGE=atomic-discover ./env/kms-stage-test.sh
grep -qF 'stage=atomic-discover complete' \
  env/local/evidence/drm-kms-stage-atomic-discover-stdout.txt
```

Expected: zero exit and completion line. A reset identifies resource/property discovery.

- [ ] **Step 3: Run atomic-build only after discovery succeeds**

Run:

```bash
STAGE=atomic-build ./env/kms-stage-test.sh
grep -qF 'stage=atomic-build complete' \
  env/local/evidence/drm-kms-stage-atomic-build-stdout.txt
```

Expected: zero exit and completion line. A reset identifies libdrm mode-blob or request construction.

- [ ] **Step 4: Run atomic-test only after build succeeds**

Run:

```bash
STAGE=atomic-test ./env/kms-stage-test.sh
grep -qF 'stage=atomic-test complete' \
  env/local/evidence/drm-kms-stage-atomic-test-stdout.txt
```

Expected: zero exit and completion line. A reset identifies the kernel atomic property/check path before state application.

- [ ] **Step 5: Run atomic-commit only after test-only succeeds**

Run:

```bash
STAGE=atomic-commit ./env/kms-stage-test.sh
grep -qF 'stage=atomic-commit complete' \
  env/local/evidence/drm-kms-stage-atomic-commit-stdout.txt
```

Expected: zero exit and completion line. A reset identifies state application/commit.

- [ ] **Step 6: Record the first observed failure only**

Update `PROJECT-STATUS.md` and the troubleshooting document with completed substages, the first failing one, evidence paths, and reset behavior. If every substage completes, record real atomic-modeset evidence for Ticket 4 but do not claim pixels. Do not change KMS code in this plan.

## Plan Self-Review

- Spec coverage: Task 1 isolates resource/property discovery from build. Task 2 splits validation-only atomic ioctl from real commit. Task 3 extends reset-safe execution. Task 4 captures the first failing boundary in order.
- Placeholder scan: stage names, commands, file paths, flags, and stopping criteria are explicit.
- Type consistency: `gud-kms-stage <stage> <card-path>` and `STAGE` use the exact same seven accepted values in every task.
