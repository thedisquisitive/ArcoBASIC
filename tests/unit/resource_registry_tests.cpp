#include "arco/resource_registry.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    arco::RuntimeHandleTable handles;
    const auto handle = handles.create("SURFACE", std::make_shared<int>(1));
    arco::ResourceRegistry registry;
    require(registry.register_resource({handle, "SURFACE", "Execution Context", "Software Graphics Provider",
                                        arco::ResourceLifetime::Explicit, arco::ResourceLifecycle::Creating,
                                        {"Graphics Provider"}}), "registers a resource record");
    require(registry.lookup(handle) != nullptr, "looks up a resource by runtime handle");
    require(registry.transition(handle, arco::ResourceLifecycle::Ready), "accepts Creating to Ready transition");
    require(!registry.transition(handle, arco::ResourceLifecycle::Creating), "rejects invalid lifecycle transition");
    require(registry.add_dependency(handle, "Display Backend"), "adds directional dependency metadata");
    require(registry.describe().find("Software Graphics Provider") != std::string::npos, "diagnostic output exposes provider");
    require(registry.unregister_resource(handle), "unregisters a destroyed resource");
    require(registry.lookup(handle) == nullptr, "unregistered resource is no longer visible");
    require(!registry.unregister_resource(handle), "double-unregister is rejected");
    return 0;
}
