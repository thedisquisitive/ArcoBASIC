#pragma once

#include <string>

namespace arco::fission {

struct Result {
    bool ok = true;
    std::string output;
    std::string error;
};

Result reveal_amir(const std::string& source, const std::string& source_name);
Result reveal_amir_file(const std::string& path);
Result reveal_bytecode(const std::string& source, const std::string& source_name);
Result reveal_bytecode_file(const std::string& path);
Result run_bytecode(const std::string& bytecode);
Result run_bytecode_file(const std::string& path);
Result compile_run(const std::string& source, const std::string& source_name);
Result compile_run_file(const std::string& path);
Result build_native_file(const std::string& path, const std::string& output_path);
Result reveal_ast(const std::string& source, const std::string& source_name);
Result reveal_ast_file(const std::string& path);
Result reveal_callconv(const std::string& source, const std::string& source_name);
Result reveal_callconv_file(const std::string& path);
Result reveal_x86_64(const std::string& source, const std::string& source_name, const std::string& entry_function);
Result reveal_x86_64_file(const std::string& path, const std::string& entry_function);
Result build_efi_image(const std::string& source, const std::string& source_name, const std::string& entry_function,
                        const std::string& output_path);
Result build_efi_image_file(const std::string& path, const std::string& entry_function, const std::string& output_path);

} // namespace arco::fission
