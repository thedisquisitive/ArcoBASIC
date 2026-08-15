#include "arco/shell.hpp"

#include <iostream>
#include <cctype>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::size_t parse_instruction_limit(const std::string& value) {
    std::string normalized = value;
    for (char& c : normalized) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (normalized == "unlimited") return 0;
    if (value.empty() || value == "0" || value.find_first_not_of("0123456789") != std::string::npos) {
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
    arco::Runtime runtime;
    runtime.set_instruction_limit_policy(true);
    arco::shell::register_shell_builtins(runtime);

    int arg_index = 1;
    bool login = false;
    bool load_rc = true;
    std::string rc_path;
    while (arg_index < argc) {
        const std::string option = argv[arg_index];
        if (option == "--instruction-limit") {
            if (arg_index + 1 >= argc) {
                std::cerr << "arcosh: --instruction-limit expects COUNT or unlimited\n";
                return 2;
            }
            try {
                runtime.set_instruction_limit_override(parse_instruction_limit(argv[arg_index + 1]));
            } catch (const std::exception& error) {
                std::cerr << "arcosh: " << error.what() << '\n';
                return 2;
            }
            arg_index += 2;
            continue;
        }
        if (option == "--no-color") {
            arco::shell::set_color_enabled(false);
            arg_index++;
            continue;
        }
        if (option == "--color") {
            arco::shell::set_color_enabled(true);
            arg_index++;
            continue;
        }
        if (option == "--login" || option == "-l") {
            login = true;
            arg_index++;
            continue;
        }
        if (option == "--no-rc" || option == "--norc") {
            load_rc = false;
            arg_index++;
            continue;
        }
        if (option == "--safe") {
            load_rc = false;
            arco::shell::set_color_enabled(false);
            arg_index++;
            continue;
        }
        if (option == "--rc") {
            if (arg_index + 1 >= argc) {
                std::cerr << "arcosh: --rc expects a file\n";
                return 2;
            }
            rc_path = argv[arg_index + 1];
            load_rc = false;
            arg_index += 2;
            continue;
        }
        break;
    }

    if (arg_index < argc && std::string(argv[arg_index]) == "--doctor") {
        return arco::shell::doctor(std::cout);
    }

    if (arg_index < argc && std::string(argv[arg_index]) == "--init-profile") {
        const auto result = arco::shell::init_profile(std::cout);
        if (!result.ok) {
            std::cerr << result.error << '\n';
            return 1;
        }
        return 0;
    }

    if (load_rc) {
        const auto startup = arco::shell::load_startup(runtime, std::cerr, login);
        if (startup.exited) {
            return startup.exit_code;
        }
    }
    if (!rc_path.empty()) {
        const auto startup = arco::shell::run_file(runtime, rc_path);
        if (!startup.ok) {
            std::cerr << "arcosh startup " << rc_path << ": " << startup.error << '\n';
            return 1;
        }
        if (startup.exited) {
            return startup.exit_code;
        }
    }

    if (arg_index >= argc) {
        return arco::shell::repl(runtime, std::cin, std::cout, true);
    }

    const std::string arg = argv[arg_index];
    if (arg == "--help" || arg == "-h") {
        std::cout << arco::shell::help_text(arg_index + 1 < argc ? argv[arg_index + 1] : "");
        return 0;
    }
    if (arg == "--version") {
        std::cout << arco::shell::colorize("ArcoSH", "green") << " alpha 0.1\n";
        return 0;
    }
    if (arg == "-c" || arg == "--command") {
        if (arg_index + 1 >= argc) {
            std::cerr << "arcosh: -c expects a command\n";
            return 2;
        }
        return arco::shell::run_command_once(runtime, argv[arg_index + 1], std::cout);
    }
    if (arg == "--repl") {
        return arco::shell::repl(runtime, std::cin, std::cout, true);
    }
    if (arg == "--tutorial") {
        const auto result = arco::shell::run_tutorial(runtime, std::cin, std::cout, arg_index + 1 < argc ? argv[arg_index + 1] : "");
        if (!result.ok) {
            std::cerr << result.error << '\n';
            return 1;
        }
        return result.exited ? result.exit_code : 0;
    }
    if (arg == "--install-shell") {
        const auto result = arco::shell::run_login_shell_wizard(runtime);
        if (!result.ok) {
            std::cerr << result.error << '\n';
            return 1;
        }
        return result.exited ? result.exit_code : 0;
    }

    std::vector<std::string> script_args;
    for (int i = arg_index + 1; i < argc; ++i) {
        script_args.emplace_back(argv[i]);
    }
    const auto result = arco::shell::run_file(runtime, arg, script_args);
    if (!result.ok) {
        std::cerr << result.error << '\n';
        return 1;
    }
    if (result.exited) {
        return result.exit_code;
    }
    return 0;
}
