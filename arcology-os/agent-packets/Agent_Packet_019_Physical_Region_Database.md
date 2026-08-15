# Agent Packet 019 — Physical Region Database and Stateful Memory Management

Implement RFC-0019 in phases: source-level region invariants and translation, persistent PRD
records/reservations, stateful split/allocate/release/coalesce operations, protected metadata, and
diagnostics. Preserve RFC-0018 handoff sequencing and keep all allocator policy in ArcoBASIC.

Completion requires descriptor translation exactly once, no firmware lookups after initialization,
framebuffer/runtime/ACPI/MMIO reservations, deterministic allocation/release behavior, and complete
systems/reference documentation.
