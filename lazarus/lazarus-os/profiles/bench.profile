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

# Local fallback storage. Use Administration > Image Storage to assign a
# persistent storage device before customer imaging.
image_storage=/var/lib/arcology-lazarus/images

# Port roles and labels use normalized port: topology identities. Lazarus creates
# these from /dev/disk/by-path and keeps configured ports visible while empty.
#
# source=port:pci-0000:00:14.0-usb-0:3
# destination=port:pci-0000:00:14.0-usb-0:5
# image_storage_port=port:pci-0000:00:14.0-usb-0:6
# removable_media=port:pci-0000:00:14.0-usb-0:7
# ignored=/dev/disk/by-path/pci-0000:01:00.0-nvme-1
# port_label=port:pci-0000:00:14.0-usb-0:3|Left USB3 Source
