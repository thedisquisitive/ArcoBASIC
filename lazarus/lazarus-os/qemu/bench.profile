# QEMU-only Arcology Lazarus bench fixture.
# This profile must never be installed as the production appliance default.

name=Arcology Lazarus QEMU Test Bench
branding_theme=Lazarus Default Theme
branding_product_name=Arcology Lazarus
branding_subtitle=Offline Imaging | Recovery | Hardware Migration
branding_accent=#f39a22
branding_background=#10161b
branding_surface=#171f25
branding_text=#edf1f3
branding_icon=#f39a22
branding_report_footer=Generated locally by Arcology Lazarus. SMART results describe reported device facts.

image_storage=/mnt/lazarus-storage/images
image_storage_device=/dev/disk/by-id/virtio-LAZARUS_STORAGE_TEST
source=/dev/disk/by-id/virtio-LAZARUS_SOURCE_TEST
destination=/dev/disk/by-id/virtio-LAZARUS_DEST_TEST
destination=/dev/disk/by-id/virtio-LAZARUS_INSTALL_TEST
removable_media=/dev/disk/by-id/usb-QEMU_QEMU_HARDDISK_LAZ_RECOVERY_TEST-0:0
ignored=/dev/disk/by-id/virtio-LAZARUS_SYSTEM
port_label=/dev/disk/by-id/virtio-LAZARUS_SOURCE_TEST|QEMU Source Drive
port_label=/dev/disk/by-id/virtio-LAZARUS_DEST_TEST|QEMU Destination Drive
port_label=/dev/disk/by-id/virtio-LAZARUS_INSTALL_TEST|QEMU Temporary VHD Install Target
port_label=/dev/disk/by-id/virtio-LAZARUS_STORAGE_TEST|QEMU Image Storage
port_label=/dev/disk/by-id/usb-QEMU_QEMU_HARDDISK_LAZ_RECOVERY_TEST-0:0|QEMU Removable Recovery Media
