# Agent Packet 023 — ArcoBASIC ExitBootServices wrapper

Added `ExitBootServicesSafe(imageHandle, systemTable)` to the ArcoBASIC UEFI bootstrap module. It
acquires a map key, rejects acquisition failure, and immediately invokes `ExitBootServices` without
an intervening firmware call. The companion fixture proves two internal ArcoBASIC calls and PE32+
emission.

The fixture is intentionally not booted: after a successful handoff, UEFI boot services are no
longer valid and a post-handoff runtime/entry path must own the machine. A production boot path
still needs map-key retry handling and a next-stage memory manager before enabling this endpoint.
