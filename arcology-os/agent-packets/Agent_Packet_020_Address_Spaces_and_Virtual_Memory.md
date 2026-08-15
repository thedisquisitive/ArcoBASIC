# Agent Packet 020 — Address Spaces and Virtual Memory

Implement RFC-0020 in phases: virtual-region records and overlap/protection invariants, address
space handle/lifecycle integration, PRD-backed mapping allocation, page-table ownership, and CR3/
INVLPG hardware mechanisms. Mapping policy remains ArcoBASIC; compiler changes remain mechanisms
only. Validate map/unmap/protect, framebuffer/MMIO mappings, PRD reservations, diagnostics, and
post-transition regressions.
