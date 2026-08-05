# QEMU/OVMF Harness

Status: WP-010 (QEMU/OVMF Harness)
Depends on: `arcology-os/docs/systems/pe32-image.md`

`arcology-os/scripts/run/run-uefi-hello.sh` is a reusable, deterministic harness that boots a built ArcoBASIC UEFI
application under real QEMU + OVMF firmware and checks its console output for an expected string.
`arcology-os/tests/systems/systems_qemu_ovmf_harness_smoke.sh` wires it into the automated test suite for the
hello-world program specifically.

This formalizes the ad hoc boot check performed manually while implementing WP-009 (see
`arcology-os/docs/systems/pe32-image.md`) into the reusable tool Packet WP-010 asks for, usable both by the
test suite and directly by a developer.

## Usage

```sh
arcology-os/scripts/run/run-uefi-hello.sh EFI_FILE EXPECTED_OUTPUT [TIMEOUT_SECONDS]
```

```sh
$ arcofission build arcology-os/tests/fixtures/uefi-hello/hello.abas -o hello.efi --target uefi-x86_64
$ arcology-os/scripts/run/run-uefi-hello.sh hello.efi "Hello from ArcoBASIC"
PASS: Hello from ArcoBASIC
```

matching Packet WP-010's preferred result exactly.

## How It Works (Packet WP-010 "Required behavior")

- **Launches QEMU with OVMF**: `qemu-system-x86_64 -bios <OVMF.fd> ...`. The firmware image is
  located by searching `/usr/share/ovmf` and `/usr/share/OVMF` for `OVMF*.fd`, excluding
  Secure-Boot variants (`*secboot*`) so the harness always picks a firmware build that will run an
  unsigned application without extra configuration.
- **Exposes a FAT-formatted EFI system partition or equivalent test image**: a temporary directory
  laid out as `EFI/BOOT/BOOTX64.EFI` is served directly via QEMU's built-in `fat:rw:DIR` driver.
  Packet WP-010 explicitly accepts "a FAT-formatted EFI system partition **or equivalent test
  image**"; a virtual-FAT directory is exactly that, and needs no extra tooling (`mtools`,
  `mkfs.vfat`) beyond QEMU itself, keeping the harness dependency-free beyond QEMU and OVMF
  themselves.
- **Starts the generated EFI application**: naming the file `BOOTX64.EFI` under `EFI/BOOT/` is the
  standard UEFI removable-media default boot path, so OVMF's boot manager finds and starts it
  automatically with no interactive input or `startup.nsh` script required.
- **Captures serial output**: `-vga none -display none -serial stdio` forces OVMF's console onto
  the serial port (with a display device present, `ConOut` defaults to the graphics console
  instead, invisible to a headless harness) and connects it to the harness's own stdout, which is
  then captured via command substitution.
- **Enforces a timeout**: the entire QEMU invocation is wrapped in `timeout TIMEOUT_SECONDS`
  (default 20s), so a hung or misbehaving image cannot block the harness indefinitely.
- **Returns a nonzero status on failure**: exit `1` if QEMU ran but the expected string never
  appeared in the captured output (with the captured output printed to stderr for diagnosis); exit
  `2` for an environment problem (see below); exit `0` only when the expected string is found.

## Environment Policy (Packet WP-010)

> If OVMF is unavailable, document installation requirements and provide a detection error. Do not
> download firmware silently during tests.

`arcology-os/scripts/run/run-uefi-hello.sh` checks for `qemu-system-x86_64` on `PATH` and an `OVMF*.fd` file under
the standard install locations before doing anything else. Either missing tool produces a clear
`ERROR:` message on stderr naming the specific install command for Debian/Ubuntu, Fedora, and Arch
Linux, and exits `2` -- never a silent download or a confusing QEMU error about a missing `-bios`
argument.

`arcology-os/tests/systems/systems_qemu_ovmf_harness_smoke.sh` performs the same detection itself before calling the
harness, so that a missing tool produces a clean ctest `SKIP` (exit `0` with a `SKIP:` message)
rather than a `FAIL` -- consistent with the fact that not every environment running the test suite
will have QEMU/OVMF installed, and their absence is not a defect in ArcoBASIC.

## Verification

Ran on this development host (where `qemu-system-x86_64` and `/usr/share/ovmf/OVMF.fd` are both
present):

```text
$ arcology-os/scripts/run/run-uefi-hello.sh hello.efi "Hello from ArcoBASIC"
PASS: Hello from ArcoBASIC
```

`arcology-os/tests/systems/systems_qemu_ovmf_harness_smoke.sh` additionally proves the harness is not vacuously
passing: it also runs the harness against an expected string that is never printed, and asserts
that call fails (nonzero exit, `FAIL: expected output not found` on stderr) rather than silently
succeeding regardless of actual console content.

## What This Work Package Does Not Do

- Does not build a real FAT-formatted disk image file (via `mtools`/`mkfs.vfat`); the QEMU virtual
  FAT directory driver is used instead, which Packet WP-010 explicitly accepts as "equivalent."
  A future work package could add a real-image code path if a scenario specifically needs one
  (e.g. testing on a QEMU version or firmware without `fat:` driver support), but none does today.
- Does not exercise anything beyond the hello-world program; the harness script itself is generic
  (any `.efi` file and expected string), but only one automated test currently uses it.
- Does not capture graphical console output (only serial); not needed since the hello-world's only
  observable behavior is text written to `ConOut`.
