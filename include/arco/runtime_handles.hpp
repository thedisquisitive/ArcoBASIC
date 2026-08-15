#pragma once

#include "arco/value.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace arco {

class RuntimeHandleTable {
public:
    template <typename T>
    RuntimeHandle create(std::string type, std::shared_ptr<T> object, bool destroyable = true) {
        for (std::uint32_t i = 0; i < entries_.size(); ++i) {
            if (!entries_[i].object) {
                entries_[i].object = std::move(object);
                entries_[i].type = type;
                entries_[i].destroyable = destroyable;
                ++entries_[i].generation;
                if (entries_[i].generation == 0) ++entries_[i].generation;
                return {i, entries_[i].generation, entries_[i].type};
            }
        }
        entries_.push_back(Entry{std::move(object), std::move(type), 1, destroyable});
        return {static_cast<std::uint32_t>(entries_.size() - 1), 1, entries_.back().type};
    }

    bool valid(const RuntimeHandle& handle, const std::string& expected_type = {}) const {
        if (handle.slot >= entries_.size()) return false;
        const auto& entry = entries_[handle.slot];
        return entry.object && entry.generation == handle.generation &&
               (expected_type.empty() ? entry.type == handle.type : entry.type == expected_type);
    }

    bool destroy(const RuntimeHandle& handle) {
        if (!valid(handle)) return false;
        if (!entries_[handle.slot].destroyable) return false;
        entries_[handle.slot].object.reset();
        return true;
    }

    bool destroyable(const RuntimeHandle& handle) const {
        return valid(handle) && entries_[handle.slot].destroyable;
    }

    std::shared_ptr<void> object(const RuntimeHandle& handle, const std::string& expected_type = {}) const {
        if (!valid(handle, expected_type)) return {};
        return entries_[handle.slot].object;
    }

private:
    struct Entry {
        std::shared_ptr<void> object;
        std::string type;
        std::uint32_t generation = 0;
        bool destroyable = true;
    };
    std::vector<Entry> entries_;
};

} // namespace arco
