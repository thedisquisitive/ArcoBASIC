#include "arco/runtime.hpp"

#include "../core/lexer.hpp"
#include "../core/parser.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace arco {

namespace {

struct ConditionalFrame {
    bool parent_active = true;
    bool active = true;
    bool branch_taken = false;
};

std::string trim_copy(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string upper_copy(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string function_key(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::vector<std::string> split_words(std::string text) {
    std::replace(text.begin(), text.end(), ',', ' ');
    std::istringstream in(text);
    std::vector<std::string> words;
    std::string word;
    while (in >> word) {
        words.push_back(word);
    }
    return words;
}

std::string unquote(const std::string& text) {
    const std::string value = trim_copy(text);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::optional<std::filesystem::path> executable_directory() {
#ifndef _WIN32
    std::array<char, 4096> path{};
    const ssize_t length = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (length <= 0) {
        return std::nullopt;
    }
    path[static_cast<std::size_t>(length)] = '\0';
    return std::filesystem::path(path.data()).parent_path();
#else
    return std::nullopt;
#endif
}

std::vector<std::filesystem::path> import_candidates(const std::string& import_name) {
    const std::filesystem::path requested(import_name);
    std::vector<std::filesystem::path> bases = {
        std::filesystem::current_path(),
        std::filesystem::current_path() / "stdlib",
        std::filesystem::current_path() / "../stdlib",
        std::filesystem::path("/usr/local/share/arcobasic/stdlib"),
        std::filesystem::path("/usr/share/arcobasic/stdlib")
    };
    if (const auto exe_dir = executable_directory()) {
        bases.push_back(*exe_dir / "../share/arcobasic/stdlib");
    }
    if (const char* stdlib_env = std::getenv("ARCOBASIC_STDLIB")) {
        if (*stdlib_env) {
            bases.emplace_back(stdlib_env);
        }
    }

    std::vector<std::filesystem::path> candidates;
    auto add_candidate = [&candidates](const std::filesystem::path& path) {
        candidates.push_back(path);
        if (!path.has_extension()) {
            candidates.push_back(path.string() + ".abas");
            candidates.push_back(path.string() + ".arc");
            candidates.push_back(path.string() + ".bas");
        }
    };

    add_candidate(requested);
    if (requested.is_relative()) {
        for (const auto& base : bases) {
            add_candidate(base / requested);
        }
    }
    return candidates;
}

std::filesystem::path resolve_import_path(const std::string& import_name) {
    for (const auto& candidate : import_candidates(import_name)) {
        if (std::filesystem::exists(candidate) && !std::filesystem::is_directory(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("could not include " + import_name);
}

Value parse_define_value(const std::string& text) {
    const std::string value = trim_copy(text);
    if (value.empty()) {
        return true;
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return unquote(value);
    }
    const std::string upper = upper_copy(value);
    if (upper == "TRUE") {
        return true;
    }
    if (upper == "FALSE") {
        return false;
    }
    if (value.rfind("0b", 0) == 0 || value.rfind("0B", 0) == 0) {
        return static_cast<double>(std::stoll(value.substr(2), nullptr, 2));
    }
    if (!value.empty() && value.front() == '%') {
        return static_cast<double>(std::stoll(value.substr(1), nullptr, 2));
    }
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
        return static_cast<double>(std::stoll(value.substr(2), nullptr, 16));
    }
    if (value.rfind("&H", 0) == 0 || value.rfind("&h", 0) == 0) {
        return static_cast<double>(std::stoll(value.substr(2), nullptr, 16));
    }
    return std::stod(value);
}

bool active_conditions(const std::vector<ConditionalFrame>& frames) {
    for (const auto& frame : frames) {
        if (!frame.active) {
            return false;
        }
    }
    return true;
}

bool symbol_enabled(const std::set<std::string>& defines, const std::string& symbol) {
    return defines.find(upper_copy(symbol)) != defines.end();
}

std::string read_text_file(const std::string& path) {
    const auto resolved = resolve_import_path(path);
    std::ifstream input(resolved);
    if (!input) {
        throw std::runtime_error("could not include " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::vector<std::string> source_lines(const std::string& code) {
    std::vector<std::string> lines;
    std::istringstream input(code);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    if (!code.empty() && code.back() == '\n') {
        lines.emplace_back();
    }
    return lines;
}

bool parse_line_column_error(const std::string& error, int& line, int& column) {
    const std::string prefix = "line ";
    if (error.rfind(prefix, 0) != 0) {
        return false;
    }
    const auto comma = error.find(", column ", prefix.size());
    if (comma == std::string::npos) {
        return false;
    }
    const auto colon = error.find(':', comma + 9);
    if (colon == std::string::npos) {
        return false;
    }
    try {
        line = std::stoi(error.substr(prefix.size(), comma - prefix.size()));
        column = std::stoi(error.substr(comma + 9, colon - (comma + 9)));
    } catch (const std::exception&) {
        return false;
    }
    return line > 0 && column > 0;
}

bool parse_at_line_error(const std::string& error, int& line) {
    const std::string marker = " at line ";
    const auto position = error.rfind(marker);
    if (position == std::string::npos) {
        return false;
    }
    try {
        line = std::stoi(error.substr(position + marker.size()));
    } catch (const std::exception&) {
        return false;
    }
    return line > 0;
}

std::string format_source_diagnostic(const std::string& error, const std::string& code) {
    int line = 0;
    int column = 0;
    const auto lines = source_lines(code);
    if (parse_line_column_error(error, line, column)) {
        if (static_cast<std::size_t>(line) > lines.size()) {
            return error;
        }

        std::ostringstream output;
        output << error << '\n';
        output << lines[static_cast<std::size_t>(line - 1)] << '\n';
        for (int i = 1; i < column; ++i) {
            output << ' ';
        }
        output << '^';
        return output.str();
    }

    if (parse_at_line_error(error, line)) {
        if (static_cast<std::size_t>(line) > lines.size()) {
            return error;
        }
        std::ostringstream output;
        output << error << '\n';
        output << lines[static_cast<std::size_t>(line - 1)];
        return output.str();
    }
    return error;
}

std::string format_runtime_diagnostic(const std::string& error, const std::string& code, int line, int column) {
    const auto lines = source_lines(code);
    if (line <= 0 || column <= 0 || static_cast<std::size_t>(line) > lines.size()) {
        return error;
    }
    std::ostringstream output;
    output << error << '\n';
    output << "runtime error at line " << line << ", column " << column << '\n';
    output << lines[static_cast<std::size_t>(line - 1)] << '\n';
    for (int i = 1; i < column; ++i) {
        output << ' ';
    }
    output << '^';
    return output.str();
}

long long value_to_int(const Value& value) {
    return static_cast<long long>(value.as_number());
}

Value bit_binary(const std::vector<Value>& args, const std::string& name, char op) {
    if (args.size() != 2) {
        throw std::runtime_error(name + " expects 2 arguments");
    }
    const long long left = value_to_int(args[0]);
    const long long right = value_to_int(args[1]);
    switch (op) {
        case '&':
            return static_cast<double>(left & right);
        case '|':
            return static_cast<double>(left | right);
        case '^':
            return static_cast<double>(left ^ right);
        case '<':
            return static_cast<double>(left << right);
        case '>':
            return static_cast<double>(left >> right);
        default:
            throw std::runtime_error("unknown bit operation");
    }
}

void expect_arg_count(const std::vector<Value>& args, const std::string& name, std::size_t min, std::size_t max) {
    if (args.size() < min || args.size() > max) {
        throw std::runtime_error(name + " expects " + std::to_string(min == max ? min : min) + (min == max ? "" : " or " + std::to_string(max)) + " arguments");
    }
}

std::string bits_to_string(unsigned long long value, int width) {
    std::string bits;
    if (value == 0) {
        bits = "0";
    } else {
        while (value != 0) {
            bits.push_back((value & 1ULL) ? '1' : '0');
            value >>= 1U;
        }
        std::reverse(bits.begin(), bits.end());
    }
    if (width > static_cast<int>(bits.size())) {
        bits.insert(bits.begin(), static_cast<std::size_t>(width - bits.size()), '0');
    }
    return bits;
}

Value shift_function(const std::vector<Value>& args) {
    expect_arg_count(args, "SHIFT", 2, 2);
    const long long value = value_to_int(args[0]);
    const long long amount = value_to_int(args[1]);
    return static_cast<double>(amount >= 0 ? (value << amount) : (value >> -amount));
}

Value bit_test_function(const std::vector<Value>& args) {
    expect_arg_count(args, "BIT", 2, 2);
    return (value_to_int(args[0]) & (1LL << value_to_int(args[1]))) != 0;
}

Value bit_set_function(const std::vector<Value>& args) {
    expect_arg_count(args, "SETBIT", 2, 2);
    return static_cast<double>(value_to_int(args[0]) | (1LL << value_to_int(args[1])));
}

Value bit_clear_function(const std::vector<Value>& args) {
    expect_arg_count(args, "CLEARBIT", 2, 2);
    return static_cast<double>(value_to_int(args[0]) & ~(1LL << value_to_int(args[1])));
}

Value bit_toggle_function(const std::vector<Value>& args) {
    expect_arg_count(args, "TOGGLEBIT", 2, 2);
    return static_cast<double>(value_to_int(args[0]) ^ (1LL << value_to_int(args[1])));
}

Value bits_text_function(const std::vector<Value>& args) {
    expect_arg_count(args, "BitsToString", 1, 2);
    const int width = args.size() == 2 ? static_cast<int>(value_to_int(args[1])) : 0;
    return bits_to_string(static_cast<unsigned long long>(value_to_int(args[0])), width);
}

Value string_to_bits_function(const std::vector<Value>& args) {
    expect_arg_count(args, "StringToBits", 1, 1);
    long long value = 0;
    for (char c : args[0].to_string()) {
        if (c != '0' && c != '1') {
            throw std::runtime_error("StringToBits expects a binary string");
        }
        value = (value << 1) | (c == '1' ? 1 : 0);
    }
    return static_cast<double>(value);
}

Value bitcount_function(const std::vector<Value>& args) {
    expect_arg_count(args, "BITCOUNT", 1, 1);
    unsigned long long value = static_cast<unsigned long long>(value_to_int(args[0]));
    int count = 0;
    while (value != 0) {
        count += static_cast<int>(value & 1ULL);
        value >>= 1U;
    }
    return static_cast<double>(count);
}

Value rotate_function(const std::vector<Value>& args, bool left) {
    expect_arg_count(args, left ? "ROTATELEFT" : "ROTATERIGHT", 2, 2);
    const unsigned long long value = static_cast<unsigned long long>(value_to_int(args[0]));
    const unsigned int amount = static_cast<unsigned int>(value_to_int(args[1])) % 64U;
    if (amount == 0) {
        return static_cast<double>(value);
    }
    const unsigned long long rotated = left ? ((value << amount) | (value >> (64U - amount))) : ((value >> amount) | (value << (64U - amount)));
    return static_cast<double>(rotated);
}

Value bits_table_function(const std::vector<Value>& args) {
    expect_arg_count(args, "BitsTable", 1, 2);
    const long long value = value_to_int(args[0]);
    int width = args.size() == 2 ? static_cast<int>(value_to_int(args[1])) : 8;
    if (width < 1) {
        width = 1;
    }
    std::ostringstream out;
    out << "Bit  Value  Set\n\n";
    for (int bit = width - 1; bit >= 0; --bit) {
        const long long bit_value = 1LL << bit;
        out << bit << "    " << bit_value << "    " << ((value & bit_value) ? "Yes" : "No");
        if (bit != 0) {
            out << '\n';
        }
    }
    return out.str();
}

Value hex_to_string_function(const std::vector<Value>& args) {
    expect_arg_count(args, "HexToString", 1, 1);
    std::ostringstream out;
    out << std::uppercase << std::hex << value_to_int(args[0]);
    return out.str();
}

Value string_to_hex_function(const std::vector<Value>& args) {
    expect_arg_count(args, "StringToHex", 1, 1);
    return static_cast<double>(std::stoll(args[0].to_string(), nullptr, 16));
}

Value bytes_to_hex_function(const std::vector<Value>& args) {
    expect_arg_count(args, "BytesToHex", 1, 1);
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    if (args[0].is_array()) {
        for (const auto& byte : args[0].as_array()) {
            out << std::setw(2) << (value_to_int(byte) & 0xFF);
        }
    } else {
        for (unsigned char c : args[0].to_string()) {
            out << std::setw(2) << static_cast<int>(c);
        }
    }
    return out.str();
}

Value hex_to_bytes_function(const std::vector<Value>& args) {
    expect_arg_count(args, "HexToBytes", 1, 1);
    std::string text = args[0].to_string();
    if (text.size() % 2 != 0) {
        text.insert(text.begin(), '0');
    }
    Value::Array bytes;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        bytes.emplace_back(static_cast<double>(std::stoll(text.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

Value array_push_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Push", 2, 2);
    Value array_value = args[0];
    auto& array = array_value.as_array();
    array.push_back(args[1]);
    return static_cast<double>(array.size());
}

Value array_pop_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Pop", 1, 1);
    Value array_value = args[0];
    auto& array = array_value.as_array();
    if (array.empty()) {
        return {};
    }
    Value value = array.back();
    array.pop_back();
    return value;
}

Value array_find_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Find", 2, 2);
    const auto& array = args[0].as_array();
    for (std::size_t i = 0; i < array.size(); ++i) {
        if (values_equal(array[i], args[1])) {
            return static_cast<double>(i);
        }
    }
    return -1.0;
}

Value array_reverse_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Reverse", 1, 1);
    Value::Array result = args[0].as_array();
    std::reverse(result.begin(), result.end());
    return result;
}

Value array_join_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Join", 2, 2);
    const auto& array = args[0].as_array();
    const std::string separator = args[1].to_string();
    std::ostringstream output;
    for (std::size_t i = 0; i < array.size(); ++i) {
        if (i != 0) {
            output << separator;
        }
        output << array[i].to_string();
    }
    return output.str();
}

Value array_contains_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Contains", 2, 2);
    const auto& array = args[0].as_array();
    return std::any_of(array.begin(), array.end(), [&](const Value& value) {
        return values_equal(value, args[1]);
    });
}

Value array_sort_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Sort", 1, 1);
    Value::Array result = args[0].as_array();
    std::sort(result.begin(), result.end(), [](const Value& left, const Value& right) {
        if (left.is_number() && right.is_number()) {
            return left.as_number() < right.as_number();
        }
        return left.to_string() < right.to_string();
    });
    return result;
}

Value object_keys_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Object.Keys", 1, 1);
    Value::Array keys;
    for (const auto& [key, value] : args[0].as_object()) {
        (void)value;
        keys.emplace_back(key);
    }
    return keys;
}

Value object_has_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Object.Has", 2, 2);
    const auto& object = args[0].as_object();
    return object.find(args[1].to_string()) != object.end();
}

Value object_get_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Object.Get", 2, 3);
    const auto& object = args[0].as_object();
    const auto found = object.find(args[1].to_string());
    if (found != object.end()) {
        return found->second;
    }
    return args.size() == 3 ? args[2] : Value();
}

Value object_set_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Object.Set", 3, 3);
    Value::Object object = args[0].as_object();
    object[args[1].to_string()] = args[2];
    return object;
}

std::string trim_text(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

Value string_split_function(const std::vector<Value>& args) {
    expect_arg_count(args, "String.Split", 2, 2);
    const std::string text = args[0].to_string();
    const std::string delimiter = args[1].to_string();
    if (delimiter.empty()) {
        throw std::runtime_error("String.Split delimiter cannot be empty");
    }
    Value::Array parts;
    std::size_t start = 0;
    while (true) {
        const auto found = text.find(delimiter, start);
        if (found == std::string::npos) {
            parts.emplace_back(text.substr(start));
            break;
        }
        parts.emplace_back(text.substr(start, found - start));
        start = found + delimiter.size();
    }
    return parts;
}

Value string_replace_function(const std::vector<Value>& args) {
    expect_arg_count(args, "String.Replace", 3, 3);
    std::string text = args[0].to_string();
    const std::string from = args[1].to_string();
    const std::string to = args[2].to_string();
    if (from.empty()) {
        return text;
    }
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

Value string_lines_function(const std::vector<Value>& args) {
    expect_arg_count(args, "String.Lines", 1, 1);
    std::istringstream input(args[0].to_string());
    std::string line;
    Value::Array lines;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.emplace_back(line);
    }
    return lines;
}

Value format_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Format", 1, 64);
    std::string text = args[0].to_string();
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string key = "{" + std::to_string(i - 1) + "}";
        const std::string value = args[i].to_string();
        std::size_t position = 0;
        while ((position = text.find(key, position)) != std::string::npos) {
            text.replace(position, key.size(), value);
            position += value.size();
        }
    }
    return text;
}

Value time_timestamp_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Time.Timestamp", 0, 0);
    const auto now = std::chrono::system_clock::now();
    return static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
}

Value time_now_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Time.Now", 0, 0);
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream output;
    output << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

Value sleep_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Sleep", 1, 1);
    const auto milliseconds = static_cast<int>(args[0].as_number());
    if (milliseconds > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
    return {};
}

} // namespace

