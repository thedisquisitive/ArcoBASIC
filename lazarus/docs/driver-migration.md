# Driver Migration

Lazarus uses an add-first policy for storage-controller migration.

## Universal Restore Workflow

The technician-facing workflow does not expose INF paths or hardware IDs.
Lazarus performs these stages in order:

1. Select a verified backup and replacement disk.
2. Identify the PCI storage controller that owns the replacement disk.
3. Scan `DriverVault` beneath configured image storage.
4. Require a matching catalog-backed storage INF before erasing anything.
5. Restore and read-back verify the image.
6. Mount the restored Windows partition inside Lazarus OS.
7. Create a rollback copy of the offline SYSTEM hive and any replaced driver.
8. Copy the matched boot driver into `Windows/System32/drivers`.
9. Register the boot-start service and controller binding in the offline SYSTEM hive.
10. Flush and unmount the replacement disk before marking it safe to disconnect.
11. Preserve existing storage drivers as fallback boot paths.

The replacement disk must be connected through the controller it will use to
boot Windows. A USB adapter exposes the adapter or bench controller rather than
the replacement computer's internal VMD, RAID, SATA, or NVMe controller.

## Rules

* Never modify the source disk.
* Service only a destination-only restored clone.
* Match packages against replacement-hardware IDs.
* Accept extracted INF packages, not vendor EXE installers.
* Require the declared catalog to be present.
* Add every required storage package before considering removals.
* Preserve old AHCI, SATA, and storage packages by default.
* Do not remove existing storage packages during Universal Restore.
* Require the INF to resolve a boot-start kernel service and included SYS payload.
* Preserve the complete imported package on the restored Windows volume for audit and first-boot recovery.

Lazarus performs the bootstrap injection itself while Lazarus OS remains
running. The implementation follows the proven Linux `virt-v2v` model: place
the boot-critical SYS file where Windows can load it, register a boot-start
service, and add the controller descriptor to the offline SYSTEM hive. Windows
Plug and Play remains responsible for normal device enumeration on first boot.

This is deliberately narrower than a general-purpose Windows driver installer.
Universal Restore injects only the matched boot-storage driver required to
reach the Windows kernel. Chipset, network, display, and application drivers
remain first-boot or later maintenance work.

## Initial Controller Priorities

1. Intel VMD and RST
2. AMD RAID
3. Vendor NVMe controllers
4. Standard AHCI and NVMe fallback validation

Driver removal is not part of Universal Restore. A package associated with the
previous motherboard is not automatically unsafe on the replacement.
