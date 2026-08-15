# Agent Packet 018 — Firmware Transition and Memory Ownership

Implement RFC-0018 in phases: validate and translate firmware descriptors in ArcoBASIC, reserve
protected regions, complete final `GetMemoryMap`/`ExitBootServices` sequencing, and replace the
prototype region helpers with a persistent substrate-owned page allocator. Compiler work is limited
to mechanisms; allocator policy and diagnostics remain ArcoBASIC source.

Definition of done includes descriptor overflow/alignment tests, reservation and overlap tests,
allocation/release/coalescing tests, protected metadata tests, framebuffer preservation, and a
post-handoff execution proof.
