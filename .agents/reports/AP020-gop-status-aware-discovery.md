# Agent Packet 020 — status-aware GOP discovery

`UEFI.GOP.Discover` now checks the `EFI_STATUS` returned by `LocateProtocol`. A nonzero status
branches around the output-pointer load and materializes a null `UEFI.GraphicsOutputProtocol`
value. Existing ArcoBASIC consumers already guard that value before dereferencing GOP metadata.

Added and verified the x86-64 `cmp rax, 0` encoder primitive. GOP, substrate, and runtime tests pass.
This closes the previous failure-status/null-output hazard without inventing a hosted fallback.
