# Substrate Resource Model — Status

## Delivered

- Independent `ResourceRegistry` and `ResourceRecord` types.
- Resource kind, owner, provider, lifetime, lifecycle state, and dependency metadata.
- Lookup, enumeration, diagnostic description, dependency registration, and unregister operations.
- Validated lifecycle transitions across Creating, Ready, Quiescing, Unavailable, Failed, and Retired.
- Graphics memory surfaces register as explicit resources and unregister on destruction.
- Primary surfaces register as runtime-owned resources and remain registered when destroy is rejected.
- `RESOURCE.Describe()` provides substrate diagnostics without exposing implementation payloads.
- Dedicated registry unit test and end-to-end graphics-handle/resource smoke fixture.

## Backend boundary

The hosted provider is `Software Graphics Provider`. A future UEFI integration will register the
same surface record with `UEFI GOP`/display-backend provider metadata; application APIs do not change.

## Validation

- `resource_registry_tests` passes.
- `systems_runtime_handle_abi_smoke` passes, including provider/dependency diagnostics.
- Runtime handle, GOP, memory, and port-I/O focused tests remain green.
