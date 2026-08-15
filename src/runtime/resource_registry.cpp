#include "arco/resource_registry.hpp"

#include <sstream>

namespace arco {

const char* resource_lifecycle_name(ResourceLifecycle state) {
    switch (state) {
    case ResourceLifecycle::Creating: return "Creating";
    case ResourceLifecycle::Ready: return "Ready";
    case ResourceLifecycle::Quiescing: return "Quiescing";
    case ResourceLifecycle::Unavailable: return "Unavailable";
    case ResourceLifecycle::Failed: return "Failed";
    case ResourceLifecycle::Retired: return "Retired";
    }
    return "Unknown";
}

const char* resource_lifetime_name(ResourceLifetime lifetime) {
    switch (lifetime) {
    case ResourceLifetime::RuntimeOwned: return "RuntimeOwned";
    case ResourceLifetime::Explicit: return "Explicit";
    case ResourceLifetime::Borrowed: return "Borrowed";
    case ResourceLifetime::Shared: return "Shared";
    case ResourceLifetime::Persistent: return "Persistent";
    }
    return "Unknown";
}

bool ResourceRegistry::register_resource(ResourceRecord record) {
    if (record.handle.type.empty() || record.kind.empty() || record.owner.empty()) return false;
    if (lookup(record.handle) != nullptr) return false;
    records_.push_back(std::move(record));
    return true;
}

const ResourceRecord* ResourceRegistry::lookup(const RuntimeHandle& handle) const {
    for (const auto& record : records_) if (record.handle == handle) return &record;
    return nullptr;
}

ResourceRecord* ResourceRegistry::lookup(const RuntimeHandle& handle) {
    for (auto& record : records_) if (record.handle == handle) return &record;
    return nullptr;
}

bool ResourceRegistry::unregister_resource(const RuntimeHandle& handle) {
    for (auto it = records_.begin(); it != records_.end(); ++it) {
        if (it->handle == handle) {
            records_.erase(it);
            return true;
        }
    }
    return false;
}

bool ResourceRegistry::valid_transition(ResourceLifecycle from, ResourceLifecycle to) {
    if (from == ResourceLifecycle::Retired) return false;
    if (to == ResourceLifecycle::Retired) return true;
    if (to == ResourceLifecycle::Failed) return from != ResourceLifecycle::Failed;
    if (from == ResourceLifecycle::Creating) return to == ResourceLifecycle::Ready;
    if (from == ResourceLifecycle::Ready) return to == ResourceLifecycle::Quiescing || to == ResourceLifecycle::Unavailable;
    if (from == ResourceLifecycle::Quiescing) return to == ResourceLifecycle::Ready || to == ResourceLifecycle::Unavailable;
    if (from == ResourceLifecycle::Unavailable) return to == ResourceLifecycle::Ready;
    return false;
}

bool ResourceRegistry::transition(const RuntimeHandle& handle, ResourceLifecycle next) {
    auto* record = lookup(handle);
    if (record == nullptr || !valid_transition(record->state, next)) return false;
    record->state = next;
    return true;
}

bool ResourceRegistry::add_dependency(const RuntimeHandle& handle, std::string dependency) {
    auto* record = lookup(handle);
    if (record == nullptr || dependency.empty()) return false;
    for (const auto& existing : record->dependencies) if (existing == dependency) return true;
    record->dependencies.push_back(std::move(dependency));
    return true;
}

std::vector<ResourceRecord> ResourceRegistry::enumerate() const { return records_; }

std::string ResourceRegistry::describe() const {
    std::ostringstream output;
    for (const auto& record : records_) {
        output << record.kind << " #" << record.handle.slot << "\n"
               << "Owner: " << record.owner << "\n"
               << "Provider: " << (record.provider.empty() ? "(none)" : record.provider) << "\n"
               << "State: " << resource_lifecycle_name(record.state) << "\n"
               << "Lifetime: " << resource_lifetime_name(record.lifetime) << "\n"
               << "Dependencies:";
        if (record.dependencies.empty()) output << " (none)";
        for (const auto& dependency : record.dependencies) output << ' ' << dependency;
        output << "\n";
    }
    return output.str();
}

} // namespace arco
