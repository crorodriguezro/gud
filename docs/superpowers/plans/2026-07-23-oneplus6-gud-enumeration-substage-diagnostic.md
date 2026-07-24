# OnePlus 6 GUD Enumeration Substage Diagnostic Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Identify the exact DRM enumeration operation that resets the OnePlus 6 before any atomic request exists.

**Architecture:** Replace `atomic-discover` with five smaller stages in `gud-kms-stage`. Each one repeats validated buffer setup and advances through exactly one new libdrm enumeration boundary. The existing runner preserves output and kernel logs locally, requiring the Pi identity and a GUD probe before every run.

**Tech Stack:** OnePlus 6 Ubuntu Touch/Halium Linux 4.9.112, libdrm, DRM mode resource ioctls, existing staged diagnostic runner, shell contract tests.

## Global Constraints

- Do not alter `gud.ko`, DRM core, GEM, USB role/lifetime, Pi firmware, GUD protocol, vblank, or display output behavior.
- Run a stage only with Pi `1d50:614d`, a successful GUD probe, and `/dev/dri/card1`.
- Run stages in order: `resources`, `connector`, `encoder-crtc`, `planes`, `properties`.
- Stop at the first reset, timeout, nonzero exit, or missing completion marker.
- The host-side runner retains stdout, stderr, exit, and streamed dmesg; phone-local evidence is not sufficient after reset.
- No Ticket 4 completion claim follows from an enumeration diagnostic.

---

### Task 1: Add Enumeration Stage Contract

**Files:**
- Modify: `backport-4.9/tests/test-gud-kms-stage-contract.sh`
- Modify: `backport-4.9/tests/gud-kms-stage.c`

**Interfaces:**
- Consumes: `<stage> /dev/dri/card1`.
- Produces: accepted stage names `resources`, `connector`, `encoder-crtc`, `planes`, and `properties`.

- [ ] **Step 1: Write failing stage-name and boundary requirements**

Append to the contract:

```bash
for text in \
    '"resources"' '"connector"' '"encoder-crtc"' '"planes"' '"properties"' \
    'if (strcmp(stage, "resources") == 0)' \
    'if (strcmp(stage, "connector") == 0)' \
    'if (strcmp(stage, "encoder-crtc") == 0)' \
    'if (strcmp(stage, "planes") == 0)' \
    'if (strcmp(stage, "properties") == 0)'; do
    grep -qF "$text" "$repo_root/$file" || {
        printf 'FAIL [enumeration stage]: %s\n' "$text" >&2
        failures=$((failures + 1))
    }
done
```

- [ ] **Step 2: Run the contract and confirm failure**

Run: `bash backport-4.9/tests/test-gud-kms-stage-contract.sh`

Expected: nonzero exit reporting missing enumeration-stage requirements.

- [ ] **Step 3: Extend accepted stage names and remove atomic-discover**

Replace the accepted stage list with:

```c
if (strcmp(stage, "caps") && strcmp(stage, "dumb") &&
    strcmp(stage, "fb") && strcmp(stage, "resources") &&
    strcmp(stage, "connector") && strcmp(stage, "encoder-crtc") &&
    strcmp(stage, "planes") && strcmp(stage, "properties") &&
    strcmp(stage, "atomic-build") && strcmp(stage, "atomic-test") &&
    strcmp(stage, "atomic-commit")) {
    fprintf(stderr, "invalid stage: %s\n", stage);
    return 2;
}
```

- [ ] **Step 4: Add the resources gate**

Immediately after successful `drmModeGetResources(fd)`, add:

```c
if (strcmp(stage, "resources") == 0) {
    printf("stage=%s complete\n", stage);
    rc = 0;
    goto out;
}
```

- [ ] **Step 5: Compile and pass the contract**

Run:

```bash
cc -Wall -Wextra -Werror -O2 -o /tmp/opencode/gud-kms-stage \
  backport-4.9/tests/gud-kms-stage.c $(pkg-config --cflags --libs libdrm)
bash backport-4.9/tests/test-gud-kms-stage-contract.sh
```

Expected: both commands exit zero.

### Task 2: Add Connector And Encoder/CRTC Gates

**Files:**
- Modify: `backport-4.9/tests/gud-kms-stage.c`
- Modify: `backport-4.9/tests/test-gud-kms-stage-contract.sh`

**Interfaces:**
- Consumes: resources from Task 1.
- Produces: connector selection at `connector`, then valid `crtc_id` and `crtc_index` at `encoder-crtc`.

- [ ] **Step 1: Add failing contract requirements**

Require:

```bash
for text in \
    'drmModeGetConnector(fd, resources->connectors[i])' \
    'drmModeGetEncoder(fd, connector->encoder_id)' \
    'if (strcmp(stage, "connector") == 0)' \
    'if (strcmp(stage, "encoder-crtc") == 0)'; do
    grep -qF "$text" "$repo_root/$file" || {
        printf 'FAIL [connector/crtc stage]: %s\n' "$text" >&2
        failures=$((failures + 1))
    }
done
```

- [ ] **Step 2: Verify the contract fails before adding gates**

Run: `bash backport-4.9/tests/test-gud-kms-stage-contract.sh`

Expected: nonzero exit for missing connector/CRTC gates.

- [ ] **Step 3: Gate after connector selection**

After `connector_id = connector->connector_id`, add:

```c
if (strcmp(stage, "connector") == 0) {
    printf("stage=%s complete\n", stage);
    rc = 0;
    goto out;
}
```

- [ ] **Step 4: Gate after compatible CRTC resolution**

After the existing `if (!crtc_id)` error check, add:

