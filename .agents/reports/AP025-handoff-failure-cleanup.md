# Agent Packet 025 — handoff failure cleanup and map validation

The ArcoBASIC bootstrap wrapper now frees its allocated map buffer on every pre-handoff retry or
validation failure. After a successful map acquisition it validates the descriptor size and total
map size before returning the key. Successful paths intentionally retain the buffer until the
handoff because the map key remains tied to the current boot-services state.

The exit-handoff fixture still builds as PE32+ and the UEFI binding smoke passes. A post-handoff
memory manager remains the next layer; this wrapper does not free the buffer after a successful
`ExitBootServices` call because boot services no longer exist at that point.
