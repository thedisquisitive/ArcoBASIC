# Runtime Object Handles

Arcology resources are opaque runtime objects. A handle can be assigned, passed to an API, compared
for identity, or compared with a null value; it cannot be dereferenced, arithmetically manipulated,
or converted to a pointer. Libraries own creation/destruction and validate every handle at their
boundary.

The native implementation is `include/arco/runtime_handles.hpp`. It uses a slot and generation
token to reject stale handles after destruction. `Value` stores handles separately from numbers,
strings, arrays, and objects, so ordinary language operations cannot accidentally reinterpret a
resource. Graphics surfaces and deterministic pseudorandom generators use this model. Explicit
`RANDOM` handles share generator state when copied, reject stale or wrong-type use, and are consumed
by `Random.Destroy`; the runtime-owned default generator is not exposed as a destroyable handle.

See `arcology-os/rfcs/RFC-0015_Runtime_Object_Handles.md` for the language/runtime contract.
