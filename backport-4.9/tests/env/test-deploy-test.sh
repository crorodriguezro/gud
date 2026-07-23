#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
failures=0

# Helper: build a minimal test fixture
make_fixture() {
    local tmp="$1"
    local modules_output="${2:-}"         # content of /proc/modules on phone
    local load_dmesg_extra="${3:-}"       # extra lines in load dmesg
    local unload_dmesg_extra="${4:-}"     # extra lines in unload dmesg

    # Fake gud.ko (just needs to be a file; modinfo/nm will likely fail gracefully)
    touch "$tmp/gud.ko"

    # Fake kernel build dir
    mkdir -p "$tmp/kbuild"

    # Manifest
    cat > "$tmp/manifest.env" <<EOF
PHONE_KERNEL_RELEASE=4.9.337-OnePlus
PHONE_CONFIG_SHA256=abc
PHONE_ARCH=arm64
KERNEL_SOURCE_URL=https://example.com/kernel.git
KERNEL_SOURCE_COMMIT=abcdef1234567890abcdef1234567890abcdef12
KERNEL_SOURCE_REF=refs/heads/test
CROSS_COMPILE=aarch64-linux-gnu-
TOOLCHAIN_VERSION=mock
CONFIG_MODVERSIONS=n
CONFIG_MODULE_SIG=n
CONFIG_MODULE_SIG_FORCE=n
KERNEL_BUILD_DIR=$tmp/kbuild
EOF

    cat > "$tmp/adb" <<MOCK
#!/usr/bin/env bash
# Strip -s serial
if [ "\$1" = "-s" ]; then shift 2; fi
subcmd="\$1"; shift
case "\$subcmd" in
  devices)   printf 'List of devices attached\ntest123\tdevice\n'; exit 0 ;;
  get-state) printf 'device\n'; exit 0 ;;
  push)      exit 0 ;;
  shell)
    cmd="\$*"
    case "\$cmd" in
      'cat /proc/modules')
        printf '%s\n' "$modules_output"
        exit 0 ;;
      'sudo insmod /home/phablet/gud.ko')
        exit 0 ;;
      'sudo rmmod gud')
        exit 0 ;;
      dmesg)
        # Distinguish load vs unload by counting calls via a state file
        state_file="$tmp/.dmesg_call"
        if [ ! -f "\$state_file" ]; then
            touch "\$state_file"
            printf 'gud: build probe loaded\n%s\n' "$load_dmesg_extra"
        else
            printf 'gud: build probe loaded\ngud: build probe unloaded\n%s\n' "$unload_dmesg_extra"
        fi
        exit 0 ;;
      *) exit 0 ;;
    esac ;;
  *) exit 0 ;;
esac
MOCK
    chmod +x "$tmp/adb"
}

# Test 1: already-loaded module → refuse
(
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    make_fixture "$tmp" "gud 12345 0 - Live 0x0000000000000000"

    actual_output=$(ADB="$tmp/adb" MANIFEST="$tmp/manifest.env" MODULE_PATH="$tmp/gud.ko" \
        DEPLOY_DIR="$tmp/local" bash "$repo_root/backport-4.9/env/deploy-test.sh" 2>&1) || rc=$?
    rc=${rc:-0}
    if [ "$rc" -ne 2 ]; then
        printf 'FAIL [already-loaded]: expected exit 2, got %s\n' "$rc" >&2
        exit 1
    fi
    if ! printf '%s' "$actual_output" | grep -qF 'refusing to replace an already-loaded gud module'; then
        printf 'FAIL [already-loaded]: expected message not found\nOutput: %s\n' "$actual_output" >&2
        exit 1
    fi
    printf 'PASS [already-loaded]\n'
) || failures=$((failures + 1))

# Test 2: success path
(
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    make_fixture "$tmp" "" "" ""

    actual_output=$(ADB="$tmp/adb" MANIFEST="$tmp/manifest.env" MODULE_PATH="$tmp/gud.ko" \
        DEPLOY_DIR="$tmp/local" bash "$repo_root/backport-4.9/env/deploy-test.sh" 2>&1) || rc=$?
    rc=${rc:-0}
    if [ "$rc" -ne 0 ]; then
        printf 'FAIL [success]: expected exit 0, got %s\nOutput:\n%s\n' "$rc" "$actual_output" >&2
        exit 1
    fi
    for f in module-metadata.txt load-dmesg.txt unload-dmesg.txt; do
        if [ ! -f "$tmp/local/evidence/$f" ]; then
            printf 'FAIL [success]: missing evidence file %s\n' "$f" >&2
            exit 1
        fi
    done
    printf 'PASS [success]\n'
) || failures=$((failures + 1))

# Test 3: signature failure in load dmesg → reject but preserve evidence
(
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    make_fixture "$tmp" "" "Required key not available" ""

    actual_output=$(ADB="$tmp/adb" MANIFEST="$tmp/manifest.env" MODULE_PATH="$tmp/gud.ko" \
        DEPLOY_DIR="$tmp/local" bash "$repo_root/backport-4.9/env/deploy-test.sh" 2>&1) || rc=$?
    rc=${rc:-0}
    if [ "$rc" -eq 0 ]; then
        printf 'FAIL [sig-fail]: expected nonzero exit\n' >&2
        exit 1
    fi
    if [ ! -f "$tmp/local/evidence/load-dmesg.txt" ]; then
        printf 'FAIL [sig-fail]: load-dmesg.txt not preserved\n' >&2
        exit 1
    fi
    printf 'PASS [sig-fail]\n'
) || failures=$((failures + 1))

printf 'deploy-test tests: %s failures\n' "$failures"
exit "$failures"
