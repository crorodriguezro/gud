# Project Status

## Objective

Backport the host-side Generic USB Display (GUD) DRM driver to the OnePlus 6 Ubuntu Touch / Halium 9 Linux 4.9 kernel as a standalone `gud.ko` module. The phone is the USB host; the already-validated Raspberry Pi Zero 2 W is the GUD USB gadget and HDMI endpoint.

The MVP is a real DRM/KMS external output: one connector, XRGB8888, full-frame USB transfers, and a static 1280x720 test pattern. It must not require a replacement kernel image or DRM core changes unless a documented blocker proves them unavoidable.

## Current Branch

- Branch: `linux-4.9-backport`
- Current implementation commit: `b0c6856` (`build: address code review findings F1-F8`)
- Scope completed in source: Ticket 1 external-module environment and no-op module skeleton.
- Scope not completed: Ticket 1 real-phone acceptance and every GUD driver feature.

Read these first:

- `AGENTS.md`: project constraints and verification expectations.
- `LINUX-4.9-BACKPORT.md`: architecture, MVP, and compatibility strategy.
- `BACKLOG.md`: ordered milestones. All items remain unchecked until their hardware evidence exists.
- `docs/superpowers/specs/2026-07-23-oneplus6-external-module-environment-design.md`: Ticket 1 requirements.
- `docs/superpowers/plans/2026-07-23-oneplus6-external-module-environment.md`: Ticket 1 implementation plan.
- `docs/superpowers/plans/2026-07-23-oneplus6-gud-host-backport.md`: full driver ticket sequence.

## Implemented

`backport-4.9/` now contains the Ticket 1 build probe:

- `Kbuild` and `Makefile` build the out-of-tree `gud.ko` module.
- `gud_stub.c` is intentionally a no-op module. It only logs `gud: build probe loaded` and `gud: build probe unloaded`; it does not register USB, DRM, workqueues, sysfs, or device nodes.
- `env/capture-phone.sh` captures the running phone's kernel identity, exported configuration when available, module policy, and raw ADB evidence over USB.
- `env/prepare-kernel.sh` validates an ignored target manifest, checks out a pinned public kernel source, verifies the captured configuration, prepares generated headers, verifies the release, and blocks on configuration drift or forced module signing.
- `env/deploy-test.sh` captures module metadata, guards against an already-loaded module or unreadable module state, pushes with ADB, performs `insmod`/`rmmod`, and preserves dmesg evidence on errors.
- `env/target-manifest.env.example` defines the required non-secret build inputs.
- `tests/env/` contains hermetic shell tests for the scripts.

Generated Kbuild artifacts in `backport-4.9/` and all target-specific data below `backport-4.9/env/local/` are ignored. Do not commit device captures, kernel source/build trees, `Module.symvers`, module binaries, or runtime logs.

## Validation Status

Validated locally:

- The hermetic environment tests passed after `b0c6856`: `test-env-scripts.sh`, `test-prepare-kernel.sh`, and `test-deploy-test.sh`.
- The implementation was reviewed and follow-up fixes were committed in `b0c6856`.

Not yet validated on hardware:

- No phone kernel identity or configuration capture has been retained for this project.
- No public OnePlus 6 Ubuntu Touch kernel repository and immutable matching commit has been selected.
- No arm64 toolchain has been confirmed compatible with the installed phone kernel.
- No matching generated headers or trusted `Module.symvers` have been obtained.
- No `gud.ko` has been built against the exact target kernel.
- No real-device `insmod`/`rmmod` evidence exists.

Do not check any item in `BACKLOG.md` until those facts are captured and the real phone passes the load/unload test.

## Active Gate: Ticket 1 Hardware Acceptance

Use an authorized ADB USB connection to the OnePlus 6. The intended workflow is:

```bash
cd backport-4.9
bash tests/env/test-env-scripts.sh
bash tests/env/test-prepare-kernel.sh
bash tests/env/test-deploy-test.sh

./env/capture-phone.sh
cp env/target-manifest.env.example env/target-manifest.env
```

Then populate the ignored `env/target-manifest.env` from the capture output and a public-source investigation:

- `PHONE_KERNEL_RELEASE` and `PHONE_CONFIG_SHA256` from `capture-phone.sh`.
- A public OnePlus 6 Ubuntu Touch/Halium Linux 4.9 source URL and a full 40-character `KERNEL_SOURCE_COMMIT`.
- `KERNEL_SOURCE_REF`, `CROSS_COMPILE`, and the observed `TOOLCHAIN_VERSION`.
- Captured values for `CONFIG_MODVERSIONS`, `CONFIG_MODULE_SIG`, and `CONFIG_MODULE_SIG_FORCE`.

Record candidate-selection rationale and rejected candidates under `env/local/evidence/`. Do not choose a generic OnePlus 6 or LineageOS tree merely because it compiles; the exact installed-kernel release and module ABI must match.

Continue only after source selection:

```bash
./env/prepare-kernel.sh
make MANIFEST="$PWD/env/target-manifest.env" modules
./env/deploy-test.sh
```

Ticket 1 passes only if `deploy-test.sh` retains `module-metadata.txt`, `load-dmesg.txt`, and `unload-dmesg.txt`, with the expected stub messages and no module-format, signing, oops, warning, or lock-safety failure.

## Hard Stops

Stop and preserve evidence instead of working around any of these conditions:

- `CONFIG_MODULES` is disabled.
- `/proc/config.gz` is unavailable and no matching configuration can be obtained from source/build artifacts.
- `CONFIG_MODULE_SIG_FORCE=y` without an existing signing key trusted by the installed kernel image. Creating a new key is not sufficient.
- The selected source cannot reproduce the phone's kernel release and normalized configuration.
- `CONFIG_MODVERSIONS=y` without a trustworthy matching `Module.symvers`. `modules_prepare` alone does not create it; use target build artifacts or a full matching kernel-module build.
- The external module fails to build, has incompatible vermagic/symbol versions, fails `insmod`, fails `rmmod`, or emits kernel warnings.

Never replace the installed kernel, flash a boot image, or patch DRM core to bypass Ticket 1.

## Next After Ticket 1

Only after the no-op module completes a real load/unload cycle, start Ticket 2:

1. Audit GUD protocol/API dependencies against the exact Linux 4.9 source and exported symbols.
2. Add GUD protocol v1 definitions, `struct gud_device`, USB ID matching, probe, descriptor parsing, and disconnect cleanup.
3. Validate that the connected Pi Zero 2 W gadget enumerates and survives repeated attach/detach cycles.

Do not begin GEM, DRM/KMS, framebuffer transfers, compression, damage tracking, or Lomiri integration before the applicable preceding ticket is accepted.

## Collaboration Rules

- Preserve unrelated working-tree changes. At the last status update, `opencode.json` and `docs/superpowers/plans/` were untracked user/session files.
- Use `apply_patch` for manual edits and do not reset/revert changes made by others.
- For any implementation or bug fix, use the required process skills and verify with fresh command output before making success claims.
- Update `BACKLOG.md` only with evidence-backed completion. Runtime milestones require phone logs; first-pixels and unplug safety also require hardware observation.
