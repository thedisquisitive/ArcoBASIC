#include "arco/fission.hpp"

#include <fstream>
#include <cctype>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

void print_usage(std::ostream& output) {
    output
        << "ArcoFission alpha\n"
        << "\n"
        << "Usage:\n"
        << "  ArcoFission reveal FILE at AST\n"
        << "  ArcoFission reveal FILE at A-MIR\n"
        << "  ArcoFission reveal FILE at BYTECODE\n"
        << "  ArcoFission reveal FILE --stage AST\n"
        << "  ArcoFission reveal FILE --stage A-MIR\n"
        << "  ArcoFission reveal FILE --stage BYTECODE\n"
        << "  ArcoFission build FILE -o OUT\n"
        << "  ArcoFission build FILE -o OUT.efi --target uefi-x86_64 [--entry NAME]\n"
        << "  ArcoFission build FILE -o OUT --target linux-x86_64 [--entry NAME]\n"
        << "    (experimental: compiles straight to real x86-64 machine code, no embedded bytecode\n"
        << "    VM -- Phase 1 scope only, see .agents/reports/ARCO_NATIVE_COMPILER_BACKEND_PLAN.md)\n"
        << "  ArcoFission bytecode FILE -o OUT.arcof\n"
        << "  ArcoFission native FILE -o OUT\n"
        << "  ArcoFission native FILE -o OUT.exe --target windows-x86_64\n"
        << "  ArcoFission native FILE -o OUT.html --target web\n"
        << "    (web needs an Emscripten-targeted build tree; point ARCOFISSION_WEB_TOOLCHAIN_DIR\n"
        << "    at it -- see arcoflow/README.md)\n"
        << "  ArcoFission run FILE.arcof\n"
        << "  ArcoFission compile-run FILE\n"
        << "  Hosted run/build commands accept --instruction-limit COUNT|unlimited\n"
        << "  Hosted ArcoFission execution defaults to unlimited instruction count.\n"
        << "\n"
        << "This first slice validates ArcoBASIC source with the existing parser\n"
        << "and emits the parsed AST, structured A-MIR, or the initial serialized\n"
        << ".arcof bytecode-prep format. On Linux, builds default to an ELF64\n"
        << "runtime capsule linked against the ArcoFission bytecode VM.\n";
}

