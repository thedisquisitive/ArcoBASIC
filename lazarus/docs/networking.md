# Network Configuration

Arcology Lazarus configures physical wired interfaces through the privileged
service. The GTK application never runs `ip` or `udhcpc` directly.

Administration > Network provides:

- Detected interface name, type, link state, carrier state, MAC address, and
  current IPv4 address.
- Automatic DHCP across detected wired interfaces.
- DHCP retry for a selected interface.
- Static IPv4 address, prefix length, gateway, and DNS configuration.
- Recent DHCP client events and factual configuration errors.

At boot, `lazarus-network` cold-loads drivers for detected PCI network
controllers, raises wired interfaces, and starts the configured address mode.
DHCP remains active to renew its lease. The Home network indicator polls the
service and updates when an address is assigned or removed.

Settings are stored in `/etc/arcology-lazarus/network.conf`. Installed systems
retain that file on the system disk. A live system also mirrors it to configured
Lazarus image storage when that storage is mounted.

Wireless configuration is intentionally unsupported in this release. The Admin
page reports wireless interfaces but will not apply settings to them.
