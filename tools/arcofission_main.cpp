#include "arco/fission.hpp"

#include <fstream>
#include <iostream>
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
        << "  ArcoFission bytecode FILE -o OUT.arcof\n"
        << "  ArcoFission native FILE -o OUT\n"
        << "  ArcoFission run FILE.arcof\n"
        << "  ArcoFission compile-run FILE\n"
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

int write_native_file(const std::string& source_path, const std::string& output_path) {
    const auto result = arco::fission::build_native_file(source_path, output_path);
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

        if (!wants_ast && !wants_amir && !wants_bytecode && !wants_callconv && !wants_x86_64) {
            std::cerr << "ArcoFission: this alpha slice can reveal AST, A-MIR, BYTECODE, CALLCONV, or X86_64\n";
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
                                                                                       : arco::fission::reveal_amir_file(file);
        if (!result.ok) {
            std::cerr << "SOURCE INTAKE FAILED\n\n" << result.error << '\n';
            return 1;
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
        for (int i = 5; i + 1 < argc; i += 2) {
            if (lowercase(argv[i]) == "--target") {
                target = lowercase(argv[i + 1]);
            } else if (lowercase(argv[i]) == "--entry") {
                entry_function = argv[i + 1];
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
        if (ends_with(lowercase(output_path), ".arcof")) {
            return write_bytecode_file(argv[2], output_path);
        }
        return write_native_file(argv[2], output_path);
    }

    if (argc == 5 && lowercase(argv[1]) == "bytecode" && std::string(argv[3]) == "-o") {
        return write_bytecode_file(argv[2], argv[4]);
    }

    if (argc == 5 && lowercase(argv[1]) == "native" && std::string(argv[3]) == "-o") {
        return write_native_file(argv[2], argv[4]);
    }

    if (argc == 3 && lowercase(argv[1]) == "run") {
        const auto result = arco::fission::run_bytecode_file(argv[2]);
        if (!result.ok) {
            std::cerr << "BYTECODE RUN FAILED\n\n" << result.error << '\n';
            return 1;
        }
        std::cout << result.output;
        return 0;
    }

    if (argc == 3 && lowercase(argv[1]) == "compile-run") {
        const auto result = arco::fission::compile_run_file(argv[2]);
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
