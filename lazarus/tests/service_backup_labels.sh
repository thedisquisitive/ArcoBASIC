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
: > "$image/completion-report.html"
printf '%s\n' '{"verified": true}' > "$image/verification.json"
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

# A repository symlink must not make an image outside the configured storage root visible.
outside="$root/outside/TCK-EVIL/Outside_Customer/2026-07-26_130000"
mkdir -p "$outside"
: > "$outside/disk.raw"
: > "$outside/hashes.dat"
: > "$outside/FINALIZED"
cp "$image/metadata.json" "$outside/metadata.json"
ln -s "$outside" "$root/images/escaped-image"

printf 'name=Label Test\nimage_storage=%s/images\n' "$root" > "$root/bench.profile"
printf '%s\n' '2026-07-26 13:15|Test Tech|verified|TCK-42|Example Customer' > "$root/activity.log"
response=$(printf '%s\n' '{"command":"backups"}' | "$service" --stdio --config "$root/bench.profile" --activity-log "$root/activity.log")
printf '%s\n' "$response" | grep -Fq 'TCK-42 | Example Customer | 2026-07-26'
if printf '%s\n' "$response" | grep -Fq 'escaped-image'; then
    echo "backup scan followed a repository symlink" >&2
    exit 1
fi
if printf '%s\n' "$response" | grep -Fq 'Example Customer | finalized'; then
    echo "backup title still exposes image state" >&2
    exit 1
fi

ticket_response=$(printf '%s\n' '{"command":"tickets"}' | "$service" --stdio --config "$root/bench.profile" --activity-log "$root/activity.log")
printf '%s\n' "$ticket_response" | grep -Fq 'TCK-42'
printf '%s\n' "$ticket_response" | grep -Fq 'Example Customer'
printf '%s\n' "$ticket_response" | grep -Fq 'Verified'
printf '%s\n' "$ticket_response" | grep -Fq 'Completion report'
printf '%s\n' "$ticket_response" | grep -Fq '2026-07-26 13:15'
