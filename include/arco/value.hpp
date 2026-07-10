#pragma once

#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace arco {

class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;
    using ArrayPtr = std::shared_ptr<Array>;
    using ObjectPtr = std::shared_ptr<Object>;
    using Storage = std::variant<std::monostate, bool, double, std::string, ArrayPtr, ObjectPtr>;

    Value() = default;
    Value(std::nullptr_t) : data_(std::monostate{}) {}
    Value(bool value) : data_(value) {}
    Value(int value) : data_(static_cast<double>(value)) {}
    Value(double value) : data_(value) {}
    Value(const char* value) : data_(std::string(value)) {}
    Value(std::string value) : data_(std::move(value)) {}
    Value(Array value) : data_(std::make_shared<Array>(std::move(value))) {}
    Value(Object value) : data_(std::make_shared<Object>(std::move(value))) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(data_); }
    bool is_bool() const { return std::holds_alternative<bool>(data_); }
    bool is_number() const { return std::holds_alternative<double>(data_); }
    bool is_string() const { return std::holds_alternative<std::string>(data_); }
    bool is_array() const { return std::holds_alternative<ArrayPtr>(data_); }
    bool is_object() const { return std::holds_alternative<ObjectPtr>(data_); }

    double as_number() const {
        if (is_number()) {
            return std::get<double>(data_);
        }
        if (is_bool()) {
            return std::get<bool>(data_) ? 1.0 : 0.0;
        }
        throw std::runtime_error("value is not a number");
    }

    bool truthy() const {
        if (is_null()) {
            return false;
        }
        if (is_bool()) {
            return std::get<bool>(data_);
        }
        if (is_number()) {
            return std::get<double>(data_) != 0.0;
        }
        if (is_string()) {
            return !std::get<std::string>(data_).empty();
        }
        if (is_array()) {
            return !as_array().empty();
        }
        return !as_object().empty();
    }

    const Array& as_array() const {
        if (!is_array()) {
            throw std::runtime_error("value is not an array");
        }
        return *std::get<ArrayPtr>(data_);
    }

    Array& as_array() {
        if (!is_array()) {
            throw std::runtime_error("value is not an array");
        }
        return *std::get<ArrayPtr>(data_);
    }

    const Object& as_object() const {
        if (!is_object()) {
            throw std::runtime_error("value is not an object");
        }
        return *std::get<ObjectPtr>(data_);
    }

    Object& as_object() {
        if (!is_object()) {
            throw std::runtime_error("value is not an object");
        }
        return *std::get<ObjectPtr>(data_);
    }

    Value get_property(const std::string& name) const {
        const auto& object = as_object();
        const auto found = object.find(name);
        if (found == object.end()) {
            throw std::runtime_error("undefined property: " + name);
        }
        return found->second;
    }

    void set_property(const std::string& name, Value value) {
        as_object()[name] = std::move(value);
    }

    std::string to_string() const {
        if (is_null()) {
            return "NULL";
        }
        if (is_bool()) {
            return std::get<bool>(data_) ? "TRUE" : "FALSE";
        }
        if (is_number()) {
            std::ostringstream out;
            const double value = std::get<double>(data_);
            if (std::floor(value) == value) {
                out << static_cast<long long>(value);
            } else {
                out << value;
            }
            return out.str();
        }
        if (is_string()) {
            return std::get<std::string>(data_);
        }
        if (is_array()) {
            std::ostringstream out;
            out << "[";
            const auto& array = as_array();
            for (std::size_t i = 0; i < array.size(); ++i) {
                if (i != 0) {
                    out << ", ";
                }
                out << array[i].to_string();
            }
            out << "]";
            return out.str();
        }
        const auto& object = as_object();
        const auto output = object.find("Output");
        if (output != object.end() && output->second.is_string()) {
            return output->second.to_string();
        }
        std::ostringstream out;
        out << "{";
        bool first = true;
        for (const auto& [key, value] : object) {
            if (!first) {
                out << ", ";
            }
            first = false;
            out << key << ": " << value.to_string();
        }
        out << "}";
        return out.str();
    }

    const Storage& storage() const { return data_; }

private:
    Storage data_;
};

inline bool values_equal(const Value& left, const Value& right) {
    if (left.is_number() && right.is_number()) {
        return left.as_number() == right.as_number();
    }
    if (left.is_bool() || right.is_bool()) {
        return left.truthy() == right.truthy();
    }
    return left.to_string() == right.to_string();
}

} // namespace arco
