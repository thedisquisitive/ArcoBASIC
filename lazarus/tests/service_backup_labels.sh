#!/bin/sh
set -eu

service=$1
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

image="$root/images/TCK-42/Example_Customer/2026-07-26_120000"
mkdir -p "$image"
: > "$image/disk.raw"
: > "$image/hashes.dat"
: > "$image/FINALIZED"
cat > "$image/metadata.json" <<'EOF'
{
  "format": "laz-dir",
  "image_state": "finalized",
  "job": {
    "ticket_number": "TCK-42",
    "customer_name": "Example Customer",
    "technician": "Test Tech",
    "purpose": "Regression Test"
  }
}
EOF

printf 'name=Label Test\nimage_storage=%s/images\n' "$root" > "$root/bench.profile"
response=$(printf '%s\n' '{"command":"backups"}' | "$service" --stdio --config "$root/bench.profile")
printf '%s\n' "$response" | grep -Fq 'TCK-42 | Example Customer | 2026-07-26'
if printf '%s\n' "$response" | grep -Fq 'Example Customer | finalized'; then
    echo "backup title still exposes image state" >&2
    exit 1
fi