ExitSignal::ExitSignal(int code) : code_(code) {}

const char* ExitSignal::what() const noexcept {
    return "program exited";
}

int ExitSignal::code() const noexcept {
    return code_;
}

ReturnSignal::ReturnSignal(Value value) : value_(std::move(value)) {}

const char* ReturnSignal::what() const noexcept {
    return "function returned";
}

const Value& ReturnSignal::value() const noexcept {
    return value_;
}

GotoSignal::GotoSignal(int line) : line_(line) {}

const char* GotoSignal::what() const noexcept {
    return "goto";
}

int GotoSignal::line() const noexcept {
    return line_;
}

const char* StopSignal::what() const noexcept {
    return "program stopped";
}

Runtime::Runtime() : output_(&std::cout) {
    register_function("PRINT", [this](const std::vector<Value>& args) -> Value {
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i != 0) {
                *output_ << ' ';
            }
            *output_ << args[i].to_string();
        }
        *output_ << '\n';
        return {};
    });
    register_function("LEN", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("LEN expects 1 argument");
        }
        if (args[0].is_array()) {
            return static_cast<double>(args[0].as_array().size());
        }
        if (args[0].is_object()) {
            return static_cast<double>(args[0].as_object().size());
        }
        return static_cast<double>(args[0].to_string().size());
    });
    register_function("Upper", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Upper expects 1 argument");
        }
        std::string value = args[0].to_string();
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return value;
    });
    register_function("Lower", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Lower expects 1 argument");
        }
        std::string value = args[0].to_string();
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    });
    register_function("TYPEOF", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("TYPEOF expects 1 argument");
        }
        if (args[0].is_null()) {
            return "Null";
        }
        if (args[0].is_bool()) {
            return "Boolean";
        }
        if (args[0].is_number()) {
            return "Number";
        }
        if (args[0].is_string()) {
            return "String";
        }
        if (args[0].is_array()) {
            return "Array";
        }
        return "Object";
    });
    register_function("ISNULL", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("ISNULL expects 1 argument");
        }
        return args[0].is_null();
    });
    register_function("NUMBER", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("NUMBER expects 1 argument");
        }
        if (args[0].is_number() || args[0].is_bool()) {
            return args[0].as_number();
        }
        return std::stod(args[0].to_string());
    });
    register_function("STRING", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("STRING expects 1 argument");
        }
        return args[0].to_string();
    });
    register_function("Array.Push", array_push_function);
    register_function("Array.Pop", array_pop_function);
    register_function("Array.Find", array_find_function);
    register_function("Array.Reverse", array_reverse_function);
    register_function("Array.Join", array_join_function);
    register_function("Array.Contains", array_contains_function);
    register_function("Array.Sort", array_sort_function);
    register_function("Object.Keys", object_keys_function);
    register_function("Object.Has", object_has_function);
    register_function("Object.Get", object_get_function);
    register_function("Object.Set", object_set_function);
    register_function("String.Trim", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "String.Trim", 1, 1);
        return trim_text(args[0].to_string());
    });
    register_function("String.Split", string_split_function);
    register_function("String.Replace", string_replace_function);
    register_function("String.Contains", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "String.Contains", 2, 2);
        return args[0].to_string().find(args[1].to_string()) != std::string::npos;
    });
    register_function("String.StartsWith", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "String.StartsWith", 2, 2);
        const std::string text = args[0].to_string();
        const std::string prefix = args[1].to_string();
        return text.rfind(prefix, 0) == 0;
    });
    register_function("String.EndsWith", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "String.EndsWith", 2, 2);
        const std::string text = args[0].to_string();
        const std::string suffix = args[1].to_string();
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    });
    register_function("String.Lines", string_lines_function);
    register_function("Format", format_function);
    register_function("Bit.And", [](const std::vector<Value>& args) -> Value { return bit_binary(args, "Bit.And", '&'); });
    register_function("Bit.Or", [](const std::vector<Value>& args) -> Value { return bit_binary(args, "Bit.Or", '|'); });
    register_function("Bit.Xor", [](const std::vector<Value>& args) -> Value { return bit_binary(args, "Bit.Xor", '^'); });
    register_function("Bit.ShiftLeft", [](const std::vector<Value>& args) -> Value { return bit_binary(args, "Bit.ShiftLeft", '<'); });
    register_function("Bit.ShiftRight", [](const std::vector<Value>& args) -> Value { return bit_binary(args, "Bit.ShiftRight", '>'); });
    register_function("Bit.Not", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Bit.Not expects 1 argument");
        }
        return static_cast<double>(~value_to_int(args[0]));
    });
    register_function("SHIFT", shift_function);
    register_function("BIT", bit_test_function);
    register_function("SETBIT", bit_set_function);
    register_function("CLEARBIT", bit_clear_function);
    register_function("TOGGLEBIT", bit_toggle_function);
    register_function("BITCOUNT", bitcount_function);
    register_function("ROTATELEFT", [](const std::vector<Value>& args) -> Value { return rotate_function(args, true); });
    register_function("ROTATERIGHT", [](const std::vector<Value>& args) -> Value { return rotate_function(args, false); });
    register_function("BitsToString", bits_text_function);
    register_function("BitsToBinary", bits_text_function);
    register_function("StringToBits", string_to_bits_function);
    register_function("BitsTable", bits_table_function);
    register_function("HexToString", hex_to_string_function);
    register_function("StringToHex", string_to_hex_function);
    register_function("BytesToHex", bytes_to_hex_function);
    register_function("HexToBytes", hex_to_bytes_function);
    register_function("Time.Now", time_now_function);
    register_function("Time.Timestamp", time_timestamp_function);
    register_function("DATE", time_now_function);
    register_function("Date", time_now_function);
    register_function("Sleep", sleep_function);
}