std::string lowercase(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::size_t parse_instruction_limit(const std::string& value) {
    const std::string normalized = lowercase(value);
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

std::optional<std::size_t> trailing_instruction_limit(int argc, char** argv, int first_optional) {
    if (argc == first_optional) return std::nullopt;
    if (argc != first_optional + 2 || std::string(argv[first_optional]) != "--instruction-limit") {
        throw std::runtime_error("expected --instruction-limit COUNT|unlimited");
    }
    return parse_instruction_limit(argv[first_optional + 1]);
}

std::optional<std::size_t> default_unlimited_instruction_limit() {
    return std::optional<std::size_t>(0);
}

std::optional<std::size_t> hosted_instruction_limit_or_unlimited(int argc, char** argv, int first_optional) {
    const auto requested = trailing_instruction_limit(argc, argv, first_optional);
    if (requested.has_value()) return requested;
    return default_unlimited_instruction_limit();
}

int write_bytecode_file(const std::string& source_path, const std::string& output_path) {
    const auto result = arco::fission::reveal_bytecode_file(source_path);
    if (!result.ok) {
        std::cerr << "SOURCE INTAKE FAILED\n\n" << result.error << '\n';
        return 1;
    }

    std::ofstream output(output_path);
    if (!output) {
        std::cerr << "ArcoFission: could not open output file " << output_path << '\n';
        return 1;
    }
    output << result.output;
    std::cout << "SOURCE ACCEPTED\n";
    std::cout << "STRUCTURE ASSEMBLED\n";
    std::cout << "BYTECODE WRITTEN " << output_path << '\n';
    return 0;
}

int write_native_file(const std::string& source_path, const std::string& output_path,
                      std::optional<std::size_t> instruction_limit = default_unlimited_instruction_limit(),
                      const std::string& target = "") {
    const auto result = arco::fission::build_native_file(source_path, output_path, instruction_limit, target);
    if (!result.ok) {
        std::cerr << "NATIVE BUILD FAILED\n\n" << result.error << '\n';
        return 1;
    }
    std::cout << result.output;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        print_usage(std::cout);
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "ArcoFission alpha 0.1\n";
        return 0;
    }

    if (argc >= 5 && lowercase(argv[1]) == "reveal") {
        const std::string file = argv[2];
        std::string stage;
        if (lowercase(argv[3]) == "at") {
            stage = argv[4];
        } else if (std::string(argv[3]) == "--stage") {
            stage = argv[4];
        }

        const std::string normalized_stage = lowercase(stage);
        const bool wants_ast = normalized_stage == "ast" || normalized_stage == "parsed" ||
                               normalized_stage == "parsed-source";
        const bool wants_amir = normalized_stage == "a-mir" || normalized_stage == "amir";
        const bool wants_bytecode = normalized_stage == "bytecode" || normalized_stage == "a-bc" ||
                                    normalized_stage == "abc" || normalized_stage == "arcof";
        const bool wants_callconv = normalized_stage == "callconv" || normalized_stage == "calling-convention";
        const bool wants_x86_64 = normalized_stage == "x86-64" || normalized_stage == "x86_64" || normalized_stage == "native-asm";
        const bool wants_pretty = normalized_stage == "pretty" || normalized_stage == "source";

        if (!wants_ast && !wants_amir && !wants_bytecode && !wants_callconv && !wants_x86_64 && !wants_pretty) {
            std::cerr << "ArcoFission: this alpha slice can reveal AST, A-MIR, BYTECODE, CALLCONV, PRETTY, or X86_64\n";
            return 2;
        }

        std::string entry_function = "Main";
        if (wants_x86_64 && argc >= 7 && lowercase(argv[5]) == "--entry") {
            entry_function = argv[6];
        }

        const auto result = wants_ast ? arco::fission::reveal_ast_file(file)
                                      : wants_bytecode ? arco::fission::reveal_bytecode_file(file)
                                                       : wants_callconv ? arco::fission::reveal_callconv_file(file)
                                                                        : wants_x86_64 ? arco::fission::reveal_x86_64_file(file, entry_function)
                                                                                       : wants_pretty ? arco::fission::reveal_pretty_file(file)
                                                                                                      : arco::fission::reveal_amir_file(file);
        if (!result.ok) {
            std::cerr << "SOURCE INTAKE FAILED\n\n" << result.error << '\n';
            return 1;
        }

        if (wants_pretty) {
            // Raw regenerated ArcoBASIC source, no status banner -- meant to be piped straight
            // into another `reveal ... at AST` for round-trip comparison, not read as a report.
            std::cout << result.output;
            return 0;
        }

        std::cout << "SOURCE ACCEPTED\n";
        std::cout << "STRUCTURE ASSEMBLED\n";
        if (wants_amir) {
            std::cout << "A-MIR GENERATED\n";
        }
        if (wants_bytecode) {
            std::cout << "BYTECODE PREPARED\n";
        }
        if (wants_callconv) {
            std::cout << "CALLING CONVENTION COMPUTED\n";
        }
        if (wants_x86_64) {
            std::cout << "X86_64 GENERATED\n";
        }
        std::cout << '\n';
        std::cout << result.output;
        return 0;
    }

    if (argc >= 5 && lowercase(argv[1]) == "build" && std::string(argv[3]) == "-o") {
        const std::string output_path = argv[4];
        std::string target;
        std::string entry_function = "Main";
        std::optional<std::size_t> instruction_limit = default_unlimited_instruction_limit();
        for (int i = 5; i + 1 < argc; i += 2) {
            if (lowercase(argv[i]) == "--target") {
                target = lowercase(argv[i + 1]);
            } else if (lowercase(argv[i]) == "--entry") {
                entry_function = argv[i + 1];
            } else if (lowercase(argv[i]) == "--instruction-limit") {
                try {
                    instruction_limit = parse_instruction_limit(argv[i + 1]);
                } catch (const std::exception& error) {
                    std::cerr << "ArcoFission: " << error.what() << '\n';
                    return 2;
                }
            }
        }
        if (target == "uefi-x86_64" || target == "uefi-x86-64") {
            const auto result = arco::fission::build_efi_image_file(argv[2], entry_function, output_path);
            if (!result.ok) {
                std::cerr << "EFI BUILD FAILED\n\n" << result.error << '\n';
                return 1;
            }
            std::cout << result.output;
            return 0;
        }
        // Phase 1 of the native (no-bytecode-VM) Linux backend -- see
        // .agents/reports/ARCO_NATIVE_COMPILER_BACKEND_PLAN.md. A distinct opt-in --target value
        // rather than the default `build`/`native` behavior, which stays exactly the ELF64
        // bytecode-VM-embedding capsule format it always has been -- this is a separate, narrower,
        // still-experimental output shape, not a replacement for it.
        if (target == "linux-x86_64" || target == "linux-x86-64") {
            const auto result = arco::fission::build_linux_native_image_file(argv[2], entry_function, output_path);
            if (!result.ok) {
                std::cerr << "LINUX NATIVE BUILD FAILED\n\n" << result.error << '\n';
                return 1;
            }
            std::cout << result.output;
            return 0;
        }
        if (ends_with(lowercase(output_path), ".arcof")) {
            return write_bytecode_file(argv[2], output_path);
        }
        return write_native_file(argv[2], output_path, instruction_limit, target);
    }

    if (argc == 5 && lowercase(argv[1]) == "bytecode" && std::string(argv[3]) == "-o") {
        return write_bytecode_file(argv[2], argv[4]);
    }

    if (argc >= 5 && lowercase(argv[1]) == "native" && std::string(argv[3]) == "-o") {
        std::optional<std::size_t> instruction_limit = default_unlimited_instruction_limit();
        std::string target;
        for (int i = 5; i + 1 < argc; i += 2) {
            if (lowercase(argv[i]) == "--target") {
                target = lowercase(argv[i + 1]);
            } else if (lowercase(argv[i]) == "--instruction-limit") {
                try {
                    instruction_limit = parse_instruction_limit(argv[i + 1]);
                } catch (const std::exception& error) {
                    std::cerr << "ArcoFission: " << error.what() << '\n';
                    return 2;
                }
            } else {
                std::cerr << "ArcoFission: unrecognized native option " << argv[i] << '\n';
                return 2;
            }
        }
        return write_native_file(argv[2], argv[4], instruction_limit, target);
    }

    if ((argc == 3 || argc == 5) && lowercase(argv[1]) == "run") {
        std::optional<std::size_t> instruction_limit;
        try { instruction_limit = hosted_instruction_limit_or_unlimited(argc, argv, 3); }
        catch (const std::exception& error) { std::cerr << "ArcoFission: " << error.what() << '\n'; return 2; }
        const auto result = arco::fission::run_bytecode_file(argv[2], instruction_limit);
        if (!result.ok) {
            std::cerr << "BYTECODE RUN FAILED\n\n" << result.error << '\n';
            return 1;
        }
        std::cout << result.output;
        return 0;
    }

    if ((argc == 3 || argc == 5) && lowercase(argv[1]) == "compile-run") {
        std::optional<std::size_t> instruction_limit;
        try { instruction_limit = hosted_instruction_limit_or_unlimited(argc, argv, 3); }
        catch (const std::exception& error) { std::cerr << "ArcoFission: " << error.what() << '\n'; return 2; }
        const auto result = arco::fission::compile_run_file(argv[2], instruction_limit);
        if (!result.ok) {
            std::cerr << "BYTECODE RUN FAILED\n\n" << result.error << '\n';
            return 1;
        }
        std::cout << result.output;
        return 0;
    }

    print_usage(std::cerr);
    return 2;
}
