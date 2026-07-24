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
    'printf("stage=%s complete\n", stage);' \
    'if (strcmp(stage, "dumb") == 0)' \
    'if (strcmp(stage, "fb") == 0)' \
    'if (strcmp(stage, "atomic") != 0)' \
    'drmModeCreatePropertyBlob' \
    'drmModeAtomicAddProperty' \
    'DRM_MODE_ATOMIC_ALLOW_MODESET'; do
    grep -qF "$text" "$repo_root/$file" || {
        printf 'FAIL [required]: %s\n' "$text" >&2
        failures=$((failures + 1))
    }
done

printf 'gud-kms-stage-contract tests: %s failures\n' "$failures"
exit "$failures"
