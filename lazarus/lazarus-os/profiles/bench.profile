# Arcology Lazarus OS default bench profile.
# Configure this with lazarus-gui or lazarus-tui before real imaging work.

name=Arcology Lazarus Appliance

# Default appliance branding. ACS Computer Services is an optional theme
# profile selectable from Admin Center or examples/acs-computer-services.profile.
branding_theme=Lazarus Default Theme
branding_product_name=Arcology Lazarus
branding_subtitle=Offline Imaging | Recovery | Hardware Migration
branding_accent=#f39a22
branding_background=#10161b
branding_surface=#171f25
branding_text=#edf1f3
branding_icon=#f39a22
branding_report_footer=Generated locally by Arcology Lazarus. SMART results describe reported device facts.

# Installed appliances may use this path when /var/lib/arcology-lazarus is on
# persistent storage. Live systems reject it when it resolves to RAM/overlay;
# use Administration > Image Storage before customer imaging.
image_storage=/var/lib/arcology-lazarus/images

# NAS image storage is configured from Administration > Image Storage. SMB
# passwords are kept separately in /var/lib/arcology-lazarus/nas-storage.auth
# with mode 0600 and are never written to this profile.
# nas_storage_protocol=smb
# nas_storage_server=nas.example.local
# nas_storage_share=Backups
# nas_storage_username=lazarus-backup
# nas_storage_domain=WORKGROUP

# Port roles and labels use normalized port: topology identities. Lazarus creates
# these from /dev/disk/by-path and keeps configured ports visible while empty.
#
# source=port:pci-0000:00:14.0-usb-0:3
# destination=port:pci-0000:00:14.0-usb-0:5
# image_storage_port=port:pci-0000:00:14.0-usb-0:6
# removable_media=port:pci-0000:00:14.0-usb-0:7
# ignored=/dev/disk/by-path/pci-0000:01:00.0-nvme-1
# port_label=port:pci-0000:00:14.0-usb-0:3|Left USB3 Source
