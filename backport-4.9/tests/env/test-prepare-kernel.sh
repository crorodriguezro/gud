#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
failures=0

# ── helper ────────────────────────────────────────────────────────────────────
run_prepare_test() {
    local test_name="$1"
    local expected_status="$2"
    local expected_message="${3:-}"
    shift 3
    # remaining args: manifest key=value overrides
    local tmp
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' RETURN

    local xc="aarch64-linux-gnu-"
    local commit="${MOCK_COMMIT:-abcdef1234567890abcdef1234567890abcdef12}"
    local mock_release="${MOCK_KERNELRELEASE:-4.9.337-OnePlus}"
    local source_url="https://example.com/kernel.git"

    # Mock git — handles clone, fetch, checkout, rev-parse, and remote get-url
    cat > "$tmp/git" <<MOCK
#!/usr/bin/env bash
subcmd="\$1"; shift
case "\$subcmd" in
  clone)   mkdir -p "\${@: -1}/.git"; exit 0 ;;
  -C)
    shift  # skip repo path arg
    subcmd2="\$1"; shift
    case "\$subcmd2" in
      fetch)    exit 0 ;;
      checkout) exit 0 ;;
      rev-parse) printf '%s\n' "$commit"; exit 0 ;;
      remote)   printf '%s\n' "$source_url"; exit 0 ;;
    esac ;;
esac
exit 0
MOCK
    chmod +x "$tmp/git"

    # Mock make — creates required artifacts; does NOT modify .config so config-drift
    # diff passes; returns mock_release for the kernelrelease target
    cat > "$tmp/make" <<MOCK
#!/usr/bin/env bash
build_dir=""
for arg in "\$@"; do
    case "\$arg" in O=*) build_dir="\${arg#O=}" ;; esac
done
for arg in "\$@"; do
    if [ "\$arg" = "kernelrelease" ]; then
        printf '%s\n' "$mock_release"
        exit 0
    fi
done
if [ -n "\$build_dir" ]; then
    mkdir -p "\$build_dir/include/generated"
    touch "\$build_dir/include/generated/autoconf.h"
    if grep -q "CONFIG_MODVERSIONS=y" "\$build_dir/.config" 2>/dev/null; then
        touch "\$build_dir/Module.symvers"
    fi
fi
exit 0
MOCK
    chmod +x "$tmp/make"

    # Mock cross-compiler
    cat > "$tmp/${xc}gcc" <<'MOCK'
#!/usr/bin/env bash
printf 'aarch64-linux-gnu-gcc (mock) 9.3.0\n'
exit 0
MOCK
    chmod +x "$tmp/${xc}gcc"

    # Build phone.config — no MODULE_SIG_FORCE by default
    mkdir -p "$tmp/capture"
    printf 'CONFIG_MODULES=y\nCONFIG_MODVERSIONS=y\nCONFIG_MODULE_SIG=n\nCONFIG_MODULE_SIG_FORCE=n\nCONFIG_MODULE_COMPRESS=n\n' \
        > "$tmp/capture/phone.config"

    local actual_sha
    actual_sha=$(sha256sum "$tmp/capture/phone.config" | awk '{print $1}')

    # Build manifest
    cat > "$tmp/manifest.env" <<EOF
