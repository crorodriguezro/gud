#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
ssh_bin="${SSH:-ssh}"
scp_bin="${SCP:-scp}"
phone_host="${PHONE_HOST:-phablet@192.168.1.120}"
module_path="${MODULE_PATH:-$repo_dir/variants/xdisp-lz4-12800/gud.ko}"
remote_module_path="${REMOTE_MODULE_PATH:-}"
normal_module_path=/home/phablet/gud.ko
normal_sha_expected="${NORMAL_MODULE_SHA256:-bd15c2c1bc4cd941bcac88bb13276b67620d9e2eec515973ff815add68f3630c}"
evidence_dir="${EVIDENCE_DIR:-$script_dir/local/evidence/xdisp-p0.1-oneplus-adaptive-stage}"

if [ ! -f "$module_path" ]; then
	printf 'diagnostic module not found: %s\n' "$module_path" >&2
	exit 2
fi
if [ -z "$remote_module_path" ]; then
	printf 'REMOTE_MODULE_PATH is required and must include the source commit\n' >&2
	exit 2
fi
if [[ ! "$remote_module_path" =~ ^/home/phablet/gud\.xdisp-p0\.1-adaptive-12800-[0-9a-f]{7,40}\.ko$ ]]; then
	printf 'refusing unsafe diagnostic path: %s\n' "$remote_module_path" >&2
	exit 2
fi
if [ "$remote_module_path" = "$normal_module_path" ]; then
	printf 'refusing to overwrite normal module: %s\n' "$normal_module_path" >&2
	exit 2
fi
case "$phone_host" in
	*[!A-Za-z0-9._@:-]*|'')
		printf 'PHONE_HOST contains unsupported characters: %s\n' "$phone_host" >&2
		exit 2
		;;
esac

module_version=$(modinfo -F version "$module_path")
module_description=$(modinfo -F description "$module_path")
module_vermagic=$(modinfo -F vermagic "$module_path")
module_alias=$(modinfo -F alias "$module_path")
module_depends=$(modinfo -F depends "$module_path")
if [ "$module_version" != xdisp-p0.1-adaptive-12800-v1 ] ||
   [ "$module_description" != \
     'OnePlus 6 GUD XDISP adaptive LZ4 12800-byte diagnostic' ] ||
   [[ "$module_vermagic" != 4.9.112-g6b190d86b* ]] ||
   [ "$module_alias" != 'usb:v1D50p614Dd*dc*dsc*dp*ic*isc*ip*in*' ] ||
   [ -n "$module_depends" ]; then
	printf 'module identity mismatch\n' >&2
	printf 'version=%s\ndescription=%s\nvermagic=%s\nalias=%s\ndepends=%s\n' \
		"$module_version" "$module_description" "$module_vermagic" \
		"$module_alias" "$module_depends" >&2
	exit 2
fi
if ! unresolved_symbols=$(nm -u "$module_path"); then
	printf 'unable to inspect diagnostic module symbols\n' >&2
	exit 2
fi
if printf '%s\n' "$unresolved_symbols" | grep -qi lz4; then
	printf 'module has an unresolved LZ4 dependency\n' >&2
	exit 2
fi

mkdir -p "$evidence_dir"
local_sha=$(sha256sum "$module_path" | awk '{print $1}')
incoming_path="${remote_module_path}.incoming"

remote_normal_sha=$(
	"$ssh_bin" -F /dev/null -o ConnectTimeout=5 \
		-o StrictHostKeyChecking=accept-new "$phone_host" \
		"sha256sum $normal_module_path" |
	awk '{print $1}'
)
if [ "$remote_normal_sha" != "$normal_sha_expected" ]; then
	printf 'normal module hash mismatch; refusing stage\nexpected: %s\nactual:   %s\n' \
		"$normal_sha_expected" "$remote_normal_sha" >&2
	exit 2
fi

if "$ssh_bin" -F /dev/null -o ConnectTimeout=5 \
	-o StrictHostKeyChecking=accept-new "$phone_host" \
	"test -e $remote_module_path -o -e $incoming_path"; then
	printf 'diagnostic or incoming path already exists; refusing overwrite: %s\n' \
		"$remote_module_path" >&2
	exit 2
fi

{
	printf 'phone_host=%s\n' "$phone_host"
	printf 'remote_module=%s\n' "$remote_module_path"
	printf 'local_module=%s\n' "$module_path"
	printf 'local_sha256=%s\n' "$local_sha"
	printf 'normal_sha256_before=%s\n' "$remote_normal_sha"
	modinfo "$module_path"
} > "$evidence_dir/stage-metadata.txt"

"$ssh_bin" -F /dev/null -o ConnectTimeout=5 \
	-o StrictHostKeyChecking=accept-new "$phone_host" \
	'uname -a; printf "\n--- modules ---\n"; cat /proc/modules; printf "\n--- drm ---\n"; ls -l /dev/dri 2>/dev/null || true' \
	> "$evidence_dir/phone-before.txt"

"$scp_bin" -F /dev/null -o ConnectTimeout=5 \
	-o StrictHostKeyChecking=accept-new \
	"$module_path" "$phone_host:$incoming_path"

remote_incoming_sha=$(
	"$ssh_bin" -F /dev/null -o ConnectTimeout=5 \
		-o StrictHostKeyChecking=accept-new "$phone_host" \
		"sha256sum $incoming_path" |
	awk '{print $1}'
)
if [ "$remote_incoming_sha" != "$local_sha" ]; then
	printf 'staged incoming hash mismatch\nlocal:  %s\nremote: %s\n' \
		"$local_sha" "$remote_incoming_sha" >&2
	exit 2
fi

"$ssh_bin" -F /dev/null -o ConnectTimeout=5 \
	-o StrictHostKeyChecking=accept-new "$phone_host" \
	"mv $incoming_path $remote_module_path"

remote_final_sha=$(
	"$ssh_bin" -F /dev/null -o ConnectTimeout=5 \
		-o StrictHostKeyChecking=accept-new "$phone_host" \
		"sha256sum $remote_module_path" |
	awk '{print $1}'
)
remote_normal_sha_after=$(
	"$ssh_bin" -F /dev/null -o ConnectTimeout=5 \
		-o StrictHostKeyChecking=accept-new "$phone_host" \
		"sha256sum $normal_module_path" |
	awk '{print $1}'
)
if [ "$remote_final_sha" != "$local_sha" ]; then
	printf 'final diagnostic hash mismatch\nlocal:  %s\nremote: %s\n' \
		"$local_sha" "$remote_final_sha" >&2
	exit 2
fi
if [ "$remote_normal_sha_after" != "$normal_sha_expected" ]; then
	printf 'normal module changed during staging; stop before activation\n' >&2
	exit 2
fi

{
	printf 'diagnostic_sha256=%s\n' "$remote_final_sha"
	printf 'normal_sha256_after=%s\n' "$remote_normal_sha_after"
	printf 'staged_only=true\n'
	printf 'activated=false\n'
} > "$evidence_dir/stage-result.txt"

printf 'diagnostic staged without activation: %s\n' "$remote_module_path"
printf 'diagnostic SHA-256: %s\n' "$remote_final_sha"
printf 'normal module preserved: %s\n' "$remote_normal_sha_after"
printf 'evidence: %s\n' "$evidence_dir"