std::string Runtime::preprocess_source(const std::string& code) {
    return preprocess_source(code, true);
}

std::string Runtime::preprocess_source(const std::string& code, bool reset_metadata) {
    if (reset_metadata) {
        metadata_ = {};
    }

    std::set<std::string> defines = {
#if defined(_WIN32)
        "TARGET_WINDOWS",
#elif defined(__APPLE__)
        "TARGET_MACOS",
#elif defined(__linux__)
        "TARGET_LINUX",
#endif
        "DEBUG"
    };

    std::vector<ConditionalFrame> conditionals;
    std::ostringstream output;
    std::istringstream input(code);
    std::string line;

    while (std::getline(input, line)) {
        const std::string trimmed = trim_copy(line);
        if (trimmed.rfind("#!", 0) == 0) {
            output << '\n';
            continue;
        }

        if (!trimmed.empty() && trimmed.front() == '@') {
            if (active_conditions(conditionals)) {
                metadata_.attributes.push_back(trimmed);
            }
            output << '\n';
            continue;
        }

        if (trimmed.empty() || trimmed.front() != '#') {
            if (active_conditions(conditionals)) {
                output << line << '\n';
            } else {
                output << '\n';
            }
            continue;
        }

        std::string rest = trim_copy(trimmed.substr(1));
        const auto split = rest.find_first_of(" \t");
        const std::string directive = upper_copy(rest.substr(0, split == std::string::npos ? std::string::npos : split));
        const std::string args = split == std::string::npos ? "" : trim_copy(rest.substr(split + 1));
        const bool active = active_conditions(conditionals);

        if (directive == "IFDEF" || directive == "IFNDEF" || directive == "IF") {
            const bool parent = active_conditions(conditionals);
            bool enabled = false;
            if (directive == "IFDEF") {
                enabled = symbol_enabled(defines, args);
            } else if (directive == "IFNDEF") {
                enabled = !symbol_enabled(defines, args);
            } else {
                enabled = symbol_enabled(defines, args) || upper_copy(args) == "TRUE" || args == "1";
            }
            conditionals.push_back({parent, parent && enabled, parent && enabled});
            output << '\n';
            continue;
        }
        if (directive == "ELSEIF") {
            if (conditionals.empty()) {
                throw std::runtime_error("#ELSEIF without #IF");
            }
            auto& frame = conditionals.back();
            const bool enabled = !frame.branch_taken && (symbol_enabled(defines, args) || upper_copy(args) == "TRUE" || args == "1");
            frame.active = frame.parent_active && enabled;
            frame.branch_taken = frame.branch_taken || frame.active;
            output << '\n';
            continue;
        }
        if (directive == "ELSE") {
            if (conditionals.empty()) {
                throw std::runtime_error("#ELSE without #IF");
            }
            auto& frame = conditionals.back();
            frame.active = frame.parent_active && !frame.branch_taken;
            frame.branch_taken = true;
            output << '\n';
            continue;
        }
        if (directive == "ENDIF") {
            if (conditionals.empty()) {
                throw std::runtime_error("#ENDIF without #IF");
            }
            conditionals.pop_back();
            output << '\n';
            continue;
        }

        if (!active) {
            output << '\n';
            continue;
        }

        if (directive == "DEFINE") {
            const auto name_end = args.find_first_of(" \t");
            const std::string name = name_end == std::string::npos ? args : args.substr(0, name_end);
            const std::string value = name_end == std::string::npos ? "" : trim_copy(args.substr(name_end + 1));
            if (!name.empty()) {
                defines.insert(upper_copy(name));
                if (!value.empty()) {
                    set_global(name, parse_define_value(value));
                } else {
                    set_global(name, true);
                }
            }
        } else if (directive == "UNDEF") {
            defines.erase(upper_copy(args));
        } else if (directive == "VERSION") {
            metadata_.version = unquote(args);
        } else if (directive == "AUTHOR") {
            metadata_.author = unquote(args);
        } else if (directive == "DESCRIPTION") {
            metadata_.description = unquote(args);
        } else if (directive == "ENTRY") {
            metadata_.entry = args;
        } else if (directive == "TARGET") {
            metadata_.targets = split_words(args);
        } else if (directive == "REQUIRE") {
            metadata_.requirements.push_back(args);
        } else if (directive == "FEATURE") {
            metadata_.features.push_back(args);
        } else if (directive == "STRICT") {
            metadata_.strict = args.empty() || upper_copy(args) == "ON" || upper_copy(args) == "TRUE";
        } else if (directive == "EXPERIMENTAL") {
            metadata_.experimental = true;
            if (!args.empty()) {
                metadata_.notes.push_back("experimental: " + unquote(args));
            }
        } else if (directive == "DEPRECATED") {
            metadata_.deprecated = true;
            if (!args.empty()) {
                metadata_.warnings.push_back("deprecated: " + unquote(args));
            }
        } else if (directive == "WARNING") {
            metadata_.warnings.push_back(unquote(args));
            *output_ << "warning: " << unquote(args) << '\n';
        } else if (directive == "ERROR") {
            throw std::runtime_error(unquote(args));
        } else if (directive == "TODO") {
            metadata_.todos.push_back(unquote(args));
        } else if (directive == "NOTE") {
            metadata_.notes.push_back(unquote(args));
        } else if (directive == "REGION" || directive == "ENDREGION") {
        } else if (directive == "INCLUDE") {
            output << preprocess_source(read_text_file(unquote(args)), false);
        } else if (directive == "IMPORT") {
            metadata_.imports.push_back(unquote(args));
            output << preprocess_source(read_text_file(unquote(args)), false);
        } else if (directive == "PACK") {
            metadata_.pack = args;
        } else if (directive == "ALIGN") {
            metadata_.align = args;
        } else if (directive == "ENDIAN") {
            metadata_.endian = args;
        } else {
            metadata_.warnings.push_back("unknown directive: #" + directive);
        }
        output << '\n';
    }

    if (!conditionals.empty()) {
        throw std::runtime_error("unterminated conditional directive");
    }

    return output.str();
}

