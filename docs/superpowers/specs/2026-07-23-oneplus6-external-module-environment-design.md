# OnePlus 6 External Module Environment Design

## Goal

Create a reproducible host environment that builds a no-op arm64 `gud.ko` compatible with the currently installed OnePlus 6 Ubuntu Touch Linux 4.9 kernel, then prove that module loads and unloads through ADB over USB.

This ticket establishes the build foundation for the GUD host backport. It does not implement USB probing, DRM/KMS, framebuffer storage, or display output.

## Scope

The ticket provides:

- A device-first capture workflow over ADB USB.
- A manifest that pins the target phone ABI, selected public kernel source, and toolchain.
- A repeatable kernel preparation workflow that generates the headers and metadata needed by an external module build.
- A minimal `gud.ko` whose only behavior is init/exit logging.
- A deployment workflow that captures the result of `insmod` and `rmmod` on the real phone.

The ticket does not modify the phone boot image, the installed kernel, or device configuration.

## Directory Layout

```text
backport-4.9/
├── Makefile
├── Kbuild
├── gud_stub.c
├── README.md
└── env/
    ├── capture-phone.sh
    ├── prepare-kernel.sh
    ├── deploy-test.sh
    ├── target-manifest.env.example
    └── .gitignore
```

`backport-4.9/env/` contains scripts, the manifest template, and documentation-safe metadata only. It must not contain a cloned kernel tree, toolchain archive, generated kernel output, raw device capture, module artifact, or dmesg capture.

Local artifacts are stored under `backport-4.9/env/local/` and ignored by Git. The subdirectories are:

- `capture/` for raw ADB responses and extracted phone configuration.
- `kernel/` for the candidate kernel source and its build output.
- `evidence/` for build, module metadata, load, unload, and dmesg logs.

## Target Identity Capture

`capture-phone.sh` requires exactly one ADB device connected over USB. It must fail if no device is connected or more than one device is visible unless an explicit serial is provided through `ADB_SERIAL`.

The script captures the following before any source repository is selected:

- `uname -a` and `uname -r`.
- `/proc/version`.
- `getprop` output, including device, build, and kernel-related properties available on the installed image.
- `/proc/config.gz` when it exists, decompressing it to `local/capture/phone.config` and recording its SHA-256 hash.
- `/proc/modules` and `lsmod` when available.
- Module-related configuration values from the extracted configuration: `CONFIG_MODULES`, `CONFIG_MODVERSIONS`, `CONFIG_MODULE_SIG`, `CONFIG_MODULE_SIG_FORCE`, and compression settings.
- `/proc/kallsyms` when readable, limited in the manifest to facts required to assess module support and symbol-version availability.
- `/lib/modules/$(uname -r)/` contents and `Module.symvers` when present on the device.

The script writes raw command outputs to `local/capture/` and emits a summary suitable for copying into `target-manifest.env`.

If `/proc/config.gz` is unavailable, the script must report that a matching configuration must instead be obtained from the selected source/build artifacts. It must not substitute a generic defconfig.

## Source Discovery and Acceptance

The installed phone is the authority for ABI identity. Public internet search is used only to locate a source candidate for the OnePlus 6 (`enchilada`) Ubuntu Touch/Halium 9 kernel.

Each candidate must be recorded in the manifest with:

- Immutable repository URL and full Git commit SHA.
- Branch or tag from which the commit was selected.
- Kernel release string expected from the candidate build.
- Exact captured phone release string.
- Source-selection rationale and rejected candidates.

`prepare-kernel.sh` clones the candidate at the pinned commit into `local/kernel/source/`. It installs the captured phone configuration as the candidate output `.config`, runs the candidate kernel's configuration normalization, and stops if required configuration options are missing or changed incompatibly.

The script then uses the selected arm64 compiler to run the minimal preparation required for external modules. The prepared output must include generated headers and a usable `Module.symvers` if `CONFIG_MODVERSIONS=y` or if the selected kernel build requires it.

Candidate acceptance requires all of the following:

