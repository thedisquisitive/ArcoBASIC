#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace arco {

// Opaque runtime-managed object identity. Source code cannot inspect this value; the fields are
// retained here only so Value can implement identity/equality without depending on a backend.
struct RuntimeHandle {
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;
    std::string type;
    bool operator==(const RuntimeHandle& other) const {
        return slot == other.slot && generation == other.generation && type == other.type;
    }
};

struct BitVector {
    std::vector<std::uint64_t> words;
    std::size_t length = 0;

    static BitVector from_string(const std::string& text) {
        BitVector result;
        result.length = text.size();
        result.words.assign((result.length + 63) / 64, 0);
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '0' && text[i] != '1') throw std::runtime_error("bit vector text must contain only 0 or 1");
            if (text[i] == '1') result.words[i / 64] |= std::uint64_t{1} << (i % 64);
        }
        return result;
    }

    bool get(std::size_t index) const {
        if (index >= length) throw std::runtime_error("bit vector index out of range");
        return (words[index / 64] >> (index % 64)) & 1U;
    }

    std::string string() const {
        std::string result(length, '0');
        for (std::size_t i = 0; i < length; ++i) if (get(i)) result[i] = '1';
        return result;
    }

    bool operator==(const BitVector& other) const {
        return length == other.length && words == other.words;
    }
};

struct RangeValue {
    long long start = 0;
    long long stop = 0;
    long long step = 1;
    std::size_t length = 0;

    bool contains(long long value) const {
        if (length == 0) return false;
        if (step > 0) {
            if (value < start || value >= stop) return false;
        } else if (value > start || value <= stop) {
            return false;
        }
        return (value - start) % step == 0;
    }

    long long at(std::size_t index) const {
        if (index >= length) throw std::runtime_error("range index out of range");
        return start + static_cast<long long>(index) * step;
    }

    bool operator==(const RangeValue& other) const {
        return start == other.start && stop == other.stop && step == other.step && length == other.length;
    }
};

inline std::size_t range_length(long long start, long long stop, long long step) {
    if (step == 0) throw std::runtime_error("Range step cannot be zero");
    if (step > 0 && start >= stop) return 0;
    if (step < 0 && start <= stop) return 0;
    const unsigned long long distance = step > 0
        ? static_cast<unsigned long long>(stop - start)
        : static_cast<unsigned long long>(start - stop);
    const unsigned long long stride = static_cast<unsigned long long>(step > 0 ? step : -step);
    const unsigned long long length = (distance + stride - 1) / stride;
    if (length > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("Range length is too large");
    }
    return static_cast<std::size_t>(length);
}