RunResult Runtime::run_string(const std::string& code) {
    std::string processed;
    try {
        reset_instruction_count();
        processed = preprocess_source(code);
        Lexer lexer(processed);
        Parser parser(lexer.scan_tokens());
        auto statements = parser.parse();
        std::unordered_map<int, std::size_t> labels;
        for (std::size_t i = 0; i < statements.size(); ++i) {
            if (statements[i]->line_label >= 0) {
                labels[statements[i]->line_label] = i;
            }
        }
        for (std::size_t pc = 0; pc < statements.size();) {
            try {
                statements[pc]->exec(*this);
                pc++;
            } catch (const GotoSignal& jump) {
                const auto target = labels.find(jump.line());
                if (target == labels.end()) {
                    throw std::runtime_error("undefined line number: " + std::to_string(jump.line()));
                }
                pc = target->second;
            } catch (const StopSignal&) {
                break;
            } catch (const ExitSignal&) {
                throw;
            } catch (const std::exception& error) {
                throw std::runtime_error(format_runtime_diagnostic(error.what(), processed, statements[pc]->source_line, statements[pc]->source_column));
            }
        }
        return {};
    } catch (const ExitSignal& exit) {
        return {true, "", true, exit.code()};
    } catch (const std::exception& error) {
        return {false, format_source_diagnostic(error.what(), processed.empty() ? code : processed)};
    }
}

