#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
file=backport-4.9/tests/gud-kms-stage.c
failures=0

for text in \
    '"caps"' '"dumb"' '"fb"' \
    '"resources"' '"connector"' '"encoder-crtc"' '"planes"' '"properties"' \
    '"atomic-build"' '"atomic-test"' '"atomic-commit"' \
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
    'drmModeCreatePropertyBlob' \
    'drmModeAtomicAddProperty' \
    'DRM_MODE_ATOMIC_ALLOW_MODESET'; do
    grep -qF "$text" "$repo_root/$file" || {
        printf 'FAIL [required]: %s\n' "$text" >&2
        failures=$((failures + 1))
    }
done

for text in \
    '"resources"' '"connector"' '"encoder-crtc"' '"planes"' '"properties"' \
    'if (strcmp(stage, "resources") == 0)' \
    'if (strcmp(stage, "connector") == 0)' \
    'if (strcmp(stage, "encoder-crtc") == 0)' \
    'if (strcmp(stage, "planes") == 0)' \
    'if (strcmp(stage, "properties") == 0)' \
    'drmModeGetConnector(fd, resources->connectors[i])' \
    'drmModeGetEncoder(fd, connector->encoder_id)' \
    'drmModeGetPlaneResources(fd)' \
    'drmModeGetPlane(fd, plane_res->planes[i])' \
    'DRM_PLANE_TYPE_PRIMARY'; do
    grep -qF "$text" "$repo_root/$file" || {
        printf 'FAIL [enumeration stage]: %s\n' "$text" >&2
        failures=$((failures + 1))
    }
done

for text in \
    '"properties"' \
    '"atomic-build"' \
    'if (strcmp(stage, "properties") == 0)' \
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

printf 'gud-kms-stage-contract tests: %s failures\n' "$failures"
exit "$failures"
