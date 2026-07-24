# OnePlus 6 GUD Connector Instrumentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emit stable GUD connector boundary markers to identify the interval that resets the phone during `drmModeGetConnector()`.

**Architecture:** Add temporary `dev_info()` calls only to `gud_connector_detect()` and `gud_connector_get_modes()`. Keep control flow, locking, mode fields, and all DRM/USB behavior unchanged. Use the existing `connector` staged runner to collect the last marker before reset.

**Tech Stack:** OnePlus 6 Linux 4.9.112, Linux DRM connector helper, out-of-tree Kbuild module, shell contract tests, SSH evidence runner.

## Global Constraints

- Change only `backport-4.9/gud_connector.c` and its source contract test.
- Use `GUD connector:` at the start of every temporary marker.
- Do not add a USB request, allocation, locking change, mode change, atomic change, or vblank change.
- Build against the exact target tree before deploying.
- Run only the reset-prone `connector` staged diagnostic after the module build succeeds.
- Remove these diagnostic markers once the connector root cause and permanent correction are validated.

---

### Task 1: Add Failing Marker Contract

**Files:**
- Modify: `backport-4.9/tests/test-drm-kms-contract.sh`
- Modify: `backport-4.9/gud_connector.c`

**Interfaces:**
- Consumes: the existing connector callbacks.
- Produces: stable dmesg markers identifying detect and fixed-mode callback boundaries.

- [ ] **Step 1: Add missing-marker checks**

Append this test block:

```bash
for text in \
    'GUD connector: detect enter' \
    'GUD connector: detect status=' \
    'GUD connector: get_modes enter' \
    'GUD connector: mode created' \
    'GUD connector: mode probed' \
    'GUD connector: get_modes complete'; do
    require backport-4.9/gud_connector.c "$text"
done
```

- [ ] **Step 2: Verify the contract fails**

Run: `bash backport-4.9/tests/test-drm-kms-contract.sh`

Expected: nonzero exit with all six missing marker strings.

- [ ] **Step 3: Add detect markers without changing locking**

In `gud_connector_detect()`, add:

```c
dev_info(&gud->intf->dev, "GUD connector: detect enter\n");
```

before the mutex lock. After unlocking and before return, add:

```c
dev_info(&gud->intf->dev, "GUD connector: detect status=%d\n", status);
```

- [ ] **Step 4: Add get-modes markers at the four boundaries**

Add these exact marker locations:

```c
dev_info(&gud->intf->dev, "GUD connector: get_modes enter\n");
```

at function entry after recovering `gud` with `container_of(connector, struct gud_device, connector)`;

```c
dev_info(&gud->intf->dev, "GUD connector: mode created\n");
```

after a non-NULL `drm_mode_create()` result;

```c
dev_info(&gud->intf->dev, "GUD connector: mode probed\n");
```

after `drm_mode_probed_add()`;

```c
dev_info(&gud->intf->dev, "GUD connector: get_modes complete\n");
```

immediately before `return 1`.

- [ ] **Step 5: Run source contracts**

Run:

```bash
bash backport-4.9/tests/test-drm-kms-contract.sh
bash backport-4.9/tests/test-usb-probe-contract.sh
bash backport-4.9/tests/test-gem-contract.sh
```

Expected: all exit zero.

- [ ] **Step 6: Commit temporary instrumentation**

```bash
git add backport-4.9/gud_connector.c backport-4.9/tests/test-drm-kms-contract.sh
git commit -m "debug: trace GUD connector probing"
```

### Task 2: Build And Run The Connector Boundary Capture

**Files:**
- Evidence only: `backport-4.9/env/local/evidence/drm-kms-stage-connector-*.txt`
- Modify after evidence: `PROJECT-STATUS.md`

**Interfaces:**
- Consumes: built `gud.ko`, compiled stage utility, Pi `1d50:614d`, and active SSH.
- Produces: a final GUD connector marker before completion or reset.

- [ ] **Step 1: Build and audit the instrumented module**

Run:

```bash
cd backport-4.9
make MANIFEST="$PWD/env/target-manifest.env" modules
nm -u gud.ko | sort
```

Expected: module build exits zero; no new symbol dependency beyond existing `dev_info` support.

- [ ] **Step 2: Recover USB host mode and verify the Pi**

Run remotely before the stage:

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

Expected: a `1d50` / `614d` entry.

- [ ] **Step 3: Run connector stage with reset-safe capture**

Run:

```bash
STAGE=connector ./env/kms-stage-test.sh
```

Expected: either a completion marker or reset-safe local evidence. Do not run any later stage.

- [ ] **Step 4: Interpret and record only the final marker**

Run:

```bash
grep -F 'GUD connector:' env/local/evidence/drm-kms-stage-connector-dmesg.txt
```

Record the exact final marker and whether the stage completed/reset in `PROJECT-STATUS.md`. Do not claim a root cause until the marker interval is independently understood.

## Plan Self-Review

- Spec coverage: Task 1 adds all six required marker boundaries without behavior changes. Task 2 builds, captures, and records the final hardware boundary.
- Placeholder scan: all marker strings, locations, commands, and outcomes are explicit.
- Type consistency: `gud` derives from the embedded connector in both callback functions; no interface changes are introduced.
