#pragma once

// The embedded ArcoBASIC VM boundary -- mirrors fissure::VM's shape exactly (same proven
// embedding pattern: one arco::Runtime member, register_host_contracts() registers every
// RIVET.* function, load_stdlib()/load_build_script() are repeated run_string() calls on the
// SAME instance so state persists across them -- already proven by fissure/core/vm/vm.cpp's own
// working VM::load_script).

#include "rivet/graph.hpp"

#include "arco/runtime.hpp"

#include <string>

namespace rivet {

class VM {
public:
    explicit VM(std::string project_root);

    // Loads rivet/stdlib/rivet.abas then rivet/adapters/toolchain/cxx.ab, in that order, from
    // `rivet_root` (the directory containing this Rivet installation's own stdlib/adapters/,
    // resolved by the CLI the same /proc/self/exe way Fissure's own main.cpp resolves its
    // adapters/ directory).
    void load_stdlib(const std::string& rivet_root);

    // Loads the project's own build.abas -- must be called AFTER load_stdlib().
    void load_build_script(const std::string& path);

    BuildGraph& graph() { return graph_; }
    const BuildGraph& graph() const { return graph_; }

private:
    void register_host_contracts();

    arco::Runtime runtime_;
    BuildGraph graph_;
    std::string project_root_;
};

} // namespace rivet
