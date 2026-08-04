# Lazarus Bench Acceptance

Lazarus remains a secondary imaging system until every gate below passes on the exact appliance hardware, docks, adapters, and storage used at the bench.

## Customer-Service Guardrails

* Keep the established backup product as the primary image during the evaluation period.
* Use Lazarus only as the second image for customer disks.
* Restore only to disposable test drives until the restore matrix passes.
* Never resume an interrupted image unless Lazarus accepts the original source identity and captured prefix.
* A finalized image is not sufficient. Require a successful Lazarus verification report.
* A completed restore is not sufficient. Require full destination readback. For a healthy source image, also require partition-layout validation. A byte-matched restore of a known-corrupt source layout is evidence preservation only and must remain an escalated recovery case, not a deliverable boot disk.
* BitLocker images can be unlocked and browsed for file recovery from Recover Files using the recovery key.

## Required Test Matrix

1. Windows 10 UEFI/GPT with NTFS, EFI, MSR, and Recovery partitions.
2. Windows 11 UEFI/GPT with the same standard partition set.
3. Windows 10 legacy BIOS/MBR with a primary NTFS system partition.
4. BitLocker-protected Windows 10 and Windows 11 disks; Recover Files must unlock and browse the volume with the correct recovery key and clearly reject browsing without one.
5. SATA HDD, SATA SSD, NVMe, USB-to-SATA, and USB-to-NVMe paths used at the bench.
6. Same-size restore and restore to a larger disposable drive.
7. Interrupted imaging followed by resume with the original source.
8. Attempted resume with a different same-size source; Lazarus must refuse.
9. Destination disconnect or replacement during pre-restore verification; Lazarus must refuse before writing.
10. Rescue image with injected read failures and a persisted bad-sector map.
11. Corrupt GPT usable-LBA range; Lazarus must create and hash-verify the complete raw image, retain the layout warning, and generate a specialist escalation report without writing to the source.
12. SMB NAS with the production hostname, account, permissions, and network path; complete image, verify, ticket review, recover, and restore-to-disposable-disk workflows.
13. NFS NAS when used by the bench; repeat the same workflow and confirm the export cannot be mounted read-only by mistake.
14. NAS interruption during imaging followed by repository reconnection and job resume. Lazarus must retain the incomplete job and must not redirect output into the appliance root filesystem.
15. Drive Analysis against confirmed Windows and Linux installations, a Windows-like layout without readable installation data, a BitLocker volume, and an unknown or corrupt disk. Confirm that confidence labels remain factual, every temporary analysis mount is released, the source remains byte-identical, and raw imaging remains offered for the unknown or corrupt case.
16. Create Backup source preflight: select each analysis fixture, continue entering ticket fields while analysis runs, and confirm the image action unlocks afterward. A healthy source must show its factual summary; damaged, failed-health, and analysis-unavailable cases must offer Rescue Backup; replacing a drive on the same port during analysis must invalidate the selection rather than display the old result.

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