const CompileMetadata& Runtime::compile_metadata() const {
    return metadata_;
}

void Runtime::register_function(const std::string& name, HostFunction function) {
    host_functions_[function_key(name)] = std::move(function);
}

void Runtime::set_global(const std::string& name, Value value) {
    const auto dot = name.find('.');
    if (dot != std::string::npos) {
        const std::string root_name = name.substr(0, dot);
        const std::string property_path = name.substr(dot + 1);
        auto assign_property = [&](Value& root) {
            Value* current = &root;
            std::size_t start = 0;
            while (true) {
                const auto next = property_path.find('.', start);
                const std::string property = property_path.substr(start, next == std::string::npos ? std::string::npos : next - start);
                if (next == std::string::npos) {
                    current->set_property(property, std::move(value));
                    return true;
                }
                current = &current->as_object()[property];
                start = next + 1;
            }
        };

        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(root_name);
            if (found != scope->end()) {
                assign_property(found->second);
                return;
            }
        }
        const auto found = globals_.find(root_name);
        if (found != globals_.end()) {
            assign_property(found->second);
            return;
        }
    }

    if (!scopes_.empty()) {
        scopes_.back()[name] = std::move(value);
        return;
    }
    globals_[name] = std::move(value);
}

void Runtime::set_indexed(const std::string& name, const std::vector<int>& indexes, Value value) {
    if (indexes.empty()) {
        set_global(name, std::move(value));
        return;
    }

    auto assign = [&](Value& root) {
        Value* current = &root;
        for (std::size_t i = 0; i < indexes.size(); ++i) {
            auto& array = current->as_array();
            const int index = indexes[i];
            if (index < 0 || static_cast<std::size_t>(index) >= array.size()) {
                throw std::runtime_error("array index out of range");
            }
            if (i + 1 == indexes.size()) {
                array[static_cast<std::size_t>(index)] = std::move(value);
                return;
            }
            current = &array[static_cast<std::size_t>(index)];
        }
    };

    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto found = scope->find(name);
        if (found != scope->end()) {
            assign(found->second);
            return;
        }
    }
    const auto found = globals_.find(name);
    if (found != globals_.end()) {
        assign(found->second);
        return;
    }
    throw std::runtime_error("undefined variable: " + name);
}

