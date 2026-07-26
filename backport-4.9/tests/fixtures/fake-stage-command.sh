#!/usr/bin/env bash
set -euo pipefail

command_name=$(basename "$0")

case "$command_name" in
modinfo)
	case "${1:-}" in
	-F)
		case "${2:-}" in
		version) printf 'xdisp-p0.1-adaptive-12800-v1\n' ;;
		description) printf 'OnePlus 6 GUD XDISP adaptive LZ4 12800-byte diagnostic\n' ;;
		vermagic) printf '4.9.112-g6b190d86b SMP preempt mod_unload modversions aarch64\n' ;;
		alias) printf 'usb:v1D50p614Dd*dc*dsc*dp*ic*isc*ip*in*\n' ;;
		depends) ;;
		*) exit 2 ;;
		esac
		;;
	*)
		printf 'description: OnePlus 6 GUD XDISP adaptive LZ4 12800-byte diagnostic\n'
		;;
	esac
	;;
scp)
	: > "$FAKE_SCP_CALLED"
	;;
nm)
	;;
ssh)
	remote_command="${!#}"
	case "$remote_command" in
	"sha256sum /home/phablet/gud.ko")
		printf '%s  /home/phablet/gud.ko\n' "$FAKE_NORMAL_SHA"
		;;
	test\ -e*)
		if [ "${FAKE_REMOTE_EXISTS:-0}" = 1 ]; then
			exit 0
		fi
		exit 1
		;;
	uname\ -a*)
		printf 'Linux fake-phone 4.9.112-g6b190d86b\n'
		;;
	sha256sum\ *.incoming)
		printf '%s  incoming\n' "${FAKE_INCOMING_SHA:-$FAKE_LOCAL_SHA}"
		;;
	mv\ *)
		: > "$FAKE_MV_CALLED"
		;;
	sha256sum\ /home/phablet/gud.xdisp-p0.1-adaptive-12800-*.ko)
		printf '%s  diagnostic\n' "$FAKE_LOCAL_SHA"
		;;
	*)
		printf 'unexpected fake ssh command: %s\n' "$remote_command" >&2
		exit 97
		;;
	esac
	;;
*)
	printf 'unsupported fake command: %s\n' "$command_name" >&2
	exit 98
	;;
esac