class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;
    using ArrayPtr = std::shared_ptr<Array>;
    using TuplePtr = std::shared_ptr<const Array>;
    using ObjectPtr = std::shared_ptr<Object>;
    using HandlePtr = std::shared_ptr<RuntimeHandle>;
    using BitVectorPtr = std::shared_ptr<const BitVector>;
    using RangePtr = std::shared_ptr<const RangeValue>;
    using Storage = std::variant<std::monostate, bool, double, std::string, ArrayPtr, ObjectPtr, HandlePtr, BitVectorPtr, TuplePtr, RangePtr>;

    Value() = default;
    Value(std::nullptr_t) : data_(std::monostate{}) {}
    Value(bool value) : data_(value) {}
    Value(int value) : data_(static_cast<double>(value)) {}
    Value(double value) : data_(value) {}
    Value(const char* value) : data_(std::string(value)) {}
    Value(std::string value) : data_(std::move(value)) {}
    Value(Array value) : data_(std::make_shared<Array>(std::move(value))) {}
    Value(Object value) : data_(std::make_shared<Object>(std::move(value))) {}
    explicit Value(RuntimeHandle handle);
    explicit Value(BitVector value) : data_(std::make_shared<const BitVector>(std::move(value))) {}
    explicit Value(RangeValue value) : data_(std::make_shared<const RangeValue>(std::move(value))) {}
    static Value tuple(Array values) {
        Value result;
        result.data_ = std::make_shared<const Array>(std::move(values));
        return result;
    }

    bool is_null() const { return std::holds_alternative<std::monostate>(data_); }
    bool is_bool() const { return std::holds_alternative<bool>(data_); }
    bool is_number() const { return std::holds_alternative<double>(data_); }
    bool is_string() const { return std::holds_alternative<std::string>(data_); }
    bool is_array() const { return std::holds_alternative<ArrayPtr>(data_); }
    bool is_object() const { return std::holds_alternative<ObjectPtr>(data_); }
    bool is_handle() const { return std::holds_alternative<HandlePtr>(data_); }
    bool is_bit_vector() const { return std::holds_alternative<BitVectorPtr>(data_); }
    bool is_tuple() const { return std::holds_alternative<TuplePtr>(data_); }
    bool is_range() const { return std::holds_alternative<RangePtr>(data_); }
    const RuntimeHandle& as_handle() const;
    const BitVector& as_bit_vector() const {
        if (!is_bit_vector()) throw std::runtime_error("value is not a bit vector");
        return *std::get<BitVectorPtr>(data_);
    }
    const Array& as_tuple() const {
        if (!is_tuple()) throw std::runtime_error("value is not a tuple");
        return *std::get<TuplePtr>(data_);
    }
    const RangeValue& as_range() const {
        if (!is_range()) throw std::runtime_error("value is not a range");
        return *std::get<RangePtr>(data_);
    }

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
        if (is_handle()) {
            return true;
        }
        if (is_bit_vector()) return as_bit_vector().length != 0;
        if (is_tuple()) return !as_tuple().empty();
        if (is_range()) return as_range().length != 0;
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
        if (is_handle()) {
            return "<" + as_handle().type + ">";
        }
        if (is_bit_vector()) return as_bit_vector().string();
        if (is_tuple()) {
            std::ostringstream out;
            out << "(";
            const auto& tuple = as_tuple();
            for (std::size_t i = 0; i < tuple.size(); ++i) {
                if (i) out << ", ";
                out << tuple[i].to_string();
            }
            if (tuple.size() == 1) out << ',';
            out << ")";
            return out.str();
        }
        if (is_range()) {
            const auto& range = as_range();
            std::ostringstream out;
            out << "Range(" << range.start << ", " << range.stop;
            if (range.step != 1) out << ", " << range.step;
            out << ")";
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
    if (left.is_handle() || right.is_handle()) {
        return left.is_handle() && right.is_handle() && left.as_handle() == right.as_handle();
    }
    if (left.is_bit_vector() || right.is_bit_vector()) {
        return left.is_bit_vector() && right.is_bit_vector() && left.as_bit_vector() == right.as_bit_vector();
    }
    if (left.is_tuple() || right.is_tuple()) {
        if (!left.is_tuple() || !right.is_tuple() || left.as_tuple().size() != right.as_tuple().size()) return false;
        for (std::size_t i = 0; i < left.as_tuple().size(); ++i) {
            if (!values_equal(left.as_tuple()[i], right.as_tuple()[i])) return false;
        }
        return true;
    }
    if (left.is_range() || right.is_range()) {
        return left.is_range() && right.is_range() && left.as_range() == right.as_range();
    }
    return left.to_string() == right.to_string();
}

inline long long exact_slice_integer(double number, const std::string& role) {
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<long long>::min()) ||
        number > static_cast<double>(std::numeric_limits<long long>::max())) {
        throw std::runtime_error("slice " + role + " must be an integral value");
    }
    return static_cast<long long>(number);
}

struct NormalizedSlice {
    long long start = 0;
    long long end = 0;
    long long step = 1;
};