Value Runtime::get_global(const std::string& name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto local = scope->find(name);
        if (local != scope->end()) {
            return local->second;
        }
    }

    const auto dot = name.find('.');
    if (dot != std::string::npos) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto root = scope->find(name.substr(0, dot));
            if (root != scope->end()) {
                Value value = root->second;
                std::size_t start = dot + 1;
                while (start < name.size()) {
                    const auto next = name.find('.', start);
                    const std::string property = name.substr(start, next == std::string::npos ? std::string::npos : next - start);
                    value = value.get_property(property);
                    if (next == std::string::npos) {
                        return value;
                    }
                    start = next + 1;
                }
            }
        }
    }

    const auto found = globals_.find(name);
    if (found != globals_.end()) {
        return found->second;
    }

    if (dot != std::string::npos) {
        const auto root = globals_.find(name.substr(0, dot));
        if (root != globals_.end()) {
            Value value = root->second;
            std::size_t start = dot + 1;
            while (start < name.size()) {
                const auto next = name.find('.', start);
                const std::string property = name.substr(start, next == std::string::npos ? std::string::npos : next - start);
                value = value.get_property(property);
                if (next == std::string::npos) {
                    return value;
                }
                start = next + 1;
            }
        }
    }

    if (found == globals_.end()) {
        throw std::runtime_error("undefined variable: " + name);
    }
    return {};
}

