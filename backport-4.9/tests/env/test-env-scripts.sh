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
assert_file backport-4.9/gud_drv.c
assert_file backport-4.9/env/.gitignore
assert_file backport-4.9/env/target-manifest.env.example

for text in 'USB_DEVICE(0x1d50, 0x614d)' 'MODULE_LICENSE("GPL")' 'module_usb_driver'; do
    grep -Fq "$text" "$repo_root/backport-4.9/gud_drv.c" || failures=$((failures + 1))
done
if grep -Eq 'alloc_workqueue|drm_dev_register' \
    "$repo_root/backport-4.9/gud_drv.c"; then
    printf 'gud_drv.c contains forbidden driver registration\n' >&2
    failures=$((failures + 1))
fi
for key in PHONE_KERNEL_RELEASE PHONE_CONFIG_SHA256 PHONE_ARCH KERNEL_SOURCE_URL \
    KERNEL_SOURCE_COMMIT KERNEL_SOURCE_REF CROSS_COMPILE TOOLCHAIN_VERSION \
    CONFIG_MODVERSIONS CONFIG_MODULE_SIG CONFIG_MODULE_SIG_FORCE KERNEL_BUILD_DIR; do
    grep -Eq "^${key}=" "$repo_root/backport-4.9/env/target-manifest.env.example" || failures=$((failures + 1))
done

# ---------------------------------------------------------------------------
# Task 2: mock-ADB tests for capture-phone.sh
# ---------------------------------------------------------------------------
run_capture_test() {
    local test_name="$1"
    local mock_adb_body="$2"
    local expected_status="$3"
    local expected_message="$4"

    local tmp
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' RETURN

    cat > "$tmp/adb" <<EOF
#!/usr/bin/env bash
$mock_adb_body
EOF
    chmod +x "$tmp/adb"

    local actual_output actual_status
    actual_output=$(ADB="$tmp/adb" CAPTURE_DIR="$tmp/capture" bash "$repo_root/backport-4.9/env/capture-phone.sh" 2>&1) || actual_status=$?
    actual_status=${actual_status:-0}

    if [ "$actual_status" -ne "$expected_status" ]; then
        printf 'FAIL [%s]: expected exit %s got %s\n' "$test_name" "$expected_status" "$actual_status" >&2
        failures=$((failures + 1))
        return
    fi
    if [ -n "$expected_message" ] && ! printf '%s' "$actual_output" | grep -qF "$expected_message"; then
        printf 'FAIL [%s]: expected message %q not found in output: %s\n' "$test_name" "$expected_message" "$actual_output" >&2
        failures=$((failures + 1))
        return
    fi
}

# Test: multiple ADB devices → error
run_capture_test "multiple-devices" \
    'case "$1" in
  devices) printf "List of devices attached\nserial1\tdevice\nserial2\tdevice\n" ;;
  *) exit 1 ;;
esac' \
    2 \
    "multiple ADB devices found; set ADB_SERIAL"

# Test: success path – one device, modules enabled, config.gz available
run_capture_test_success() {
    local tmp
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' RETURN

    # Pre-generate a config.gz the mock adb pull will "provide"
    printf 'CONFIG_MODULES=y\nCONFIG_MODVERSIONS=y\nCONFIG_MODULE_SIG=n\nCONFIG_MODULE_SIG_FORCE=n\nCONFIG_MODULE_COMPRESS=n\n' \
        | gzip > "$tmp/fake-config.gz"

    # The mock adb: args are like: adb -s serial <subcmd> [args...]
    cat > "$tmp/adb" <<MOCK
#!/usr/bin/env bash
# shift past -s serial when present
if [ "\$1" = "-s" ]; then shift 2; fi
subcmd="\$1"; shift
case "\$subcmd" in
  devices)   printf 'List of devices attached\ntest123\tdevice\n'; exit 0 ;;
  get-state) printf 'device\n'; exit 0 ;;
  shell)
    cmd="\$*"
    case "\$cmd" in
      'uname -r')              printf '4.9.337-OnePlus\n'; exit 0 ;;
      'test -r /proc/config.gz') exit 0 ;;
      *)                       printf ''; exit 0 ;;
    esac ;;
  pull)
    # pull /proc/config.gz /dest/path
    dest="\$2"
    cp "$tmp/fake-config.gz" "\$dest"
    exit 0 ;;
  *) exit 0 ;;
