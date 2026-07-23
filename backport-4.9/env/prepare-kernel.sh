#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
local_dir="${PREPARE_DIR:-$script_dir/local}"
evidence_dir="$local_dir/evidence"
source_dir="$local_dir/kernel/source"
build_dir="$local_dir/kernel/build"

MANIFEST="${MANIFEST:-$script_dir/target-manifest.env}"

if [ ! -f "$MANIFEST" ]; then
    printf 'manifest not found: %s\n' "$MANIFEST" >&2
    printf 'copy env/target-manifest.env.example to env/target-manifest.env and populate it\n' >&2
    exit 2
fi

# shellcheck source=/dev/null
. "$MANIFEST"

# Validate required fields
for field in PHONE_KERNEL_RELEASE PHONE_CONFIG_SHA256 KERNEL_SOURCE_URL \
             KERNEL_SOURCE_COMMIT CROSS_COMPILE TOOLCHAIN_VERSION; do
    eval "val=\${${field}:-}"
    if [ -z "$val" ]; then
        printf '%s is required but not set in %s\n' "$field" "$MANIFEST" >&2
        exit 2
    fi
done

# Require immutable 40-character SHA
if ! echo "$KERNEL_SOURCE_COMMIT" | grep -qE '^[0-9a-f]{40}$'; then
    printf 'KERNEL_SOURCE_COMMIT must be a full 40-character Git SHA\n' >&2
    exit 2
fi

# Require captured phone.config
capture_dir="${CAPTURE_DIR:-$script_dir/local/capture}"
phone_config="$capture_dir/phone.config"
if [ ! -f "$phone_config" ]; then
    printf 'missing %s; run capture-phone.sh first\n' "$phone_config" >&2
    exit 2
fi

# Verify config SHA-256
actual_sha=$(sha256sum "$phone_config" | awk '{print $1}')
if [ "$actual_sha" != "$PHONE_CONFIG_SHA256" ]; then
    printf 'phone.config SHA-256 mismatch\nexpected: %s\nactual:   %s\n' \
        "$PHONE_CONFIG_SHA256" "$actual_sha" >&2
    exit 2
fi

mkdir -p "$evidence_dir"
log_file="$evidence_dir/prepare-kernel.log"

log() { printf '[prepare-kernel] %s\n' "$*" | tee -a "$log_file"; }

log "KERNEL_SOURCE_COMMIT=$KERNEL_SOURCE_COMMIT"
log "KERNEL_SOURCE_URL=$KERNEL_SOURCE_URL"
log "CROSS_COMPILE=$CROSS_COMPILE"
log "PHONE_KERNEL_RELEASE=$PHONE_KERNEL_RELEASE"

# Compiler version
compiler_version=$("${CROSS_COMPILE}gcc" --version 2>&1 | head -1 || echo "unknown")
log "TOOLCHAIN_VERSION_REPORTED=$compiler_version"

# Clone or reuse
if [ ! -d "$source_dir/.git" ]; then
    log "cloning $KERNEL_SOURCE_URL"
    git clone --no-checkout "$KERNEL_SOURCE_URL" "$source_dir"
fi

log "fetching commit $KERNEL_SOURCE_COMMIT"
git -C "$source_dir" fetch --depth=1 origin "$KERNEL_SOURCE_COMMIT"
git -C "$source_dir" checkout --detach "$KERNEL_SOURCE_COMMIT"
actual_head=$(git -C "$source_dir" rev-parse HEAD)
if [ "$actual_head" != "$KERNEL_SOURCE_COMMIT" ]; then
    printf 'HEAD %s does not match expected commit %s\n' "$actual_head" "$KERNEL_SOURCE_COMMIT" >&2
    exit 2
fi
log "HEAD confirmed: $actual_head"

# Prepare build tree
mkdir -p "$build_dir"
cp "$phone_config" "$build_dir/.config"
log "copied phone.config to build tree"

make -C "$source_dir" O="$build_dir" ARCH=arm64 CROSS_COMPILE="$CROSS_COMPILE" olddefconfig 2>&1 | tee -a "$log_file"
make -C "$source_dir" O="$build_dir" ARCH=arm64 CROSS_COMPILE="$CROSS_COMPILE" prepare modules_prepare 2>&1 | tee -a "$log_file"

# Check kernel release
actual_release=$(make -s -C "$source_dir" O="$build_dir" kernelrelease 2>/dev/null | tr -d '\n')
log "actual_release=$actual_release"
if [ "$actual_release" != "$PHONE_KERNEL_RELEASE" ]; then
    printf 'prepared kernel release does not match phone release\nexpected: %s\nactual:   %s\n' \
        "$PHONE_KERNEL_RELEASE" "$actual_release" >&2
    exit 2
fi

# Require generated headers
if [ ! -f "$build_dir/include/generated/autoconf.h" ]; then
    printf 'missing include/generated/autoconf.h in build tree\n' >&2
    exit 2
fi
log "autoconf.h present"

# Require Module.symvers if MODVERSIONS enabled
modversions_val=$(grep -E '^CONFIG_MODVERSIONS=' "$phone_config" | cut -d= -f2 | tr -d '"' || true)
if [ "$modversions_val" = "y" ]; then
    if [ ! -f "$build_dir/Module.symvers" ]; then
        printf 'CONFIG_MODVERSIONS=y but Module.symvers not found in build tree\n' >&2
        exit 2
    fi
    log "Module.symvers present (CONFIG_MODVERSIONS=y)"
fi

# Log policy values
for key in CONFIG_MODVERSIONS CONFIG_MODULE_SIG CONFIG_MODULE_SIG_FORCE CONFIG_MODULE_COMPRESS; do
    val=$(grep -E "^${key}[=]" "$phone_config" || echo "$key is not set")
    log "$val"
done

# Update KERNEL_BUILD_DIR in manifest (local, not committed)
if grep -q "^KERNEL_BUILD_DIR=" "$MANIFEST"; then
    sed -i "s|^KERNEL_BUILD_DIR=.*|KERNEL_BUILD_DIR=$build_dir|" "$MANIFEST"
else
    printf 'KERNEL_BUILD_DIR=%s\n' "$build_dir" >> "$MANIFEST"
fi
log "KERNEL_BUILD_DIR=$build_dir written to $MANIFEST"

log "prepare-kernel complete"
