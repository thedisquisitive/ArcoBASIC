#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

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
Result run_bytecode(const std::string& bytecode,
                    std::optional<std::size_t> instruction_limit_override = std::nullopt);
Result run_bytecode_binary(const std::string& bytecode,
                           std::optional<std::size_t> instruction_limit_override = std::nullopt,
                           const std::vector<std::string>& script_args = {});
// Same as run_bytecode_binary (Args global, self-compile-run registration, ExitSignal handling)
// but for the plain-text `.arcof-text` format instead of the compact binary format -- what a
// native_launcher_source(..., text_format=true) capsule (see build_native_bytecode_file) actually
// calls at startup.
Result run_bytecode_text(const std::string& bytecode,
                         std::optional<std::size_t> instruction_limit_override = std::nullopt,
                         const std::vector<std::string>& script_args = {});
Result run_bytecode_file(const std::string& path,
                         std::optional<std::size_t> instruction_limit_override = std::nullopt);
Result compile_run(const std::string& source, const std::string& source_name,
                   std::optional<std::size_t> instruction_limit_override = std::nullopt);
Result compile_run_file(const std::string& path,
                        std::optional<std::size_t> instruction_limit_override = std::nullopt);
Result build_native_file(const std::string& path, const std::string& output_path,
                         std::optional<std::size_t> instruction_limit_override = std::nullopt,
                         const std::string& target = "");
// Packages an *existing* `.arcof-text` bytecode file (the same plain-text format `ArcoFission
// bytecode FILE -o OUT.arcof`/`reveal ... at BYTECODE` produce and `ArcoFission run` accepts --
// notably including bytecode produced entirely outside this legacy compiler, e.g. by the Fission
// Compiler Substrate's own SIR->A-MIR->bytecode pipeline) into a standalone Linux ELF64 capsule,
// without recompiling any ArcoBASIC source. Reuses the exact same bytecode-VM-embedding launcher
// machinery `build_native_file` uses for a from-source native build, just fed the caller's own
// pre-built bytecode text instead of text this file produced itself from source.
Result build_native_bytecode_file(const std::string& bytecode_path, const std::string& output_path,
                                  std::optional<std::size_t> instruction_limit_override = std::nullopt);
Result reveal_ast(const std::string& source, const std::string& source_name);
Result reveal_ast_file(const std::string& path);
Result reveal_pretty(const std::string& source, const std::string& source_name);
Result reveal_pretty_file(const std::string& path);
Result reveal_callconv(const std::string& source, const std::string& source_name);
Result reveal_callconv_file(const std::string& path);
Result reveal_x86_64(const std::string& source, const std::string& source_name, const std::string& entry_function);
Result reveal_x86_64_file(const std::string& path, const std::string& entry_function);
Result build_efi_image(const std::string& source, const std::string& source_name, const std::string& entry_function,
                        const std::string& output_path);
Result build_efi_image_file(const std::string& path, const std::string& entry_function, const std::string& output_path);
// The Arco native debugger tooling (`ArcoFission build ... --target linux-x86_64 --debug`/
// `--sanitize`): `annotate` makes the generated assembly carry a `# [Function] %tN := ...` comment
// (the exact same rendering `reveal amir` produces) immediately before the bytes each AMIR
// instruction generated, and keeps that .s file (as `<output>.s`) instead of deleting it with the
// rest of the build's temp directory -- so a raw crash address from gdb/objdump maps straight back
// to the AMIR instruction and source line responsible, no manual byte-decoding required. `sanitize`
// adds `-fsanitize=address -g` to the underlying compiler invocation. Both default off and change
// nothing about an ordinary build: `annotate` costs a little codegen-time bookkeeping and a larger
// .s file, `sanitize` costs real runtime overhead, neither belongs in the default path.
struct NativeDebugOptions {
    bool annotate = false;
    bool sanitize = false;
};

Result build_linux_native_image(const std::string& source, const std::string& source_name,
                                 const std::string& entry_function, const std::string& output_path,
                                 NativeDebugOptions debug_options = {});
Result build_linux_native_image_file(const std::string& path, const std::string& entry_function, const std::string& output_path,
                                      NativeDebugOptions debug_options = {});

} // namespace arco::fission
