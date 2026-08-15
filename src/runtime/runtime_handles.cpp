#include "arco/runtime_handles.hpp"

#include <stdexcept>

namespace arco {

Value::Value(RuntimeHandle handle) : data_(std::make_shared<RuntimeHandle>(std::move(handle))) {}

const RuntimeHandle& Value::as_handle() const {
    if (!is_handle()) throw std::runtime_error("value is not a runtime object handle");
    return *std::get<HandlePtr>(data_);
}

} // namespace arco