```c
if (strcmp(stage, "encoder-crtc") == 0) {
    printf("stage=%s complete\n", stage);
    rc = 0;
    goto out;
}
```

- [ ] **Step 5: Compile and verify contract**

Run:

```bash
cc -Wall -Wextra -Werror -O2 -o /tmp/opencode/gud-kms-stage \
  backport-4.9/tests/gud-kms-stage.c $(pkg-config --cflags --libs libdrm)
bash backport-4.9/tests/test-gud-kms-stage-contract.sh
```

Expected: both commands exit zero.

### Task 3: Add Plane And Property Gates

**Files:**
- Modify: `backport-4.9/tests/gud-kms-stage.c`
- Modify: `backport-4.9/tests/test-gud-kms-stage-contract.sh`

**Interfaces:**
- Consumes: connector, CRTC, and CRTC index.
- Produces: selected primary plane at `planes`; all twelve later-modeset property IDs at `properties`.

- [ ] **Step 1: Add failing contract requirements**

Require:

```bash
for text in \
    'drmModeGetPlaneResources(fd)' \
    'drmModeGetPlane(fd, plane_res->planes[i])' \
    'DRM_PLANE_TYPE_PRIMARY' \
    'if (strcmp(stage, "planes") == 0)' \
    'if (strcmp(stage, "properties") == 0)'; do
    grep -qF "$text" "$repo_root/$file" || {
        printf 'FAIL [plane/property stage]: %s\n' "$text" >&2
        failures=$((failures + 1))
    }
done
```

- [ ] **Step 2: Confirm the contract fails before gate additions**

Run: `bash backport-4.9/tests/test-gud-kms-stage-contract.sh`

Expected: nonzero exit for missing plane/property gates.

- [ ] **Step 3: Gate after primary plane selection**

After the existing `if (!plane)` error check, add:

```c
if (strcmp(stage, "planes") == 0) {
    printf("stage=%s complete\n", stage);
    rc = 0;
    goto out;
}
```

- [ ] **Step 4: Gate after all property ID lookups**

After the existing required-property validation check, add:

```c
if (strcmp(stage, "properties") == 0) {
    printf("stage=%s complete\n", stage);
    rc = 0;
    goto out;
}
```

- [ ] **Step 5: Compile and verify the complete contract**

Run:

```bash
cc -Wall -Wextra -Werror -O2 -o /tmp/opencode/gud-kms-stage \
  backport-4.9/tests/gud-kms-stage.c $(pkg-config --cflags --libs libdrm)
bash backport-4.9/tests/test-gud-kms-stage-contract.sh
```

Expected: both commands exit zero.

### Task 4: Extend Runner Documentation And Run Hardware Stages

**Files:**
- Modify: `backport-4.9/env/kms-stage-test.sh`
- Modify: `backport-4.9/tests/env/test-kms-stage-test.sh`
- Modify: `backport-4.9/README.md`
- Evidence only: `backport-4.9/env/local/evidence/drm-kms-stage-{resources,connector,encoder-crtc,planes,properties}-*.txt`

**Interfaces:**
- Consumes: each new `STAGE` value and existing reset-safe runner requirements.
- Produces: first observed failing enumeration boundary.

- [ ] **Step 1: Add runner stage values and failing test expectation**

Extend the valid-stage case and invalid-stage text with `resources`, `connector`, `encoder-crtc`, `planes`, and `properties`. Update the mock valid-stage invocation in `test-kms-stage-test.sh` to use `resources`. Run the test first and confirm it fails before the runner case is changed.

- [ ] **Step 2: Implement runner stage acceptance and verify tests**

Run:

```bash
bash backport-4.9/tests/env/test-kms-stage-test.sh
bash backport-4.9/tests/test-gud-kms-stage-contract.sh
bash backport-4.9/tests/test-drm-kms-contract.sh
```

Expected: every command exits zero.

- [ ] **Step 3: Document ordered enumeration execution**

Replace the atomic discovery portion of the README loop with:

```bash
for STAGE in caps dumb fb resources connector encoder-crtc planes properties \
             atomic-build atomic-test atomic-commit; do
  export STAGE
  ./env/kms-stage-test.sh || break
done
```

Document that a reset stops the loop and identifies the first incomplete stage; no later stage may run after failure.

- [ ] **Step 4: Run resources only after recovery checks**

Verify controller host mode, `1d50:614d`, GUD probe, and `card1`, then run:

```bash
STAGE=resources ./env/kms-stage-test.sh
grep -qF 'stage=resources complete' \
  env/local/evidence/drm-kms-stage-resources-stdout.txt
```

Expected: zero exit and completion line. On failure/reset, stop.

- [ ] **Step 5: Run each later stage only after its predecessor completes**

Run in this exact order:

```bash
STAGE=connector ./env/kms-stage-test.sh
STAGE=encoder-crtc ./env/kms-stage-test.sh
STAGE=planes ./env/kms-stage-test.sh
STAGE=properties ./env/kms-stage-test.sh
```

After each command, require its matching `stage=<name> complete` marker before advancing. On reset/timeout/nonzero exit, stop and collect pstore plus PMIC reset reason after recovery.

- [ ] **Step 6: Record the observed boundary only**

Update `PROJECT-STATUS.md` only with stages that have valid completion evidence and the first failed stage plus evidence paths. Do not modify `gud.ko` in this plan.

## Plan Self-Review

- Spec coverage: Tasks 1-3 split all five specified enumeration boundaries. Task 4 makes the runner accept them, documents ordering, and captures first-failure hardware evidence.
- Placeholder scan: all stage names, stage gates, commands, and expected outcomes are explicit.
- Type consistency: all uses of `STAGE` and `<stage>` name the same five enumeration values, alongside the retained earlier values.