- The source is a Linux 4.9-based OnePlus 6 vendor tree compatible with the captured device identity.
- The prepared tree preserves the captured kernel release string used for module vermagic.
- The selected compiler identity is recorded and compatible with the target kernel's module policy.
- The external module can be built from the prepared tree without adding core patches.
- The resulting no-op module loads and unloads on the installed phone.

A source tree that compiles but fails the runtime module test is rejected. The workflow must retain the exact failure output and not continue to GUD driver work.

## Manifest and Reproducibility

`target-manifest.env.example` defines the exact non-secret fields required for a build:

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

The active `target-manifest.env` is local and ignored because it may contain host-specific absolute paths. `README.md` documents how to copy the example, populate it from capture output, and invoke every script.

Each build records these values in `local/evidence/build-metadata.txt`, together with the full external-module command, compiler version, `modinfo gud.ko`, and unresolved-symbol inspection output.

## Stub Module

The initial module is named `gud.ko` so later tickets can replace its internals without changing the deployment path.

`Kbuild` declares a single module target and compiles `gud_stub.c`. The stub module has:

- GPL-compatible module license.
- Clear module description identifying it as the OnePlus 6 GUD backport build probe.
- `module_init` that logs a stable `gud: build probe loaded` message.
- `module_exit` that logs a stable `gud: build probe unloaded` message.

It must not register a USB driver, DRM driver, workqueue, sysfs attribute, or device node.

## Build and Deployment Flow

The primary transport is ADB over USB.

1. Connect the running phone by USB and authorize ADB.
2. Run `capture-phone.sh` and preserve its raw capture.
3. Identify public source candidates and record the selected immutable commit and rejected candidates in the active manifest.
4. Run `prepare-kernel.sh` to create the prepared kernel build output.
5. Build with `make -C "$KERNEL_BUILD_DIR" M="$PWD/backport-4.9" ARCH=arm64 CROSS_COMPILE="$CROSS_COMPILE" modules`.
6. Inspect `modinfo gud.ko` and undefined symbols before deployment.
7. Run `deploy-test.sh`, which pushes the module to `/home/phablet/gud.ko`, invokes `sudo insmod`, collects dmesg, invokes `sudo rmmod gud`, and collects final dmesg.

The deploy script must refuse to replace an already-loaded `gud` module. It must fail and preserve evidence if ADB transport, privilege escalation, module loading, or module unloading fails.

## Failure Rules

The ticket is blocked, rather than worked around, under any of these conditions:

- The phone reports `CONFIG_MODULES=n`.
- Required module signature enforcement is enabled and no approved signing path for the installed kernel is available.
- The target configuration cannot be retrieved from the phone or matched source/build artifacts.
- No public source candidate can reproduce the phone kernel release and external-module ABI requirements.
- The host toolchain cannot generate an arm64 module with the required vermagic and symbol versions.
- The stub module fails `insmod`, fails `rmmod`, or emits unexpected kernel warnings.

Each blocker is recorded with the exact command, return status, and captured output. The ticket does not replace the running kernel to bypass a blocker.

## Verification and Completion

Ticket 1 is complete only when all conditions hold:

- The active manifest pins the target phone identity, source URL and commit, toolchain identity, and module policy.
- A clean host can recreate the prepared kernel build directory from the manifest and phone capture.
- `gud.ko` builds warning-clean where practical for the selected Linux 4.9 toolchain.
- `modinfo gud.ko` reports vermagic compatible with `uname -r` on the phone.
- The undefined-symbol inspection has no unresolved requirement incompatible with the target kernel exports.
- ADB USB deployment produces the expected loaded and unloaded dmesg messages.
- `insmod` and `rmmod` both succeed without `Invalid module format`, signature error, oops, lock warning, or other kernel warning.

The stored evidence is the build metadata, module metadata, deployment transcript, and dmesg log. Compilation alone is insufficient evidence of completion.

## Out of Scope

- GUD USB protocol definitions or USB ID matching.
- DRM, GEM, framebuffer, KMS, EDID, or HDMI output behavior.
- Raspberry Pi gadget configuration or validation.
- Kernel core modifications, kernel image rebuild/deployment, boot partition flashing, or rollback tooling.
- ADB-over-IP as a primary or documented alternate path for this ticket.
