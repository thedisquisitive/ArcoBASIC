# Transactional VRD Mapping Policy

## Scope delivered

Added `VMAPAddValidated` to the ArcoBASIC virtual-region policy. Requests are
now rejected before record insertion when they have zero length, unaligned
virtual/physical bases, non-page-sized lengths, arithmetic overflow,
non-canonical endpoints, or overlap with an active VRD record.

Retired records remain queryable but do not block a new mapping. The existing
`VMAPAdd` primitive remains available as the low-level record writer; new policy
callers should use the validated entry point.

## Tests

`systems_arco_basic_vrd_validation_smoke.sh` proves canonical frontend/A-MIR
representation. The virtual-region policy also builds as a freestanding UEFI
entry point.

## Remaining work

This is the validation half of the transaction. It does not yet materialize
page-table leaves, reserve physical backing, or roll back partial hierarchy
construction. Those operations must be integrated before VRD can be declared
the authoritative mapping policy.

