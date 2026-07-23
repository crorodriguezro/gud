#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sysfs_root="${SYSFS_USB_ROOT:-/sys/bus/usb/devices}"
capture_dir="${PI_CAPTURE_DIR:-$script_dir/local/pi-usb}"
lsusb_bin="${LSUSB:-lsusb}"
usb_devices_bin="${USB_DEVICES:-usb-devices}"
device_name="${1:-}"

if [ -z "$device_name" ] || [ ! -d "$sysfs_root/$device_name" ]; then
    printf 'usage: %s <sysfs-usb-device-name>\n' "$0" >&2
    exit 2
fi

device_dir="$sysfs_root/$device_name"
for name in idVendor idProduct busnum devnum descriptors; do
    if [ ! -r "$device_dir/$name" ]; then
        printf 'selected USB device is missing readable %s: %s\n' "$name" "$device_dir" >&2
        exit 2
    fi
done

vendor=$(tr -d '[:space:]' < "$device_dir/idVendor")
product=$(tr -d '[:space:]' < "$device_dir/idProduct")
if [ "$vendor" != 1d50 ] || [ "$product" != 614d ]; then
    printf 'selected device is not the configured Pi GUD gadget: expected 1d50:614d\n' >&2
    exit 2
fi

busnum=$(tr -d '[:space:]' < "$device_dir/busnum")
devnum=$(tr -d '[:space:]' < "$device_dir/devnum")

# Zero-pad to three digits
busnum_pad=$(printf '%03d' "$busnum")
devnum_pad=$(printf '%03d' "$devnum")
selector="${busnum_pad}:${devnum_pad}"

mkdir -p "$capture_dir"

"$lsusb_bin" -v -s "$selector" > "$capture_dir/lsusb-v.txt"
"$usb_devices_bin" > "$capture_dir/usb-devices.txt"
cp "$device_dir/descriptors" "$capture_dir/device-descriptors.bin"

# Copy every interface descriptor
iface_count=0
for iface_dir in "$sysfs_root/$device_name":*; do
    [ -d "$iface_dir" ] || continue
    iface_name=$(basename "$iface_dir")
    if [ -r "$iface_dir/descriptors" ]; then
        cp "$iface_dir/descriptors" "$capture_dir/interface-${iface_name}-descriptors.bin"
        iface_count=$((iface_count + 1))
    fi
done

if [ "$iface_count" -eq 0 ]; then
    printf 'no interface descriptors found under %s\n' "$sysfs_root/$device_name" >&2
    exit 2
fi

# Write identity.env
cat > "$capture_dir/identity.env" <<EOF
GUD_USB_SYSFS_DEVICE=${device_name}
GUD_USB_VENDOR_ID=0x1d50
GUD_USB_PRODUCT_ID=0x614d
GUD_USB_BUSNUM=${busnum_pad}
GUD_USB_DEVNUM=${devnum_pad}
EOF

printf 'Pi USB capture written to: %s\n' "$capture_dir"
