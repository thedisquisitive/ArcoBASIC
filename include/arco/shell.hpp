#pragma once

#include "arco/runtime.hpp"

#include <iosfwd>
#include <string>

namespace arco::shell {

void register_shell_builtins(Runtime& runtime);
RunResult run_file(Runtime& runtime, const std::string& path);
RunResult run_file(Runtime& runtime, const std::string& path, const std::vector<std::string>& args);
RunResult load_startup(Runtime& runtime, std::ostream& output, bool login);
RunResult init_profile(std::ostream& output);
int doctor(std::ostream& output);
RunResult run_tutorial(Runtime& runtime, std::istream& input, std::ostream& output, const std::string& topic);
int run_command_once(Runtime& runtime, const std::string& command, std::ostream& output);
int repl(Runtime& runtime, std::istream& input, std::ostream& output, bool interactive);
std::string help_text(const std::string& topic);
Value help_topics();
void set_color_enabled(bool enabled);
bool color_enabled();
std::string colorize(const std::string& text, const std::string& color);

} // namespace arco::shell
