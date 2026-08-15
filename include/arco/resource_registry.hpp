#pragma once

#include "arco/runtime_handles.hpp"

#include <string>
#include <vector>

namespace arco {

enum class ResourceLifecycle { Creating, Ready, Quiescing, Unavailable, Failed, Retired };
enum class ResourceLifetime { RuntimeOwned, Explicit, Borrowed, Shared, Persistent };

struct ResourceRecord {
    RuntimeHandle handle;
    std::string kind;
    std::string owner;
    std::string provider;
    ResourceLifetime lifetime = ResourceLifetime::Explicit;
    ResourceLifecycle state = ResourceLifecycle::Creating;
    std::vector<std::string> dependencies;
};

class ResourceRegistry {
public:
    bool register_resource(ResourceRecord record);
    const ResourceRecord* lookup(const RuntimeHandle& handle) const;
    ResourceRecord* lookup(const RuntimeHandle& handle);
    bool unregister_resource(const RuntimeHandle& handle);
    bool transition(const RuntimeHandle& handle, ResourceLifecycle next);
    bool add_dependency(const RuntimeHandle& handle, std::string dependency);
    std::vector<ResourceRecord> enumerate() const;
    std::string describe() const;

private:
    static bool valid_transition(ResourceLifecycle from, ResourceLifecycle to);
    std::vector<ResourceRecord> records_;
};

const char* resource_lifecycle_name(ResourceLifecycle state);
const char* resource_lifetime_name(ResourceLifetime lifetime);

} // namespace arco