inline NormalizedSlice normalize_slice(std::size_t size, std::optional<long long> requested_start,
                                       std::optional<long long> requested_end, long long step) {
    if (step == 0) throw std::runtime_error("slice step cannot be zero");
    const long long length = size > static_cast<std::size_t>(std::numeric_limits<long long>::max())
        ? throw std::runtime_error("collection is too large to slice")
        : static_cast<long long>(size);
    NormalizedSlice result;
    result.step = step;
    if (step > 0) {
        auto bound = [length](long long value) {
            if (value < 0) value = std::max(-length, value) + length;
            return std::max(0LL, std::min(length, value));
        };
        result.start = requested_start ? bound(*requested_start) : 0;
        result.end = requested_end ? bound(*requested_end) : length;
    } else {
        auto bound = [length](long long value) {
            if (value < 0) value += length;
            return std::max(-1LL, std::min(length - 1, value));
        };
        result.start = requested_start ? bound(*requested_start) : length - 1;
        result.end = requested_end ? bound(*requested_end) : -1;
    }
    return result;
}

inline std::vector<std::string> utf8_codepoints(const std::string& text) {
    std::vector<std::string> points;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t width = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 : (lead & 0xf0) == 0xe0 ? 3 : 4;
        width = std::min(width, text.size() - i);
        points.push_back(text.substr(i, width));
        i += width;
    }
    return points;
}

inline Value slice_value(const Value& value, std::optional<long long> start, std::optional<long long> end,
                         long long step = 1) {
    std::size_t size = 0;
    if (value.is_array()) size = value.as_array().size();
    else if (value.is_tuple()) size = value.as_tuple().size();
    else if (value.is_string()) size = utf8_codepoints(value.to_string()).size();
    else if (value.is_bit_vector()) size = value.as_bit_vector().length;
    else throw std::runtime_error("slice target must be an array, string, tuple, or BITVECTOR");
    const NormalizedSlice slice = normalize_slice(size, start, end, step);

    if (value.is_array()) {
        Value::Array output;
        for (long long i = slice.start; slice.step > 0 ? i < slice.end : i > slice.end; i += slice.step) {
            output.push_back(value.as_array()[static_cast<std::size_t>(i)]);
        }
        return output;
    }
    if (value.is_tuple()) {
        Value::Array output;
        for (long long i = slice.start; slice.step > 0 ? i < slice.end : i > slice.end; i += slice.step) {
            output.push_back(value.as_tuple()[static_cast<std::size_t>(i)]);
        }
        return Value::tuple(std::move(output));
    }
    if (value.is_string()) {
        const auto points = utf8_codepoints(value.to_string());
        std::string output;
        for (long long i = slice.start; slice.step > 0 ? i < slice.end : i > slice.end; i += slice.step) {
            output += points[static_cast<std::size_t>(i)];
        }
        return output;
    }
    std::string output;
    for (long long i = slice.start; slice.step > 0 ? i < slice.end : i > slice.end; i += slice.step) {
        output.push_back(value.as_bit_vector().get(static_cast<std::size_t>(i)) ? '1' : '0');
    }
    return Value(BitVector::from_string(output));
}

inline Value shallow_copy_value(const Value& value) {
    if (value.is_array()) return Value(Value::Array(value.as_array().begin(), value.as_array().end()));
    if (value.is_object()) return Value(Value::Object(value.as_object().begin(), value.as_object().end()));
    return value;
}

inline Value replace_array_slice(Value target, std::optional<long long> start, std::optional<long long> end,
                                 const Value& replacement) {
    if (!target.is_array()) throw std::runtime_error("slice assignment target must be an array");
    if (!replacement.is_array()) throw std::runtime_error("array slice assignment requires an array replacement");
    auto& values = target.as_array();
    NormalizedSlice slice = normalize_slice(values.size(), start, end, 1);
    if (slice.end < slice.start) slice.end = slice.start;
    values.erase(values.begin() + slice.start, values.begin() + slice.end);
    values.insert(values.begin() + slice.start, replacement.as_array().begin(), replacement.as_array().end());
    return target;
}

} // namespace arco