bool Runtime::has_global(const std::string& name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        if (scope->find(name) != scope->end()) {
            return true;
        }
    }
    return globals_.find(name) != globals_.end();
}

void Runtime::push_scope() {
    scopes_.push_back({});
}

void Runtime::pop_scope() {
    if (scopes_.empty()) {
        throw std::runtime_error("no local scope to pop");
    }
    scopes_.pop_back();
}

void Runtime::set_output(std::ostream& output) {
    output_ = &output;
}

std::ostream& Runtime::output() {
    return *output_;
}

void Runtime::set_limits(RuntimeLimits limits) {
    limits_ = limits;
}

const RuntimeLimits& Runtime::limits() const {
    return limits_;
}

void Runtime::tick() {
    instruction_count_++;
    if (limits_.instruction_limit > 0 && instruction_count_ > limits_.instruction_limit) {
        throw std::runtime_error("instruction limit exceeded");
    }
}

void Runtime::reset_instruction_count() {
    instruction_count_ = 0;
}

Value Runtime::call_host_function(const std::string& name, const std::vector<Value>& args) {
    tick();
    const auto found = host_functions_.find(function_key(name));
    if (found == host_functions_.end()) {
        throw std::runtime_error("unknown host function: " + name);
    }
    return found->second(args);
}

} // namespace arco
