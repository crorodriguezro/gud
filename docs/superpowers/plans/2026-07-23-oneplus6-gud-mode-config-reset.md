# OnePlus 6 GUD Mode Config Reset Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Initialize Linux 4.9 atomic connector/CRTC/plane state before registering the GUD DRM device, allowing connector queries to reach their callbacks.

**Architecture:** `gud_pipe_init()` already creates the connector and simple pipe before `gud_drm_init()` registers the DRM device. Add one `drm_mode_config_reset()` after successful pipe initialization so the existing atomic reset callbacks create coherent initial state. Retain temporary connector markers for the immediate phone verification.

**Tech Stack:** OnePlus 6 Linux 4.9.112, Linux DRM atomic modesetting, `drm_simple_display_pipe`, exact target external-module build, existing staged runner.

## Global Constraints

- Do not modify DRM core, USB behavior, GEM behavior, framebuffer handling, mode timings, vblank, or GUD protocol behavior.
- Call `drm_mode_config_reset(gud->drm)` only after successful `drm_simple_display_pipe_init()` and before `gud_pipe_init()` returns success.
- Do not call reset on either initialization error path.
- Build against the exact target tree and run existing contracts before phone deployment.
- Run only the `connector` stage for first phone verification.
- Temporary connector markers remain only until a connector-query root cause is confirmed.

---

### Task 1: Enforce Reset Ordering

**Files:**
- Modify: `backport-4.9/gud_pipe.c`
- Modify: `backport-4.9/tests/test-drm-kms-contract.sh`

**Interfaces:**
- Consumes: initialized `gud->connector` and `gud->pipe` from the existing setup sequence.
- Produces: initialized atomic KMS object state before DRM registration.

- [ ] **Step 1: Add failing source contract**

Add this required string to the pipe requirements:

```bash
require backport-4.9/gud_pipe.c 'drm_mode_config_reset(gud->drm);'
```

Add this ordering assertion:

```bash
pipe_file="$repo_root/backport-4.9/gud_pipe.c"
pipe_init_line=$(grep -nF 'ret = drm_simple_display_pipe_init' "$pipe_file" | cut -d: -f1)
reset_line=$(grep -nF 'drm_mode_config_reset(gud->drm);' "$pipe_file" | cut -d: -f1)
return_line=$(grep -nF 'return 0;' "$pipe_file" | tail -n 1 | cut -d: -f1)
if [ -z "$pipe_init_line" ] || [ -z "$reset_line" ] || [ -z "$return_line" ] ||
   [ "$reset_line" -le "$pipe_init_line" ] || [ "$reset_line" -ge "$return_line" ]; then
    printf 'FAIL [pipe reset ordering]\n' >&2
    failures=$((failures + 1))
fi
```

- [ ] **Step 2: Verify the contract fails before implementation**

Run: `bash backport-4.9/tests/test-drm-kms-contract.sh`

Expected: nonzero exit, missing `drm_mode_config_reset(gud->drm);`.

- [ ] **Step 3: Add the one-line atomic reset**

In `gud_pipe_init()`, insert exactly this after the `drm_simple_display_pipe_init()` failure check and before `return 0`:

```c
drm_mode_config_reset(gud->drm);
```

- [ ] **Step 4: Verify all source contracts**

Run:

```bash
bash backport-4.9/tests/test-drm-kms-contract.sh
bash backport-4.9/tests/test-usb-probe-contract.sh
bash backport-4.9/tests/test-gem-contract.sh
bash backport-4.9/tests/test-gud-kms-stage-contract.sh
bash backport-4.9/tests/env/test-kms-stage-test.sh
```

Expected: every command exits zero.

- [ ] **Step 5: Commit the initialization fix**

```bash
git add backport-4.9/gud_pipe.c backport-4.9/tests/test-drm-kms-contract.sh
git commit -m "fix: initialize GUD atomic KMS state"
```

### Task 2: Build And Verify The Connector Query

**Files:**
- Evidence only: `backport-4.9/env/local/evidence/drm-kms-stage-connector-*.txt`
- Modify after evidence: `PROJECT-STATUS.md`

**Interfaces:**
- Consumes: instrumented source and the exact target kernel build tree.
- Produces: phone evidence confirming the connector query reaches GUD callbacks or identifies a later boundary.

- [ ] **Step 1: Build and audit the module**

Run:

```bash
cd backport-4.9
make MANIFEST="$PWD/env/target-manifest.env" modules
nm -u gud.ko | sort
```

Expected: build exits zero; no unexpected unresolved symbol appears.

- [ ] **Step 2: Recover the Pi USB topology**

Run on the phone:

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

Expected: Pi identity `1d50:614d`.

- [ ] **Step 3: Run only connector stage**

Run:

```bash
STAGE=connector ./env/kms-stage-test.sh
grep -qF 'stage=connector complete' \
  env/local/evidence/drm-kms-stage-connector-stdout.txt
grep -F 'GUD connector:' \
  env/local/evidence/drm-kms-stage-connector-dmesg.txt
```

Expected: connector stage completes; dmesg contains detect and get-modes marker sequence. On reset, stop and preserve the final marker evidence.

- [ ] **Step 4: Record result without overclaiming**

Update `PROJECT-STATUS.md` with the observed connector-stage outcome and marker sequence. Do not mark Ticket 4 complete unless all remaining atomic stages pass.

## Plan Self-Review

- Spec coverage: Task 1 adds exactly one reset call at the specified lifecycle point and protects its ordering. Task 2 validates target build and the connector query on hardware.
- Placeholder scan: all code, commands, file paths, and acceptance outcomes are explicit.
- Type consistency: `gud_pipe_init()` and `gud->drm` use existing project names and ownership.
