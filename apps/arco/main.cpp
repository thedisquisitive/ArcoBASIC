#include "arco/runtime.hpp"

#include <fstream>
#include <iostream>
#include <cctype>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

namespace {

std::optional<std::size_t> parse_instruction_limit(const std::string& value) {
    std::string normalized = value;
    for (char& c : normalized) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (normalized == "unlimited") return 0;
    if (value.empty() || value == "0") throw std::runtime_error("instruction limit must be 1..9007199254740991 or unlimited");
    if (value.find_first_not_of("0123456789") != std::string::npos) {
        throw std::runtime_error("instruction limit must be 1..9007199254740991 or unlimited");
    }
    const auto parsed = std::stoull(value);
    if (parsed == 0 || parsed > 9007199254740991ULL || parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("instruction limit must be 1..9007199254740991 or unlimited");
    }
    return static_cast<std::size_t>(parsed);
}

} // namespace

int main(int argc, char** argv) {
    int script_index = 1;
    std::optional<std::size_t> instruction_limit;
    if (argc >= 3 && std::string(argv[1]) == "--instruction-limit") {
        try {
            instruction_limit = parse_instruction_limit(argv[2]);
        } catch (const std::exception& error) {
            std::cerr << "arco_cli: " << error.what() << '\n';
            return 2;
        }
        script_index = 3;
    }
    if (argc < script_index + 1) {
        std::cerr << "usage: arco_cli [--instruction-limit COUNT|unlimited] <script.bas> [args...]\n"
                  << "       unlimited disables instruction-count termination for this invocation\n";
        return 2;
    }

    std::ifstream input(argv[script_index]);
    if (!input) {
        std::cerr << "could not open " << argv[script_index] << '\n';
        return 1;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();

    arco::Value::Array script_args;
    for (int i = script_index + 1; i < argc; ++i) {
        script_args.emplace_back(std::string(argv[i]));
    }

    arco::Runtime runtime;
    runtime.set_instruction_limit_policy(true);
    if (instruction_limit.has_value()) runtime.set_instruction_limit_override(instruction_limit);
    runtime.set_global("Args", arco::Value(std::move(script_args)));
    const auto result = runtime.run_string(buffer.str());
    if (!result.ok) {
        std::cerr << result.error << '\n';
        return 1;
    }
    return 0;
}
