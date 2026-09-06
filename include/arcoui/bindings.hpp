#pragma once

// Registers the ArcoUI.* host functions onto an arco::Runtime. This is the only place the ArcoUI
// core (arcoui::Runtime, arcoui/core.hpp) is bridged to arco::Value -- see the dependency note at
// the top of arcoui/types.hpp.
//
// Called once from arco::Runtime::Runtime() (src/runtime/runtime.cpp), the same place GUI.* is
// registered, so ArcoUI.* is present in every build target (arco_runtime and arco_runtime_core
// alike) per the "universal" host-function convention (see commit 18383a72d0).

namespace arco {
class Runtime;
}

namespace arcoui {

void register_arcoui_functions(arco::Runtime& runtime);

} // namespace arcoui
