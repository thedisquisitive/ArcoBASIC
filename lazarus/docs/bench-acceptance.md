# Lazarus Bench Acceptance

Lazarus remains a secondary imaging system until every gate below passes on the exact appliance hardware, docks, adapters, and storage used at the bench.

## Customer-Service Guardrails

* Keep the established backup product as the primary image during the evaluation period.
* Use Lazarus only as the second image for customer disks.
* Restore only to disposable test drives until the restore matrix passes.
* Never resume an interrupted image unless Lazarus accepts the original source identity and captured prefix.
* A finalized image is not sufficient. Require a successful Lazarus verification report.
* A completed restore is not sufficient. Require full destination readback and partition-layout validation.
* BitLocker images remain raw-only until Lazarus can unlock and browse them with a recovery key.

## Required Test Matrix

1. Windows 10 UEFI/GPT with NTFS, EFI, MSR, and Recovery partitions.
2. Windows 11 UEFI/GPT with the same standard partition set.
3. Windows 10 legacy BIOS/MBR with a primary NTFS system partition.
4. BitLocker-protected Windows 10 and Windows 11 disks.
5. SATA HDD, SATA SSD, NVMe, USB-to-SATA, and USB-to-NVMe paths used at the bench.
6. Same-size restore and restore to a larger disposable drive.
7. Interrupted imaging followed by resume with the original source.
8. Attempted resume with a different same-size source; Lazarus must refuse.
9. Destination disconnect or replacement during pre-restore verification; Lazarus must refuse before writing.
10. Rescue image with injected read failures and a persisted bad-sector map.

## Acceptance Evidence

For every matrix entry, retain:

* Image metadata and source identity journal.
* Full hash-verification result.
* GPT or MBR validation result.
* NTFS MFT readability result where applicable.
* Bad-sector map, including an empty map for healthy sources.
* Full restore readback result.
* Restored partition-layout inspection.
* A successful Windows boot from the disposable restored drive.
* Manual recovery of representative files from the restored system.

## Release Gate

Customer restore must remain disabled operationally until the full matrix passes twice without unexplained warnings. NTFS directory browsing and file extraction from the image itself are still required before Lazarus can replace the primary backup product.