esac
MOCK
    chmod +x "$tmp/adb"

    local actual_output actual_status=0
    actual_output=$(ADB="$tmp/adb" CAPTURE_DIR="$tmp/capture" \
        bash "$repo_root/backport-4.9/env/capture-phone.sh" 2>&1) || actual_status=$?

    if [ "$actual_status" -ne 0 ]; then
        printf 'FAIL [success-path]: expected exit 0 got %s\nOutput: %s\n' "$actual_status" "$actual_output" >&2
        failures=$((failures + 1))
        return
    fi
    # CAPTURE_DIR sets local_dir; the script appends /capture internally
    for f in phone.config phone.config.sha256 module-policy.txt; do
        if [ ! -f "$tmp/capture/capture/$f" ]; then
            printf 'FAIL [success-path]: missing %s\n' "$f" >&2
            failures=$((failures + 1))
        fi
    done
    printf '%s' "$actual_output" | grep -q 'PHONE_KERNEL_RELEASE=' || {
        printf 'FAIL [success-path]: stdout missing PHONE_KERNEL_RELEASE=\n' >&2
        failures=$((failures + 1))
    }
    printf '%s' "$actual_output" | grep -q 'PHONE_CONFIG_SHA256=' || {
        printf 'FAIL [success-path]: stdout missing PHONE_CONFIG_SHA256=\n' >&2
        failures=$((failures + 1))
    }
}
run_capture_test_success

# Test: no devices → error
run_capture_test "no-devices" \
    'case "$1" in
  devices) printf "List of devices attached\n" ;;
  *) exit 1 ;;
esac' \
    2 \
    "no authorized ADB device found"

# Task 5: verify README references only implemented commands
for script in capture-phone.sh prepare-kernel.sh deploy-test.sh; do
    if [ ! -x "$repo_root/backport-4.9/env/$script" ]; then
        printf 'missing or non-executable: backport-4.9/env/%s\n' "$script" >&2
        failures=$((failures + 1))
    fi
done

if [ ! -f "$repo_root/backport-4.9/README.md" ]; then
    printf 'missing file: backport-4.9/README.md\n' >&2
    failures=$((failures + 1))
else
    for pattern in 'GUD probe complete for 1d50:614d' 'GUD disconnected' 'Invalid module format'; do
        grep -qF "$pattern" "$repo_root/backport-4.9/README.md" || {
            printf 'README.md missing expected string: %s\n' "$pattern" >&2
            failures=$((failures + 1))
        }
    done
    for script in capture-phone.sh prepare-kernel.sh deploy-test.sh; do
        grep -qF "$script" "$repo_root/backport-4.9/README.md" || {
            printf 'README.md does not mention: %s\n' "$script" >&2
            failures=$((failures + 1))
        }
    done
fi

# Fix 7: verify backport-4.9/.gitignore exists and covers *.ko
if [ ! -f "$repo_root/backport-4.9/.gitignore" ]; then
    printf 'missing file: backport-4.9/.gitignore\n' >&2
    failures=$((failures + 1))
else
    grep -qF '*.ko' "$repo_root/backport-4.9/.gitignore" || {
        printf 'backport-4.9/.gitignore does not ignore *.ko\n' >&2
        failures=$((failures + 1))
    }
fi

# Fix 8: verify README test paths are correct relative to cd backport-4.9
if [ -f "$repo_root/backport-4.9/README.md" ]; then
    if grep -q 'backport-4.9/tests/' "$repo_root/backport-4.9/README.md"; then
        printf 'README.md uses wrong test path (backport-4.9/tests/ inside cd backport-4.9 section)\n' >&2
        failures=$((failures + 1))
    fi
fi

exit "$failures"
