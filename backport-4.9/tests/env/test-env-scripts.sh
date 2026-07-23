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

exit "$failures"