PHONE_KERNEL_RELEASE=$mock_release
PHONE_CONFIG_SHA256=$actual_sha
PHONE_ARCH=arm64
KERNEL_SOURCE_URL=$source_url
KERNEL_SOURCE_COMMIT=$commit
KERNEL_SOURCE_REF=refs/heads/test
CROSS_COMPILE=${xc}
TOOLCHAIN_VERSION=mock
CONFIG_MODVERSIONS=y
CONFIG_MODULE_SIG=n
CONFIG_MODULE_SIG_FORCE=n
KERNEL_BUILD_DIR=
EOF

    # Apply any overrides passed as extra args
    for override in "$@"; do
        key="${override%%=*}"
        val="${override#*=}"
        if grep -q "^${key}=" "$tmp/manifest.env"; then
            sed -i "s|^${key}=.*|${key}=${val}|" "$tmp/manifest.env"
        else
            printf '%s=%s\n' "$key" "$val" >> "$tmp/manifest.env"
        fi
        # Also update phone.config for CONFIG_* overrides so SHA still matches
        if printf '%s' "$key" | grep -q '^CONFIG_'; then
            if grep -q "^${key}=" "$tmp/capture/phone.config"; then
                sed -i "s|^${key}=.*|${key}=${val}|" "$tmp/capture/phone.config"
            else
                printf '%s=%s\n' "$key" "$val" >> "$tmp/capture/phone.config"
            fi
            # Recompute SHA after config change
            actual_sha=$(sha256sum "$tmp/capture/phone.config" | awk '{print $1}')
            sed -i "s|^PHONE_CONFIG_SHA256=.*|PHONE_CONFIG_SHA256=$actual_sha|" "$tmp/manifest.env"
        fi
    done

    local actual_output actual_status=0
    actual_output=$(
        export PATH="$tmp:$PATH"
        export MOCK_COMMIT="$commit"
        MANIFEST="$tmp/manifest.env" \
        CAPTURE_DIR="$tmp/capture" \
        PREPARE_DIR="$tmp/local" \
            bash "$repo_root/backport-4.9/env/prepare-kernel.sh" 2>&1
    ) || actual_status=$?

    if [ "$actual_status" -ne "$expected_status" ]; then
        printf 'FAIL [%s]: expected exit %s got %s\nOutput:\n%s\n' \
            "$test_name" "$expected_status" "$actual_status" "$actual_output" >&2
        failures=$((failures + 1))
        return
    fi
    if [ -n "$expected_message" ] && ! printf '%s' "$actual_output" | grep -qF "$expected_message"; then
        printf 'FAIL [%s]: expected message %q\nOutput:\n%s\n' \
            "$test_name" "$expected_message" "$actual_output" >&2
        failures=$((failures + 1))
        return
    fi
    # For success: check log file exists and records the pinned SHA
    if [ "$expected_status" -eq 0 ]; then
        if [ ! -f "$tmp/local/evidence/prepare-kernel.log" ]; then
            printf 'FAIL [%s]: missing prepare-kernel.log\n' "$test_name" >&2
            failures=$((failures + 1))
            return
        fi
        if ! grep -qF "$commit" "$tmp/local/evidence/prepare-kernel.log"; then
            printf 'FAIL [%s]: log does not record pinned SHA\n' "$test_name" >&2
            failures=$((failures + 1))
            return
        fi
    fi
    printf 'PASS [%s]\n' "$test_name"
}

# Test 1: empty / non-SHA KERNEL_SOURCE_COMMIT → reject
run_prepare_test "empty-commit" 2 \
    "KERNEL_SOURCE_COMMIT must be a full 40-character Git SHA" \
    "KERNEL_SOURCE_COMMIT=not-a-sha"

# Test 2: short SHA → reject
run_prepare_test "bad-sha-format" 2 \
    "KERNEL_SOURCE_COMMIT must be a full 40-character Git SHA" \
    "KERNEL_SOURCE_COMMIT=abc123"

# Test 3: success path
run_prepare_test "success" 0 ""

# Test 4: release mismatch → reject with exact message
MOCK_KERNELRELEASE="4.9.999-wrong" \
run_prepare_test "release-mismatch" 2 \
    "prepared kernel release does not match phone release" \
    "PHONE_KERNEL_RELEASE=4.9.337-OnePlus"

# Test 5: CONFIG_MODULE_SIG_FORCE=y → hard block
run_prepare_test "sig-force-blocked" 2 \
    "CONFIG_MODULE_SIG_FORCE=y" \
    "CONFIG_MODULE_SIG_FORCE=y"

printf 'prepare-kernel tests: %s failures\n' "$failures"
exit "$failures"
