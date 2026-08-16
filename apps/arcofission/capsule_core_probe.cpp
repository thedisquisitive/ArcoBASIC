// Not a real program. This exists only so native_link_dependencies-style code in fission.cpp can
// read CMakeFiles/ArcoFissionCapsuleCoreProbe.dir/link.txt to discover exactly what a Linux
// capsule linked against the lean arco_compiler_core/arco_runtime_core (no GUI backend, no
// libcurl) needs on its link line -- the same trick the full ArcoFission target's own link.txt
// already provides for GUI/network-capable capsules. Never run.
#include "arco/fission.hpp"

int main() {
    const std::string bytecode;
    (void)arco::fission::run_bytecode_binary(bytecode);
    return 0;
}
