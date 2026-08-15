# RFC-0019: Physical Region Database and Stateful Memory Management

Status: Draft

RFC-0019 defines the substrate-owned Physical Region Database (PRD) created exactly once from the
validated UEFI map. Firmware descriptors become immutable bootstrap history; all later allocation,
reservation, release, splitting, and coalescing decisions use PRD records only.

Each record contains a page-aligned base, page count, region type/state, ownership, provider,
reservation reason, attributes, and allocation flags. Reserved regions include the Arcology image,
bootstrap and allocator metadata, framebuffer, runtime firmware, ACPI, MMIO, and initial page
tables. Metadata itself is permanently reserved and cannot overlap an allocatable region.

ArcoBASIC owns allocator policy through `MEMORY.Initialize`, `AllocatePages`, `ReleasePages`,
`Reserve`, `IsReserved`, `DescribeRegion`, `DescribeAllocator`, and `RegionCount`. Operations must
check alignment, overflow, overlap, ownership, double release, splitting, and adjacent-free-region
coalescing deterministically. The compiler supplies mechanisms only; it does not contain allocator
policy. The initial implementation phase provides source-level region invariants and translation
helpers before persistent storage and the stateful registry are enabled.
