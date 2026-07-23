# GUD Backport — OnePlus 6 External Module Environment

Reproducibly build and ADB-load a no-op `gud.ko` against the installed
OnePlus 6 Ubuntu Touch Linux 4.9 kernel.

## Prerequisites

- ADB authorized USB connection to the OnePlus 6
- `git`, `make`, `gzip`, `sha256sum`, `modinfo`, `nm`
- arm64 cross-compiler (set `CROSS_COMPILE` in the manifest)
- The phone must have `CONFIG_MODULES=y`

## Workflow

```bash
cd backport-4.9
./env/capture-phone.sh
cp env/target-manifest.env.example env/target-manifest.env
# Populate target-manifest.env with the capture output and the selected
# public source URL / 40-character commit SHA, toolchain, and expected
# kernel release.  Record selection rationale and rejected candidates
# in env/local/evidence/ — these stay ignored and never committed.
./env/prepare-kernel.sh
make MANIFEST="$PWD/env/target-manifest.env" modules
./env/deploy-test.sh
```

## Source selection

`capture-phone.sh` does **not** select a kernel source.  After capture,
the operator must:

1. Identify a public kernel repository whose `uname -r` output, `.config`
   SHA-256, and compiler match `env/local/capture/`.
2. Record the immutable 40-character commit SHA, source URL, `KERNEL_SOURCE_REF`,
   and `TOOLCHAIN_VERSION` in `env/target-manifest.env`.
3. Document any rejected candidates under `env/local/evidence/`.

Do not proceed to `prepare-kernel.sh` if source matching is unresolved.

## Hard blockers

Stop and do not continue if any of the following is true:

- `CONFIG_MODULES` is not `y` — module loading is disabled on this kernel.
- `CONFIG_MODULE_SIG_FORCE=y` — modules must be signed; obtain or build
  a matching key before loading.
- The `uname -r` of the prepared source tree does not match
  `PHONE_KERNEL_RELEASE` — the build would produce an ABI-incompatible module.
- `Module.symvers` is absent when `CONFIG_MODVERSIONS=y` — symbol version
  CRC checks will fail at load time.
- Source matching remains unresolved — do not guess or substitute a generic tree.

Do **not** modify the phone boot image, installed kernel, or device configuration.

## Local artifacts

All captures, source trees, build output, modules, and evidence logs are
written under `env/local/` which is git-ignored.  Nothing in `env/local/`
is ever committed.

| Path | Content |
| --- | --- |
| `env/local/capture/` | Raw ADB captures, `phone.config`, SHA-256, module policy |
| `env/local/kernel/source/` | Pinned kernel source checkout |
| `env/local/kernel/build/` | Prepared external-module build tree |
| `env/local/evidence/` | `prepare-kernel.log`, `module-metadata.txt`, `load-dmesg.txt`, `unload-dmesg.txt` |

## Expected dmesg evidence

After a successful `deploy-test.sh` run, `env/local/evidence/` must contain:

- `load-dmesg.txt` — must include `gud: build probe loaded`
- `unload-dmesg.txt` — must include `gud: build probe unloaded`

The deploy script rejects any output matching `Invalid module format`,
`Required key not available`, `module verification failed`, `BUG:`,
`Oops`, `WARNING:`, `lockdep`, or `use-after-free`.

## Tests

```bash
bash backport-4.9/tests/env/test-env-scripts.sh
bash backport-4.9/tests/env/test-prepare-kernel.sh
bash backport-4.9/tests/env/test-deploy-test.sh
```

All three test scripts are hermetic and do not require a phone or kernel tree.
