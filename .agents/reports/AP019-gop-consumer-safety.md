# Agent Packet 019 — GOP consumer safety guards

The freestanding GOP consumers now check the discovered protocol pointer and the current mode
pointer before reading framebuffer metadata. A missing protocol or mode reaches a deterministic
`CPU.HaltForever` path instead of dereferencing a null firmware pointer.

Updated fixtures:

- `gop-discovery.abas`
- `graphics-substrate.abas`
- `graphics-substrate-call.abas`

GOP discovery and ArcoBASIC substrate smoke tests pass. The compiler intrinsic still needs a future
status-aware typed wrapper for `LocateProtocol` and mode-info validation; these guards protect the
current source consumers without pretending that raw firmware status is already modeled.
