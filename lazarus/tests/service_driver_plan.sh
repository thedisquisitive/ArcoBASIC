#!/bin/sh
set -eu

service=${1:?service binary required}
root=$(mktemp -d "${TMPDIR:-/tmp}/lazarus-driver-plan.XXXXXX")
trap 'rm -rf "$root"' EXIT INT TERM

mkdir -p "$root/storage/DriverVault/VMD"
cat >"$root/storage/DriverVault/VMD/vmd.inf" <<'EOF'
[Version]
Signature="$Windows NT$"
Class=SCSIAdapter
ClassGuid={4D36E97B-E325-11CE-BFC1-08002BE10318}
Provider=%Vendor%
DriverVer=07/01/2026,20.1.0.1000
CatalogFile=vmd.cat
[Manufacturer]
%Vendor%=Models,NTamd64
[Models.NTamd64]
%Device%=Install, PCI\VEN_8086&DEV_7D0B
[Install.NTamd64.Services]
AddService=iaStorVD,0x00000002,VmdService
[VmdService]
ServiceType=1
StartType=0
ErrorControl=1
LoadOrderGroup=SCSI Miniport
ServiceBinary=%12%\iaStorVD.sys
EOF
printf 'catalog fixture\n' >"$root/storage/DriverVault/VMD/vmd.cat"
printf 'sys fixture\n' >"$root/storage/DriverVault/VMD/iaStorVD.sys"
cat >"$root/bench.profile" <<EOF
name=Driver Plan Test
image_storage=$root/storage
source=/test/source
destination=/test/destination
EOF

request=$(printf '{"command":"driver_plan","package_paths_text":"%s","hardware_ids_text":"PCI\\\\VEN_8086&DEV_7D0B&SUBSYS_12345678","removals_text":"","removal_risk_acknowledged":false}\n' \
    "$root/storage/DriverVault/VMD")
response=$(printf '%s\n' "$request" | "$service" --stdio --config "$root/bench.profile" | tail -n 1)
printf '%s\n' "$response" | grep -q '"ok":true'
printf '%s\n' "$response" | grep -q '"matching_storage_driver_found":true'
printf '%s\n' "$response" | grep -q '"action":"add"'
printf '%s\n' "$response" | grep -q '"requires_windows_pe":false'

unconfirmed=$(printf '{"command":"driver_apply_offline","package_paths_text":"%s","hardware_ids_text":"PCI\\\\VEN_8086&DEV_7D0B","destination_selector":"/test/destination","confirmation":"NO"}\n' \
    "$root/storage/DriverVault/VMD" | "$service" --stdio --config "$root/bench.profile" | tail -n 1)
printf '%s\n' "$unconfirmed" | grep -q '"ok":false'
printf '%s\n' "$unconfirmed" | grep -q 'Type APPLY UNIVERSAL RESTORE'

blocked=$(printf '{"command":"driver_plan","package_paths_text":"%s","hardware_ids_text":"PCI\\\\VEN_8086&DEV_7D0B","removals_text":"oem42.inf","removal_risk_acknowledged":false}\n' \
    "$root/storage/DriverVault/VMD" | "$service" --stdio --config "$root/bench.profile" | tail -n 1)
printf '%s\n' "$blocked" | grep -q '"ok":false'
printf '%s\n' "$blocked" | grep -q 'migration.removal_not_acknowledged'

automatic_without_destination=$(printf '%s\n' \
    '{"command":"driver_plan","automatic":true,"destination_selector":"/test/destination"}' \
    | "$service" --stdio --config "$root/bench.profile" | tail -n 1)
printf '%s\n' "$automatic_without_destination" | grep -q '"ok":false'
printf '%s\n' "$automatic_without_destination" | grep -q \
    'Universal Restore requires a connected destination-only or unassigned replacement disk'

echo "driver migration planning passed"
