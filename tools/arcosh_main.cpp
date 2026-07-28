#include "arco/shell.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    arco::Runtime runtime;
    arco::shell::register_shell_builtins(runtime);

    int arg_index = 1;
    bool login = false;
    bool load_rc = true;
    std::string rc_path;
    while (arg_index < argc) {
        const std::string option = argv[arg_index];
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
