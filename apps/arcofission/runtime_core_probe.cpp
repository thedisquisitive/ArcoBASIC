// Not a real program. Exists only so native_runtime_core_link_dependencies-style code in
// fission.cpp can read CMakeFiles/ArcoNativeRuntimeCoreProbe.dir/link.txt to discover exactly what
// the native Linux backend's own generic host-function bridge (arco_call_host,
// src/native/host_bridge.cpp) needs on its link line to use the lean arco_runtime_core (no GUI
// backend, no libcurl) -- the same trick ArcoFissionCapsuleCoreProbe already provides for
// arco_compiler_core, just for arco_runtime_core alone (no fission.cpp along for the ride). Never
// run.
#include "arco/runtime.hpp"

int main() {
    arco::Runtime runtime;
    (void)runtime.has_global("probe");
    return 0;
}
