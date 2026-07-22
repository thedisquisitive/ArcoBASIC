#include "arco/shell.hpp"
#include "arco/gui.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
extern char** _environ;
#else
extern char** environ;
#endif

namespace arco::shell {

namespace {

bool g_color_enabled = true;
std::filesystem::path g_previous_directory;
std::map<std::string, std::string> g_aliases;

struct ShellJob {
    int id = 0;
    int pid = 0;
    std::string command;
    bool running = true;
    int status = 0;
    int signal = 0;
};

std::vector<ShellJob> g_jobs;
int g_next_job_id = 1;

void ignore_sigint() {
#ifndef _WIN32
    struct sigaction action {};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
#endif
}

void restore_default_sigint() {
#ifndef _WIN32
    struct sigaction action {};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
#endif
}

bool set_environment_variable(const std::string& name, const std::string& value);
const std::map<std::string, std::string>& help_catalog();
bool is_arcosh_script_extension(const std::filesystem::path& path);
std::vector<std::filesystem::path> stdlib_search_directories();
std::vector<std::string> split_shell_words(const std::string& text);
std::optional<std::filesystem::path> executable_directory();
std::optional<std::filesystem::path> executable_path();

const std::map<std::string, std::string>& color_codes() {
    static const std::map<std::string, std::string> codes = {
        {"black", "30"},
        {"red", "31"},
        {"green", "32"},
        {"yellow", "33"},
        {"blue", "34"},
        {"magenta", "35"},
        {"cyan", "36"},
        {"white", "37"},
        {"brightblack", "90"},
        {"gray", "90"},
        {"grey", "90"},
        {"brightred", "91"},
        {"brightgreen", "92"},
        {"brightyellow", "93"},
        {"brightblue", "94"},
        {"brightmagenta", "95"},
        {"brightcyan", "96"},
        {"brightwhite", "97"},
        {"bold", "1"},
        {"dim", "2"},
        {"reset", "0"}
    };
    return codes;
}

std::string lowercase(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string uppercase(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string repeat_char(char c, std::size_t count) {
    return std::string(count, c);
}

std::string pad_right(const std::string& text, std::size_t width) {
    if (text.size() >= width) {
        return text;
    }
    return text + repeat_char(' ', width - text.size());
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    if (!text.empty() && text.back() == '\n') {
        lines.emplace_back();
    }
    return lines;
}

std::vector<std::string> wrap_line(const std::string& line, std::size_t width) {
    std::vector<std::string> wrapped;
    if (width == 0) {
        wrapped.push_back(line);
        return wrapped;
    }
    std::string remaining = line;
    while (remaining.size() > width) {
        std::size_t split = remaining.rfind(' ', width);
        if (split == std::string::npos || split == 0) {
            split = width;
        }
        wrapped.push_back(remaining.substr(0, split));
        remaining = trim(remaining.substr(split));
    }
    wrapped.push_back(remaining);
    return wrapped;
}

std::vector<std::string> wrap_lines(const std::string& text, std::size_t width) {
    std::vector<std::string> wrapped;
    for (const auto& line : split_lines(text)) {
        for (const auto& item : wrap_line(line, width)) {
            wrapped.push_back(item);
        }
    }
    if (wrapped.empty()) {
        wrapped.emplace_back();
    }
    return wrapped;
}

struct TuiTheme {
    std::string name;
    char top_left = '+';
    char top_right = '+';
    char bottom_left = '+';
    char bottom_right = '+';
    char horizontal = '-';
    char vertical = '|';
    char fill = ' ';
    std::string bullet = "*";
};

const std::map<std::string, TuiTheme>& tui_themes() {
    static const std::map<std::string, TuiTheme> themes = {
        {"ascii", {"ascii", '+', '+', '+', '+', '-', '|', ' ', "*"}},
        {"terminal", {"terminal", '[', ']', '[', ']', '=', '|', ' ', ">"}},
        {"double", {"double", '#', '#', '#', '#', '=', '#', ' ', "#"}},
        {"scroll", {"scroll", '/', '\\', '\\', '/', '~', '|', ' ', "~"}},
        {"parchment", {"parchment", '(', ')', '(', ')', '~', ':', '.', "~"}},
        {"circuit", {"circuit", '<', '>', '<', '>', '=', '!', ' ', "+"}},
        {"minimal", {"minimal", ' ', ' ', ' ', ' ', '-', '|', ' ', "-"}}
    };
    return themes;
}

const TuiTheme& tui_theme(const std::string& name) {
    const std::string key = lowercase(trim(name));
    const auto& themes = tui_themes();
    const auto found = themes.find(key.empty() ? "ascii" : key);
    if (found != themes.end()) {
        return found->second;
    }
    return themes.at("ascii");
}

std::string tui_rule_with_theme(const std::string& title, std::size_t width, const TuiTheme& theme) {
    const std::string clean_title = trim(title);
    if (clean_title.empty()) {
        return std::string(1, theme.top_left) + repeat_char(theme.horizontal, width) + std::string(1, theme.top_right);
    }
    const std::string label = " " + clean_title + " ";
    if (label.size() >= width) {
        return std::string(1, theme.top_left) + " " + clean_title + " " + std::string(1, theme.top_right);
    }
    const std::size_t left = (width - label.size()) / 2;
    const std::size_t right = width - label.size() - left;
    return std::string(1, theme.top_left) + repeat_char(theme.horizontal, left) + label + repeat_char(theme.horizontal, right) + std::string(1, theme.top_right);
}

std::string tui_rule(const std::string& title, std::size_t width = 72) {
    return tui_rule_with_theme(title, width, tui_theme("ascii"));
}

std::string tui_box_with_theme(const std::string& title, const std::string& body, const TuiTheme& theme) {
    std::vector<std::string> lines = split_lines(body);
    if (lines.empty()) {
        lines.emplace_back();
    }

    std::size_t width = std::max<std::size_t>(48, trim(title).size() + 6);
    for (const auto& line : lines) {
        width = std::max(width, line.size() + 2);
    }
    width = std::min<std::size_t>(width, 96);
    lines = wrap_lines(body, width - 2);

    std::ostringstream output;
    output << tui_rule_with_theme(title, width, theme) << '\n';
    for (const auto& line : lines) {
        output << theme.vertical << " " << line << repeat_char(theme.fill, width - line.size() - 1) << theme.vertical << "\n";
    }
    output << theme.bottom_left << repeat_char(theme.horizontal, width) << theme.bottom_right << "\n";
    return output.str();
}

std::string tui_box(const std::string& title, const std::string& body) {
    return tui_box_with_theme(title, body, tui_theme("ascii"));
}

std::string tui_scroll(const std::string& title, const std::string& body) {
    std::vector<std::string> lines = wrap_lines(body, 66);
    std::size_t width = std::max<std::size_t>(48, trim(title).size() + 10);
    for (const auto& line : lines) {
        width = std::max(width, line.size() + 4);
    }
    width = std::min<std::size_t>(width, 78);
    lines = wrap_lines(body, width - 4);

    std::ostringstream output;
    output << "  .-" << repeat_char('~', width - 4) << "-.\n";
    output << " / " << pad_right(trim(title), width - 4) << " \\\n";
    output << "( " << repeat_char('-', width - 4) << " )\n";
    for (const auto& line : lines) {
        output << "|  " << pad_right(line, width - 4) << "  |\n";
    }
    output << "( " << repeat_char('-', width - 4) << " )\n";
    output << " \\_" << repeat_char('~', width - 4) << "_/\n";
    return output.str();
}

std::string tui_badge(const std::string& state) {
    return "[" + uppercase(trim(state)) + "]";
}

std::string tui_status(const std::string& label, const std::string& state, const std::string& detail) {
    std::ostringstream output;
    output << tui_badge(state) << " " << label;
    if (!trim(detail).empty()) {
        output << " - " << detail;
    }
    return output.str();
}

std::string tui_progress(const std::string& label, double current, double total, std::size_t width) {
    if (total <= 0) {
        total = 1;
    }
    if (width < 8) {
        width = 8;
    }
    const double ratio = std::max(0.0, std::min(1.0, current / total));
    const std::size_t filled = static_cast<std::size_t>(ratio * static_cast<double>(width));
    const int percent = static_cast<int>(ratio * 100.0 + 0.5);
    std::ostringstream output;
    if (!trim(label).empty()) {
        output << label << " ";
    }
    output << "[" << repeat_char('#', filled) << repeat_char('.', width - filled) << "] " << percent << "%";
    return output.str();
}

std::string tui_list(const std::string& title, const Value::Array& items, bool numbered) {
    std::ostringstream body;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            body << '\n';
        }
        if (numbered) {
            body << (i + 1) << ". ";
        } else {
            body << "* ";
        }
        body << items[i].to_string();
    }
    return tui_box(title, body.str());
}

std::string tui_key_values(const std::string& title, const Value::Object& object) {
    std::size_t key_width = 0;
    for (const auto& [key, value] : object) {
        (void)value;
        key_width = std::max(key_width, key.size());
    }
    std::ostringstream body;
    bool first = true;
    for (const auto& [key, value] : object) {
        if (!first) {
            body << '\n';
        }
        first = false;
        body << pad_right(key, key_width) << " : " << value.to_string();
    }
    return tui_box(title, body.str());
}

std::string table_cell(const Value& row, const std::string& header, std::size_t index) {
    if (row.is_array()) {
        const auto& array = row.as_array();
        if (index < array.size()) {
            return array[index].to_string();
        }
        return "";
    }
    if (row.is_object()) {
        const auto& object = row.as_object();
        const auto found = object.find(header);
        if (found != object.end()) {
            return found->second.to_string();
        }
        return "";
    }
    return index == 0 ? row.to_string() : "";
}

std::string tui_table(const Value::Array& headers, const Value::Array& rows) {
    std::vector<std::string> names;
    std::vector<std::size_t> widths;
    for (const auto& header : headers) {
        names.push_back(header.to_string());
        widths.push_back(header.to_string().size());
    }
    for (const auto& row : rows) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            widths[i] = std::max(widths[i], table_cell(row, names[i], i).size());
        }
    }

    auto border = [&]() {
        std::ostringstream line;
        line << '+';
        for (const auto width : widths) {
            line << repeat_char('-', width + 2) << '+';
        }
        return line.str();
    };

    std::ostringstream output;
    output << border() << '\n';
    output << '|';
    for (std::size_t i = 0; i < names.size(); ++i) {
        output << ' ' << pad_right(names[i], widths[i]) << " |";
    }
    output << '\n' << border() << '\n';
    for (const auto& row : rows) {
        output << '|';
        for (std::size_t i = 0; i < names.size(); ++i) {
            output << ' ' << pad_right(table_cell(row, names[i], i), widths[i]) << " |";
        }
        output << '\n';
    }
    output << border() << '\n';
    return output.str();
}

Value tui_parse_event(const std::string& sequence) {
    Value::Object event{{"Type", "unknown"}, {"Raw", sequence}};
    if (sequence.empty()) {
        event["Type"] = "eof";
        return event;
    }

    // SGR extended mouse reporting: CSI < button ; column ; row M/m.
    if (sequence.size() >= 7 && sequence.compare(0, 3, "\033[<") == 0 &&
        (sequence.back() == 'M' || sequence.back() == 'm')) {
        const std::string fields = sequence.substr(3, sequence.size() - 4);
        const std::size_t first = fields.find(';');
        const std::size_t second = first == std::string::npos ? first : fields.find(';', first + 1);
        try {
            if (first == std::string::npos || second == std::string::npos) {
                return event;
            }
            const int code = std::stoi(fields.substr(0, first));
            const int x = std::stoi(fields.substr(first + 1, second - first - 1));
            const int y = std::stoi(fields.substr(second + 1));
            const bool wheel = (code & 64) != 0;
            const bool motion = (code & 32) != 0;
            const int button_code = code & 3;
            std::string button = "none";
            if (wheel) {
                button = button_code == 0 ? "wheel-up" : "wheel-down";
            } else if (button_code == 0) {
                button = "left";
            } else if (button_code == 1) {
                button = "middle";
            } else if (button_code == 2) {
                button = "right";
            }
            event = Value::Object{
                {"Type", "mouse"}, {"Action", wheel ? "scroll" : (sequence.back() == 'm' ? "release" : (motion ? "drag" : "press"))},
                {"Button", button}, {"X", x}, {"Y", y},
                {"Shift", (code & 4) != 0}, {"Alt", (code & 8) != 0}, {"Ctrl", (code & 16) != 0}, {"Raw", sequence}};
            return event;
        } catch (const std::exception&) {
            return event;
        }
    }

    event["Type"] = "key";
    if (sequence.size() == 1 && sequence[0] == 3) {
        event["Key"] = "ctrl-c";
    } else if (sequence == "\r" || sequence == "\n") {
        event["Key"] = "enter";
    } else if (sequence == "\033") {
        event["Key"] = "escape";
    } else {
        event["Key"] = sequence;
    }
    return event;
}

bool starts_basic_statement(const std::string& line) {
    const std::string text = trim(line);
    const auto space = text.find_first_of(" \t(");
    const std::string first = lowercase(text.substr(0, space == std::string::npos ? std::string::npos : space));
    return first == "print" || first == "let" || first == "if" || first == "while" ||
           first == "for" || first == "run" || first == "next" || first == "wend" ||
           first == "else" || first == "end" || first == "flags" || first == "function" ||
           first == "class" || first == "interface" || first == "abstract" || first == "return" || first == "try" || first == "catch";
}

bool inline_if_statement(const std::string& lower_line) {
    if (lower_line.rfind("if ", 0) != 0) {
        return false;
    }
    const auto then_pos = lower_line.find(" then");
    if (then_pos == std::string::npos) {
        return false;
    }
    return !trim(lower_line.substr(then_pos + 5)).empty();
}

bool starts_multiline_block(const std::string& line) {
    const std::string lower = lowercase(trim(line));
    if (lower.empty()) {
        return false;
    }
    if (lower.rfind("if ", 0) == 0) {
        return !inline_if_statement(lower);
    }
    return lower.rfind("while ", 0) == 0 || lower.rfind("for ", 0) == 0 ||
           lower.rfind("function ", 0) == 0 || lower == "try" || lower.rfind("try ", 0) == 0 ||
           lower.rfind("flags ", 0) == 0 || lower.rfind("class ", 0) == 0 || lower.rfind("interface ", 0) == 0;
}

bool ends_multiline_block(const std::string& line) {
    const std::string lower = lowercase(trim(line));
    return lower == "wend" || lower.rfind("next", 0) == 0 || lower == "end if" ||
           lower == "end function" || lower == "end class" || lower == "end interface" || lower == "end try" || lower == "end flags";
}

int multiline_delta(const std::string& line) {
    if (ends_multiline_block(line)) {
        return -1;
    }
    if (starts_multiline_block(line)) {
        return 1;
    }
    return 0;
}

bool is_flag_operation_line(const std::string& line) {
    const std::string text = trim(line);
    const auto first_space = text.find_first_of(" \t");
    if (first_space == std::string::npos) {
        return false;
    }
    const std::string rest = trim(text.substr(first_space + 1));
    const auto second_space = rest.find_first_of(" \t");
    const std::string second = lowercase(rest.substr(0, second_space == std::string::npos ? std::string::npos : second_space));
    return second == "add" || second == "remove" || second == "toggle";
}

bool valid_shell_variable_name(const std::string& name) {
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) {
        return false;
    }
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
            return false;
        }
    }
    return true;
}

bool is_assignment_word(const std::string& word) {
    const auto equals = word.find('=');
    if (equals == std::string::npos || equals == 0) {
        return false;
    }
    return valid_shell_variable_name(word.substr(0, equals));
}

bool starts_with_assignment_prefix(const std::string& line) {
    const auto words = split_shell_words(line);
    if (words.size() < 2 || !is_assignment_word(words.front())) {
        return false;
    }
    std::size_t index = 0;
    while (index < words.size() && is_assignment_word(words[index])) {
        index++;
    }
    return index < words.size();
}

bool should_run_as_shell_command(const std::string& line) {
    const std::string text = trim(line);
    if (starts_with_assignment_prefix(text)) {
        return true;
    }
    return !text.empty() && text.find('=') == std::string::npos && text.find('(') == std::string::npos &&
           !starts_basic_statement(text) && !is_flag_operation_line(text);
}

std::optional<std::pair<int, std::string>> parse_numbered_line(const std::string& line) {
    const std::string text = trim(line);
    if (text.empty() || !std::isdigit(static_cast<unsigned char>(text[0]))) {
        return std::nullopt;
    }
    std::size_t index = 0;
    while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
        index++;
    }
    if (index >= text.size() || !std::isspace(static_cast<unsigned char>(text[index]))) {
        return std::nullopt;
    }
    return std::make_pair(std::stoi(text.substr(0, index)), trim(text.substr(index + 1)));
}

std::string stored_program_source(const std::map<int, std::string>& program) {
    std::ostringstream source;
    for (const auto& [number, code] : program) {
        if (!code.empty()) {
            source << number << ' ' << code << '\n';
        }
    }
    return source.str();
}

bool starts_with_word(const std::string& line, const std::string& word) {
    const std::string text = trim(line);
    if (text == word) {
        return true;
    }
    return text.rfind(word + " ", 0) == 0;
}

std::string first_shell_word(const std::string& command) {
    const std::string text = trim(command);
    if (text.empty()) {
        return "";
    }
    const auto space = text.find_first_of(" \t");
    return text.substr(0, space == std::string::npos ? std::string::npos : space);
}

std::string shell_args_after_first_word(const std::string& command) {
    const std::string text = trim(command);
    const auto space = text.find_first_of(" \t");
    if (space == std::string::npos) {
        return "";
    }
    return trim(text.substr(space + 1));
}

std::vector<std::string> split_shell_words(const std::string& text) {
    std::vector<std::string> words;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    bool escaping = false;
    for (char c : text) {
        if (escaping) {
            current.push_back(c);
            escaping = false;
            continue;
        }
        if (c == '\\' && !in_single) {
            escaping = true;
            continue;
        }
        if (c == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (c == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) && !in_single && !in_double) {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) {
        words.push_back(current);
    }
    return words;
}

bool has_unquoted_trailing_background_marker(const std::string& text) {
    bool in_single = false;
    bool in_double = false;
    bool escaping = false;
    std::size_t marker = std::string::npos;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (escaping) {
            escaping = false;
            continue;
        }
        if (c == '\\' && !in_single) {
            escaping = true;
            continue;
        }
        if (c == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (c == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        if (c == '&' && !in_single && !in_double) {
            marker = i;
        }
    }
    if (marker == std::string::npos) {
        return false;
    }
    for (std::size_t i = marker + 1; i < text.size(); ++i) {
        if (!std::isspace(static_cast<unsigned char>(text[i]))) {
            return false;
        }
    }
    return true;
}

std::string remove_background_marker(const std::string& text) {
    std::string result = text;
    const auto ampersand = result.find_last_of('&');
    if (ampersand != std::string::npos) {
        result.erase(ampersand);
    }
    return trim(result);
}

std::string command_status_text(int status, int signal) {
    if (signal > 0) {
        return "signal=" + std::to_string(signal);
    }
    return "status=" + std::to_string(status);
}

int status_from_wait_status(int wait_status) {
#ifdef _WIN32
    return wait_status;
#else
    if (WIFEXITED(wait_status)) {
        return WEXITSTATUS(wait_status);
    }
    if (WIFSIGNALED(wait_status)) {
        return 128 + WTERMSIG(wait_status);
    }
    return wait_status;
#endif
}

int signal_from_wait_status(int wait_status) {
#ifdef _WIN32
    (void)wait_status;
    return 0;
#else
    return WIFSIGNALED(wait_status) ? WTERMSIG(wait_status) : 0;
#endif
}

std::string join_words(const std::vector<std::string>& words, std::size_t start) {
    std::ostringstream output;
    for (std::size_t i = start; i < words.size(); ++i) {
        if (i > start) {
            output << ' ';
        }
        output << words[i];
    }
    return output.str();
}

std::vector<std::filesystem::path> path_directories() {
    std::vector<std::filesystem::path> directories;
    const char* path_env = std::getenv("PATH");
    if (!path_env) {
        return directories;
    }
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::istringstream input(path_env);
    std::string part;
    while (std::getline(input, part, separator)) {
        if (!part.empty()) {
            directories.emplace_back(part);
        }
    }
    return directories;
}

std::optional<std::filesystem::path> find_external_command(const std::string& command) {
    if (command.find('/') != std::string::npos || command.find('\\') != std::string::npos) {
        const std::filesystem::path path(command);
        if (std::filesystem::exists(path) && !std::filesystem::is_directory(path)) {
            return path;
        }
        return std::nullopt;
    }
    for (const auto& directory : path_directories()) {
        const auto candidate = directory / command;
        if (std::filesystem::exists(candidate) && !std::filesystem::is_directory(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::string expand_alias(const std::string& line) {
    const std::string command = first_shell_word(line);
    const auto found = g_aliases.find(command);
    if (found == g_aliases.end()) {
        return line;
    }
    const std::string rest = shell_args_after_first_word(line);
    return found->second + (rest.empty() ? "" : " " + rest);
}

bool shell_variable_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string shell_variable_value(const std::string& name, int last_status) {
    if (name == "?") {
        return std::to_string(last_status);
    }
    const char* value = std::getenv(name.c_str());
    return value ? value : "";
}

std::string expand_shell_variables(const std::string& text, int last_status) {
    std::string output;
    bool in_single = false;
    bool in_double = false;
    bool escaping = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (escaping) {
            output.push_back(c);
            escaping = false;
            continue;
        }
        if (c == '\\' && !in_single) {
            escaping = true;
            continue;
        }
        if (c == '\'' && !in_double) {
            in_single = !in_single;
            output.push_back(c);
            continue;
        }
        if (c == '"' && !in_single) {
            in_double = !in_double;
            output.push_back(c);
            continue;
        }
        if (c != '$' || in_single) {
            output.push_back(c);
            continue;
        }

        if (i + 1 >= text.size()) {
            output.push_back(c);
            continue;
        }
        if (text[i + 1] == '?') {
            output += shell_variable_value("?", last_status);
            ++i;
            continue;
        }
        if (text[i + 1] == '{') {
            const auto end = text.find('}', i + 2);
            if (end == std::string::npos) {
                output.push_back(c);
                continue;
            }
            output += shell_variable_value(text.substr(i + 2, end - (i + 2)), last_status);
            i = end;
            continue;
        }
        if (!shell_variable_char(text[i + 1])) {
            output.push_back(c);
            continue;
        }
        std::size_t end = i + 1;
        while (end < text.size() && shell_variable_char(text[end])) {
            end++;
        }
        output += shell_variable_value(text.substr(i + 1, end - (i + 1)), last_status);
        i = end - 1;
    }
    if (escaping) {
        output.push_back('\\');
    }
    return output;
}

std::string home_directory() {
    if (const char* home = std::getenv("HOME")) {
        return home;
    }
    if (const char* user_profile = std::getenv("USERPROFILE")) {
        return user_profile;
    }
    return "";
}

std::filesystem::path arcosh_home_directory() {
    if (const char* override_home = std::getenv("ARCOSH_HOME")) {
        if (*override_home) {
            return override_home;
        }
    }
    const std::string home = home_directory();
    if (home.empty()) {
        return ".arcosh";
    }
    return std::filesystem::path(home) / ".arcosh";
}

std::filesystem::path arcosh_plugins_directory() {
    return arcosh_home_directory() / "plugins";
}

std::filesystem::path arcosh_mods_directory() {
    return arcosh_home_directory() / "mods";
}

std::filesystem::path arcosh_mods_enabled_file() {
    return arcosh_mods_directory() / "enabled.txt";
}

std::filesystem::path arcosh_scripts_directory() {
    return arcosh_home_directory() / "scripts";
}

std::filesystem::path runtime_assets_directory() {
    std::vector<std::filesystem::path> candidates;
    candidates.emplace_back(std::filesystem::current_path() / "assets");
    if (const auto executable = executable_directory()) {
        candidates.emplace_back(executable->parent_path() / "assets");
        candidates.emplace_back(executable->parent_path() / "share" / "arcobasic" / "assets");
        candidates.emplace_back(*executable / ".." / "share" / "arcobasic" / "assets");
    }
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_directory(candidate, error)) return std::filesystem::weakly_canonical(candidate, error);
    }
    return candidates.front();
}

std::filesystem::path arcosh_history_file() {
    return arcosh_home_directory() / "history";
}

std::optional<std::filesystem::path> executable_path() {
#ifndef _WIN32
    std::array<char, 4096> path{};
    const ssize_t length = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (length <= 0) {
        return std::nullopt;
    }
    path[static_cast<std::size_t>(length)] = '\0';
    return std::filesystem::path(path.data());
#else
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path> executable_directory() {
    if (const auto path = executable_path()) {
        return path->parent_path();
    }
    return std::nullopt;
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
}

void add_candidate(std::vector<std::string>& candidates, const std::string& candidate) {
    if (!candidate.empty()) {
        candidates.push_back(candidate);
    }
}

void sort_unique(std::vector<std::string>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

std::string common_prefix(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "";
    }
    std::string prefix = values.front();
    for (const auto& value : values) {
        while (!prefix.empty() && !starts_with(value, prefix)) {
            prefix.pop_back();
        }
    }
    return prefix;
}

std::vector<std::string> shell_builtin_commands() {
    return {
        "alias", "bg", "cd", "cls", "color", "complete", "disown", "env", "exit", "export", "fg", "help", "history",
        "install-login", "jobs", "kill", "list", "load", "new", "oops", "pwd", "run", "source", "tutorial", "unalias",
        "unset", "version",
        "PRINT", "LET", "IF", "WHILE", "FOR", "FUNCTION", "TRY", "FLAGS", "STOP", "GOTO"
    };
}

void prepend_path_environment(const std::filesystem::path& directory) {
    const std::string dir = directory.string();
    const char* existing = std::getenv("PATH");
    const std::string path = existing ? existing : "";
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    if (path == dir || path.rfind(dir + separator, 0) == 0 || path.find(std::string(1, separator) + dir + separator) != std::string::npos ||
        (path.size() > dir.size() && path.compare(path.size() - dir.size(), dir.size(), dir) == 0 && path[path.size() - dir.size() - 1] == separator)) {
        return;
    }
    set_environment_variable("PATH", path.empty() ? dir : dir + separator + path);
}

void ensure_arcosh_home() {
    const auto home = arcosh_home_directory();
    std::filesystem::create_directories(home);
    std::filesystem::create_directories(arcosh_plugins_directory());
    std::filesystem::create_directories(arcosh_mods_directory());
    std::filesystem::create_directories(arcosh_scripts_directory());
    prepend_path_environment(arcosh_scripts_directory());
}

class ShellHistory {
public:
    void load() {
        entries_.clear();
        std::ifstream input(arcosh_history_file());
        std::string line;
        while (std::getline(input, line)) {
            if (!trim(line).empty()) {
                entries_.push_back(line);
            }
        }
        trim_to_limit();
    }

    void add(const std::string& line) {
        const std::string text = trim(line);
        if (text.empty()) {
            return;
        }
        if (!entries_.empty() && entries_.back() == line) {
            return;
        }
        entries_.push_back(line);
        trim_to_limit();
    }

    void save() const {
        try {
            ensure_arcosh_home();
            std::ofstream output(arcosh_history_file(), std::ios::trunc);
            for (const auto& entry : entries_) {
                output << entry << '\n';
            }
        } catch (const std::exception&) {
        }
    }

    void clear() {
        entries_.clear();
        try {
            std::filesystem::remove(arcosh_history_file());
        } catch (const std::exception&) {
        }
    }

    const std::vector<std::string>& entries() const {
        return entries_;
    }

private:
    void trim_to_limit() {
        constexpr std::size_t max_entries = 2000;
        if (entries_.size() > max_entries) {
            entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - max_entries));
        }
    }

    std::vector<std::string> entries_;
};

std::vector<std::string> profile_script_completions(const std::string& prefix) {
    std::vector<std::string> candidates;
    const auto directory = arcosh_scripts_directory();
    if (!std::filesystem::exists(directory)) {
        return candidates;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto path = entry.path();
        const std::string filename = path.filename().string();
        if (starts_with(filename, prefix)) {
            add_candidate(candidates, filename);
        }
        if (is_arcosh_script_extension(path)) {
            const std::string stem = path.stem().string();
            if (starts_with(stem, prefix)) {
                add_candidate(candidates, stem);
            }
        }
    }
    sort_unique(candidates);
    return candidates;
}

std::vector<std::string> file_completions(const std::string& prefix, bool scripts_only = false) {
    std::vector<std::string> candidates;
    std::string expanded_prefix = prefix;
    std::string visible_prefix;
    if (starts_with(prefix, "~/")) {
        expanded_prefix = (std::filesystem::path(home_directory()) / prefix.substr(2)).string();
        visible_prefix = "~/";
    }

    std::filesystem::path prefix_path(expanded_prefix.empty() ? "." : expanded_prefix);
    std::filesystem::path directory = ".";
    std::string filename_prefix = expanded_prefix;
    std::string replacement_directory;
    if (expanded_prefix.find('/') != std::string::npos || expanded_prefix.find('\\') != std::string::npos) {
        directory = prefix_path.parent_path();
        if (directory.empty()) {
            directory = ".";
        }
        filename_prefix = prefix_path.filename().string();
        replacement_directory = visible_prefix.empty() ? prefix_path.parent_path().string() : visible_prefix;
        if (!replacement_directory.empty() && replacement_directory.back() != '/' && replacement_directory.back() != '\\') {
            replacement_directory += "/";
        }
    }

    if (!std::filesystem::exists(directory)) {
        return candidates;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const std::string name = entry.path().filename().string();
        if (!starts_with(name, filename_prefix)) {
            continue;
        }
        std::string candidate = replacement_directory + name;
        if (entry.is_directory()) {
            candidate += "/";
        } else if (scripts_only && !is_arcosh_script_extension(entry.path())) {
            continue;
        }
        add_candidate(candidates, candidate);
    }
    sort_unique(candidates);
    return candidates;
}

std::vector<std::string> script_launch_completions(const std::string& prefix, bool include_profile_scripts) {
    std::vector<std::string> candidates;
    const bool path_like = prefix.find('/') != std::string::npos || prefix.find('\\') != std::string::npos ||
                           starts_with(prefix, ".") || starts_with(prefix, "~");
    if (include_profile_scripts && !path_like) {
        for (const auto& script : profile_script_completions(prefix)) {
            add_candidate(candidates, script);
        }
    }
    for (const auto& file : file_completions(prefix, true)) {
        add_candidate(candidates, file);
    }
    sort_unique(candidates);
    return candidates;
}

std::vector<std::string> completion_candidates(const std::string& buffer, std::size_t cursor) {
    const std::size_t safe_cursor = std::min(cursor, buffer.size());
    const std::size_t token_start = buffer.find_last_of(" \t", safe_cursor == 0 ? 0 : safe_cursor - 1);
    const std::size_t start = token_start == std::string::npos ? 0 : token_start + 1;
    const std::string before_token = buffer.substr(0, start);
    const std::string token = buffer.substr(start, safe_cursor - start);
    const std::string lower_before = lowercase(trim(before_token));
    const std::string lower_buffer = lowercase(buffer.substr(0, safe_cursor));
    const std::string first_word = lowercase(first_shell_word(buffer.substr(0, safe_cursor)));
    std::vector<std::string> candidates;

    if (lower_buffer == "help " || starts_with(lower_buffer, "help ")) {
        for (const auto& [topic, text] : help_catalog()) {
            (void)text;
            if (starts_with(topic, lowercase(token))) {
                add_candidate(candidates, topic);
            }
        }
        sort_unique(candidates);
        return candidates;
    }

    const bool first_token = trim(before_token).empty();
    if (first_token && starts_with(token, "@")) {
        const std::string launch_prefix = token.substr(1);
        for (const auto& script : script_launch_completions(launch_prefix, true)) {
            add_candidate(candidates, "@" + script);
        }
        sort_unique(candidates);
        return candidates;
    }

    if (!first_token && (first_word == "load" || first_word == "run")) {
        for (const auto& script : script_launch_completions(token, first_word == "run")) {
            add_candidate(candidates, script);
        }
        return candidates;
    }

    const bool path_like = token.find('/') != std::string::npos || token.find('\\') != std::string::npos ||
                           starts_with(token, ".") || starts_with(token, "~");
    if (!first_token || path_like) {
        return file_completions(token);
    }

    for (const auto& command : shell_builtin_commands()) {
        if (starts_with(lowercase(command), lowercase(token))) {
            add_candidate(candidates, command);
        }
    }
    for (const auto& script : profile_script_completions(token)) {
        add_candidate(candidates, script);
    }
    for (const auto& file : file_completions(token)) {
        add_candidate(candidates, file);
    }
    sort_unique(candidates);
    return candidates;
}

#ifndef _WIN32
class RawTerminalGuard {
public:
    RawTerminalGuard() {
        enabled_ = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &original_) == 0;
        if (!enabled_) {
            return;
        }
        termios raw = original_;
        raw.c_lflag &= static_cast<unsigned>(~(ECHO | ICANON | ISIG));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
            enabled_ = false;
        }
    }

    ~RawTerminalGuard() {
        if (enabled_) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original_);
        }
    }

    bool enabled() const {
        return enabled_;
    }

private:
    termios original_{};
    bool enabled_ = false;
};
#endif

Value read_tui_event() {
#ifndef _WIN32
    RawTerminalGuard raw;
    if (!raw.enabled()) {
        return Value::Object{{"Type", "unsupported"}, {"Raw", ""}};
    }

    std::string sequence;
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) {
        return tui_parse_event("");
    }
    sequence.push_back(c);
    if (c != '\033') {
        return tui_parse_event(sequence);
    }

    // Mouse reports are short, terminated by M (press/move) or m (release).
    // The same bounded read also preserves ordinary CSI key sequences.
    for (int i = 0; i < 63; ++i) {
        if (read(STDIN_FILENO, &c, 1) != 1) {
            break;
        }
        sequence.push_back(c);
        if ((sequence.size() >= 3 && sequence.compare(0, 3, "\033[<") == 0 && (c == 'M' || c == 'm')) ||
            (sequence.size() > 2 && sequence.compare(0, 3, "\033[<") != 0 && c >= '@' && c <= '~')) {
            break;
        }
    }
    return tui_parse_event(sequence);
#else
    return Value::Object{{"Type", "unsupported"}, {"Raw", ""}};
#endif
}

class LineEditor {
public:
    LineEditor(std::istream& input, std::ostream& output, ShellHistory& history, bool interactive)
        : input_(input), output_(output), history_(history), interactive_(interactive) {}

    std::optional<std::string> read_line(const std::string& prompt) {
#ifndef _WIN32
        if (interactive_ && &input_ == &std::cin && &output_ == &std::cout && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
            return read_terminal_line(prompt);
        }
#endif
        if (interactive_) {
            output_ << prompt << std::flush;
        }
        std::string line;
        if (!std::getline(input_, line)) {
            return std::nullopt;
        }
        return line;
    }

private:
#ifndef _WIN32
    void redraw(const std::string& prompt, const std::string& buffer, std::size_t cursor) {
        output_ << "\r\033[2K" << prompt << buffer;
        if (buffer.size() > cursor) {
            output_ << "\033[" << (buffer.size() - cursor) << "D";
        }
        output_ << std::flush;
    }

    void apply_completion(const std::string& prompt, std::string& buffer, std::size_t& cursor) {
        const std::size_t token_start = buffer.find_last_of(" \t", cursor == 0 ? 0 : cursor - 1);
        const std::size_t start = token_start == std::string::npos ? 0 : token_start + 1;
        const std::string token = buffer.substr(start, cursor - start);
        const auto candidates = completion_candidates(buffer, cursor);
        if (candidates.empty()) {
            output_ << '\a' << std::flush;
            return;
        }

        const std::string prefix = common_prefix(candidates);
        if (candidates.size() == 1 || prefix.size() > token.size()) {
            const std::string replacement = candidates.size() == 1 ? candidates.front() : prefix;
            buffer.replace(start, cursor - start, replacement);
            cursor = start + replacement.size();
            redraw(prompt, buffer, cursor);
            return;
        }

        output_ << "\r\n";
        constexpr std::size_t columns = 4;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            output_ << candidates[i];
            if ((i + 1) % columns == 0 || i + 1 == candidates.size()) {
                output_ << "\r\n";
            } else {
                output_ << "\t";
            }
        }
        redraw(prompt, buffer, cursor);
    }

    void recall_history(const std::string& prompt, std::string& buffer, std::string& saved_current,
                        std::size_t& cursor, std::size_t& history_index, int direction) {
        const auto& entries = history_.entries();
        if (direction < 0) {
            if (!entries.empty() && history_index > 0) {
                if (history_index == entries.size()) {
                    saved_current = buffer;
                }
                history_index--;
                buffer = entries[history_index];
                cursor = buffer.size();
                redraw(prompt, buffer, cursor);
            }
            return;
        }

        if (history_index < entries.size()) {
            history_index++;
            buffer = history_index == entries.size() ? saved_current : entries[history_index];
            cursor = buffer.size();
            redraw(prompt, buffer, cursor);
        }
    }

    std::string read_escape_sequence() {
        std::string sequence;
        char first = 0;
        if (read(STDIN_FILENO, &first, 1) != 1) {
            return sequence;
        }
        sequence.push_back(first);

        if (first == 'O') {
            char second = 0;
            if (read(STDIN_FILENO, &second, 1) == 1) {
                sequence.push_back(second);
            }
            return sequence;
        }

        if (first != '[') {
            return sequence;
        }

        for (int i = 0; i < 16; ++i) {
            char next = 0;
            if (read(STDIN_FILENO, &next, 1) != 1) {
                break;
            }
            sequence.push_back(next);
            if (next >= '@' && next <= '~') {
                break;
            }
        }
        return sequence;
    }

    std::optional<std::string> read_terminal_line(const std::string& prompt) {
        RawTerminalGuard raw;
        if (!raw.enabled()) {
            output_ << prompt << std::flush;
            std::string line;
            if (!std::getline(input_, line)) {
                return std::nullopt;
            }
            return line;
        }

        std::string buffer;
        std::string saved_current;
        std::size_t cursor = 0;
        std::size_t history_index = history_.entries().size();
        output_ << prompt << std::flush;

        while (true) {
            char c = 0;
            if (read(STDIN_FILENO, &c, 1) != 1) {
                output_ << '\n';
                return std::nullopt;
            }

            if (c == '\r' || c == '\n') {
                output_ << "\r\n";
                return buffer;
            }
            if (c == 3) {
                output_ << "^C\r\n";
                return "";
            }
            if (c == 4) {
                if (buffer.empty()) {
                    output_ << "\r\n";
                    return std::nullopt;
                }
                continue;
            }
            if (c == 1) {
                cursor = 0;
                redraw(prompt, buffer, cursor);
                continue;
            }
            if (c == 5) {
                cursor = buffer.size();
                redraw(prompt, buffer, cursor);
                continue;
            }
            if (c == '\t') {
                apply_completion(prompt, buffer, cursor);
                continue;
            }
            if (c == 127 || c == 8) {
                if (cursor > 0) {
                    buffer.erase(cursor - 1, 1);
                    cursor--;
                    redraw(prompt, buffer, cursor);
                }
                continue;
            }
            if (c == 27) {
                const std::string sequence = read_escape_sequence();
                if (sequence == "[A" || sequence == "OA") {
                    recall_history(prompt, buffer, saved_current, cursor, history_index, -1);
                } else if (sequence == "[B" || sequence == "OB") {
                    recall_history(prompt, buffer, saved_current, cursor, history_index, 1);
                } else if (sequence == "[C" || sequence == "OC") {
                    if (cursor < buffer.size()) {
                        cursor++;
                        redraw(prompt, buffer, cursor);
                    }
                } else if (sequence == "[D" || sequence == "OD") {
                    if (cursor > 0) {
                        cursor--;
                        redraw(prompt, buffer, cursor);
                    }
                } else if (sequence == "[H" || sequence == "OH" || sequence == "[1~" || sequence == "[7~") {
                    cursor = 0;
                    redraw(prompt, buffer, cursor);
                } else if (sequence == "[F" || sequence == "OF" || sequence == "[4~" || sequence == "[8~") {
                    cursor = buffer.size();
                    redraw(prompt, buffer, cursor);
                } else if (sequence == "[3~") {
                    if (cursor < buffer.size()) {
                        buffer.erase(cursor, 1);
                        redraw(prompt, buffer, cursor);
                    }
                }
                continue;
            }
            if (std::isprint(static_cast<unsigned char>(c))) {
                buffer.insert(buffer.begin() + static_cast<std::ptrdiff_t>(cursor), c);
                cursor++;
                redraw(prompt, buffer, cursor);
            }
        }
    }
#endif

    std::istream& input_;
    std::ostream& output_;
    ShellHistory& history_;
    bool interactive_ = false;
};

std::filesystem::path expand_shell_path(const std::string& text) {
    if (text == "~") {
        return home_directory();
    }
    if (text.rfind("~/", 0) == 0 || text.rfind("~\\", 0) == 0) {
        return std::filesystem::path(home_directory()) / text.substr(2);
    }
    return text;
}

bool is_arcosh_script_extension(const std::filesystem::path& path) {
    const std::string extension = lowercase(path.extension().string());
    return extension == ".abas" || extension == ".arc" || extension == ".arcsh" || extension == ".bas";
}

std::vector<std::filesystem::path> stdlib_search_directories() {
    std::vector<std::filesystem::path> dirs = {
        std::filesystem::current_path() / "stdlib",
        std::filesystem::current_path() / "../stdlib",
        std::filesystem::path("/usr/local/share/arcobasic/stdlib"),
        std::filesystem::path("/usr/share/arcobasic/stdlib")
    };
    if (const auto exe_dir = executable_directory()) {
        dirs.push_back(*exe_dir / "../share/arcobasic/stdlib");
    }
    if (const char* stdlib_env = std::getenv("ARCOBASIC_STDLIB")) {
        if (*stdlib_env) {
            dirs.emplace_back(stdlib_env);
        }
    }
    return dirs;
}

std::vector<std::filesystem::path> sorted_script_files(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(directory)) {
        return files;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && is_arcosh_script_extension(entry.path())) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string mod_name_from_path(const std::filesystem::path& path) {
    return path.stem().string();
}

std::string sanitize_mod_name(const std::string& requested) {
    std::string name = std::filesystem::path(trim(requested)).stem().string();
    if (name.empty()) {
        name = "mod";
    }
    std::string safe;
    for (char c : name) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) || c == '_' || c == '-' || c == '.') {
            safe.push_back(c);
        } else if (std::isspace(byte)) {
            safe.push_back('-');
        }
    }
    if (safe.empty() || safe == "." || safe == "..") {
        safe = "mod";
    }
    return safe;
}

std::vector<std::string> read_enabled_mod_names() {
    std::vector<std::string> names;
    std::ifstream input(arcosh_mods_enabled_file());
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        line = sanitize_mod_name(line);
        if (!line.empty() && std::find(names.begin(), names.end(), line) == names.end()) {
            names.push_back(line);
        }
    }
    return names;
}

void write_enabled_mod_names(const std::vector<std::string>& names) {
    std::filesystem::create_directories(arcosh_mods_directory());
    std::ofstream output(arcosh_mods_enabled_file(), std::ios::trunc);
    if (!output) {
        throw std::runtime_error("could not write " + arcosh_mods_enabled_file().string());
    }
    for (const auto& name : names) {
        output << sanitize_mod_name(name) << '\n';
    }
}

std::optional<std::filesystem::path> find_installed_mod(const std::string& requested) {
    const std::string name = sanitize_mod_name(requested);
    for (const auto& file : sorted_script_files(arcosh_mods_directory())) {
        if (mod_name_from_path(file) == name || file.filename().string() == requested) {
            return file;
        }
    }
    return std::nullopt;
}

bool mod_is_enabled(const std::string& name) {
    const auto enabled = read_enabled_mod_names();
    return std::find(enabled.begin(), enabled.end(), sanitize_mod_name(name)) != enabled.end();
}

std::vector<std::filesystem::path> enabled_mod_files() {
    std::vector<std::filesystem::path> files;
    for (const auto& name : read_enabled_mod_names()) {
        if (const auto file = find_installed_mod(name)) {
            files.push_back(*file);
        }
    }
    return files;
}

Value mod_info_value(const std::filesystem::path& path) {
    const std::string name = mod_name_from_path(path);
    return Value::Object{
        {"Name", name},
        {"File", path.filename().string()},
        {"Path", path.string()},
        {"Active", mod_is_enabled(name)}
    };
}

Value list_mods() {
    Value::Array mods;
    for (const auto& file : sorted_script_files(arcosh_mods_directory())) {
        mods.push_back(mod_info_value(file));
    }
    return mods;
}

Value install_mod(const std::string& source_text, const std::string& requested_name) {
    std::filesystem::path source = expand_shell_path(source_text);
    if (!std::filesystem::exists(source) || !std::filesystem::is_regular_file(source)) {
        throw std::runtime_error("mod source file not found: " + source.string());
    }
    if (!is_arcosh_script_extension(source)) {
        throw std::runtime_error("mod source must be an ArcoBASIC script: " + source.string());
    }
    std::filesystem::create_directories(arcosh_mods_directory());
    const std::string name = sanitize_mod_name(requested_name.empty() ? source.stem().string() : requested_name);
    const auto destination = arcosh_mods_directory() / (name + source.extension().string());
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
    return mod_info_value(destination);
}

Value activate_mod(const std::string& requested) {
    const auto file = find_installed_mod(requested);
    if (!file) {
        throw std::runtime_error("mod is not installed: " + requested);
    }
    const std::string name = mod_name_from_path(*file);
    auto enabled = read_enabled_mod_names();
    if (std::find(enabled.begin(), enabled.end(), name) == enabled.end()) {
        enabled.push_back(name);
        write_enabled_mod_names(enabled);
    }
    return mod_info_value(*file);
}

Value deactivate_mod(const std::string& requested) {
    const std::string name = sanitize_mod_name(requested);
    auto enabled = read_enabled_mod_names();
    enabled.erase(std::remove(enabled.begin(), enabled.end(), name), enabled.end());
    write_enabled_mod_names(enabled);
    if (const auto file = find_installed_mod(name)) {
        return mod_info_value(*file);
    }
    return Value::Object{{"Name", name}, {"Active", false}};
}

std::optional<std::filesystem::path> resolve_profile_script_command(const std::string& command) {
    const std::string word = first_shell_word(command);
    if (word.empty()) {
        return std::nullopt;
    }
    const std::filesystem::path scripts = arcosh_scripts_directory();
    for (const std::string& extension : {"", ".abas", ".arcsh", ".arc", ".bas"}) {
        const auto candidate = scripts / (word + extension);
        if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> resolve_local_script_command(const std::string& command) {
    const std::string word = first_shell_word(command);
    if (word.empty()) {
        return std::nullopt;
    }
    const auto resolved = find_external_command(word);
    if (resolved && is_arcosh_script_extension(*resolved)) {
        return resolved;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> resolve_script_launch_target(const std::string& target) {
    if (target.empty()) {
        return std::nullopt;
    }

    const auto expanded = expand_shell_path(target);
    if (std::filesystem::exists(expanded) && std::filesystem::is_regular_file(expanded)) {
        return expanded;
    }

    if (target.find('/') == std::string::npos && target.find('\\') == std::string::npos) {
        if (const auto profile_script = resolve_profile_script_command(target)) {
            return profile_script;
        }
        if (const auto local_script = resolve_local_script_command(target)) {
            return local_script;
        }
    }

    return expanded;
}

bool looks_like_script_launch_target(const std::string& target) {
    if (target.empty()) {
        return false;
    }
    if (is_arcosh_script_extension(std::filesystem::path(target))) {
        return true;
    }
    const auto resolved = resolve_script_launch_target(target);
    return resolved && std::filesystem::exists(*resolved) && std::filesystem::is_regular_file(*resolved) && is_arcosh_script_extension(*resolved);
}

bool set_environment_variable(const std::string& name, const std::string& value) {
    if (name.empty()) {
        return false;
    }
#ifdef _WIN32
    return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
    return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
}

bool unset_environment_variable(const std::string& name) {
    if (name.empty()) {
        return false;
    }
#ifdef _WIN32
    return _putenv_s(name.c_str(), "") == 0;
#else
    return unsetenv(name.c_str()) == 0;
#endif
}

std::vector<std::string> environment_entries() {
    std::vector<std::string> entries;
#if defined(_WIN32)
    char** env = _environ;
#else
    char** env = environ;
#endif
    if (!env) {
        return entries;
    }
    for (char** current = env; *current != nullptr; ++current) {
        entries.emplace_back(*current);
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

bool contains_token(const std::string& text, const std::string& token) {
    return text.find(token) != std::string::npos;
}

bool same_command_text(const std::string& actual, const std::string& expected) {
    return lowercase(trim(actual)) == lowercase(trim(expected));
}

std::string force_command_color(const std::string& command) {
    if (!g_color_enabled) {
        return command;
    }

    const std::string text = trim(command);
    const std::string word = first_shell_word(text);
    std::string rewritten = text;

    if (word == "ls" && !contains_token(text, "--color")) {
        rewritten = "ls --color=always" + text.substr(word.size());
    } else if ((word == "grep" || word == "egrep" || word == "fgrep") && !contains_token(text, "--color")) {
        rewritten = word + " --color=always" + text.substr(word.size());
    } else if (word == "rg" && !contains_token(text, "--color")) {
        rewritten = "rg --color=always" + text.substr(word.size());
    } else if (word == "diff" && !contains_token(text, "--color")) {
        rewritten = "diff --color=always" + text.substr(word.size());
    } else if (word == "git" && !contains_token(text, "-c color.ui")) {
        rewritten = "git -c color.ui=always -c color.status=always -c color.diff=always" + text.substr(word.size());
    }

    return "CLICOLOR_FORCE=1 FORCE_COLOR=1 " + rewritten;
}

std::string style(const std::string& text, const std::string& color) {
    if (!g_color_enabled) {
        return text;
    }
    const auto& codes = color_codes();
    const auto found = codes.find(lowercase(color));
    if (found == codes.end() || found->second == "0") {
        return text;
    }
    return "\033[" + found->second + "m" + text + "\033[0m";
}

Value color_names() {
    Value::Array names;
    for (const auto& [name, code] : color_codes()) {
        (void)code;
        if (name != "reset") {
            names.emplace_back(name);
        }
    }
    return names;
}

Value color_function(const std::vector<Value>& args, const std::string& color) {
    if (args.size() != 1) {
        throw std::runtime_error("Color." + color + " expects 1 argument");
    }
    return style(args[0].to_string(), color);
}

const char* tutorial_source() {
    return R"ARCO(
#DESCRIPTION "Interactive ArcoSH administrator tutorial"

FUNCTION Section(title)
    PRINT ""
    PRINT Color.Cyan(Color.Bold(title))
END FUNCTION

FUNCTION Continue()
    ignored = Tutorial.ReadLine("press Enter to continue ")
END FUNCTION

PRINT Color.Bold("ArcoSH sysadmin tutorial")
PRINT "This tutorial is written in ArcoBASIC and runs inside ArcoSH."
PRINT "You will practice commands that inspect the host, run tools, handle errors,"
PRINT "write tiny admin scripts, and return safely to the shell."
PRINT "At practice prompts, type hint, skip, or quit if needed."
Continue()

Section("1. Use ArcoBASIC at the prompt")
PRINT "ArcoSH is both a shell and a BASIC runtime."
PRINT "Type this command exactly:"
ok = Tutorial.Step("PRINT \"hello admin\"", "arco")

Section("2. Run host commands")
PRINT "Bare commands run through the host shell, and RUN() captures output for scripts."
PRINT "Type this bare shell command:"
ok = Tutorial.Step("printf service-ok", "shell")

Section("3. Capture command results")
PRINT "A sysadmin script usually needs an exit code and captured output."
PRINT "Type this ArcoBASIC statement:"
ok = Tutorial.Step("result = RUN(\"printf captured\") : PRINT result.Output : PRINT result.ExitCode", "arco")

Section("4. Inspect files")
PRINT "File.Exists and File.Find are available to ArcoBASIC scripts."
PRINT "Type this command:"
ok = Tutorial.Step("PRINT File.Exists(\"readme.md\")", "arco")

Section("5. Ask the host about itself")
PRINT "Host helpers let scripts adapt to the machine they are managing."
PRINT "Type this command:"
ok = Tutorial.Step("PRINT Host.OSName()", "arco")
Continue()

Section("6. Handle failures")
PRINT "Use TRY/CATCH when an admin script should report an error and continue."
PRINT "Here is a complete recovery block running as ArcoBASIC:"
ignored = Tutorial.Run("TRY\nPRINT missing_setting\nCATCH err\nPRINT \"caught: \" + err.Message\nEND TRY", "arco")
Continue()

Section("7. Build a quick line-numbered script")
PRINT "Line-numbered programs are good for quick loops at the REPL."
PRINT "This script prints a small counter and uses STOP to return to ArcoSH:"
ignored = Tutorial.Run("10 x = 0\n20 PRINT x\n30 x += 1\n40 IF x >= 3 THEN STOP\n50 GOTO 20", "arco")
Continue()

Section("8. Know the escape hatches")
PRINT "STOP ends the current ArcoBASIC program and returns to ArcoSH."
PRINT "Exit(), ExitProgram(), and ExitTheProgram() terminate the arcosh process."
PRINT "If you misspell a host command, fix the next command with: oops <correct-command>"
PRINT "Use HELP run, HELP files, HELP host, HELP try, and HELP lines for reference."

PRINT ""
PRINT Color.Green("Tutorial complete. You have run ArcoBASIC, host commands, capture, files, host inspection, error handling, and line-numbered scripts.")
)ARCO";
}

const std::map<std::string, std::string>& help_catalog() {
    static const std::map<std::string, std::string> catalog = {
        {"help",
            "ArcoSH help\n"
            "\n"
            "Usage:\n"
            "  HELP\n"
            "  HELP topics\n"
            "  HELP if\n"
            "  HELP for\n"
            "  HELP do\n"
            "  HELP select\n"
            "  HELP run\n"
            "  TUTORIAL\n"
            "  arcosh --tutorial\n"
            "  arcosh --init-profile\n"
            "  arcosh --install-shell\n"
            "  arcosh --doctor\n"
            "  arcosh --help if\n"
            "\n"
            "Script helpers:\n"
            "  PRINT Help.Topic(\"run\")\n"
            "  FOR topic IN Help.Topics()\n"
            "      PRINT topic\n"
            "  NEXT\n"},
        {"topics",
            "Available help topics:\n"
            "  if          IF condition THEN ... ELSE ... END IF\n"
            "  while       WHILE condition ... WEND\n"
            "  for         FOR loops and FOR item IN array\n"
            "  print       PRINT expression\n"
            "  let         Assignment with name = value or LET name = value\n"
            "  arrays      Array literals and FOR IN\n"
            "  strings     String.Trim, String.Split, String.Replace, String.Contains\n"
            "  objects     Object literals and property reads\n"
            "  contains    CONTAINS and IN membership checks\n"
            "  functions   Built-in function calls\n"
            "  types       TYPEOF, ISNULL, NUMBER, STRING\n"
            "  try         TRY, CATCH, END TRY error handling\n"
            "  function    Define user functions with FUNCTION and RETURN\n"
            "  classes     CLASS records with fields, Init, and methods\n"
            "  bitwise     Bit.And(), Bit.Or(), shifts, and symbolic operators\n"
            "  directives  #VERSION, #TARGET, #DEFINE, #IFDEF, and compile controls\n"
            "  attributes  @EXPERIMENTAL, @DEPRECATED, @DOC, and symbol metadata\n"
            "  stdlib      Importable .abas standard library modules\n"
            "  doctor      Check profile, stdlib, terminal, and process readiness\n"
            "  exit        Exit(), ExitProgram(), ExitTheProgram()\n"
            "  lines       Line-numbered REPL programs, LIST, RUN, NEW\n"
            "  launch      Run .abas scripts from the REPL with @, RUN, and LOAD\n"
            "  run         Running host commands with RUN()\n"
            "  files       File helpers available to shell scripts\n"
            "  host        Host/process helpers available to shell scripts\n"
            "  system      OS capabilities, GUI open, process launching, printers\n"
            "  network     HTTP GET, POST, and file downloads\n"
            "  gui         Desktop windows, drawing, and input events\n"
            "  printers    Printer discovery and file printing helpers\n"
            "  sudo        Request elevated permissions from scripts\n"
            "  colors      Color output and color-aware external commands\n"
            "  tui         Retro terminal UI helpers for scripts\n"
            "  jobs        Background jobs with &, jobs, fg, and bg\n"
            "  shell       ArcoSH command-line and REPL usage\n"
            "  editing     Line editing and persistent history\n"
            "  profile     ~/.arcosh rc files, plugins, and reusable scripts\n"
            "  login       Install ArcoSH as a login shell\n"
            "  aliases     alias, unalias, source, and script Args\n"
            "  tutorial    Interactive ArcoBASIC tutorials\n"
            "  basic       Core ArcoBASIC syntax summary\n"
            "  values      Value types summary\n"
            "  examples    Short runnable examples\n"},
        {"tui",
            "Retro TUI helpers\n"
            "\n"
            "ArcoBASIC scripts can build terminal-native panels without depending on\n"
            "ncurses or external UI libraries. Helpers return strings, so compose them\n"
            "with PRINT, File.WriteText, logs, or tutorials.\n"
            "\n"
            "Helpers:\n"
            "  TUI.Rule(title)\n"
            "  TUI.Rule(title, width)\n"
            "  TUI.ThemeRule(theme, title)\n"
            "  TUI.ThemeRule(theme, title, width)\n"
            "  TUI.Box(title, body)\n"
            "  TUI.ThemeBox(theme, title, body)\n"
            "  TUI.Scroll(title, body)\n"
            "  TUI.Header(title)\n"
            "  TUI.ThemeNames()\n"
            "  TUI.Badge(state)\n"
            "  TUI.Status(label, state)\n"
            "  TUI.Status(label, state, detail)\n"
            "  TUI.Progress(label, current, total)\n"
            "  TUI.Progress(label, current, total, width)\n"
            "  TUI.List(title, items)\n"
            "  TUI.Menu(title, items)\n"
            "  TUI.KeyValues(title, object)\n"
            "  TUI.Table(headers, rows)\n"
            "  TUI.Clear()\n"
            "  TUI.Cursor(row, column)\n"
            "  TUI.MouseEnable() / TUI.MouseDisable()\n"
            "  TUI.ReadEvent()\n"
            "  TUI.HitTest(event, x, y, width, height)\n"
            "\n"
            "Examples:\n"
            "  PRINT TUI.Header(\"Deploy Check\")\n"
            "  PRINT TUI.Box(\"STATUS\", \"web: ok\\ndb: warning\")\n"
            "  PRINT TUI.ThemeBox(\"scroll\", \"Quest\", \"Patch the server\")\n"
            "  PRINT TUI.Scroll(\"Ancient Manual\", \"Read HELP tui\")\n"
            "  PRINT TUI.ThemeBox(\"circuit\", \"Report\", \"service bus online\")\n"
            "  PRINT TUI.ThemeBox(\"parchment\", \"Notice\", \"Back up configs first\")\n"
            "  PRINT TUI.Rule(\"NEXT\", 60)\n"
            "  PRINT TUI.Status(\"database\", \"ok\", \"reachable\")\n"
            "  PRINT TUI.Progress(\"backup\", 7, 10, 20)\n"
            "  PRINT TUI.Menu(\"Actions\", [\"scan\", \"repair\", \"quit\"])\n"
            "  PRINT TUI.KeyValues(\"Host\", {\"OS\": Host.OSName(), \"Name\": Host.Hostname()})\n"
            "  rows = [[\"web\", \"ok\"], [\"db\", \"warn\"]]\n"
            "  PRINT TUI.Table([\"Service\", \"State\"], rows)\n"
            "  PRINT TUI.MouseEnable()\n"
            "  event = TUI.ReadEvent()\n"
            "  IF event.Type == \"mouse\" AND event.Action == \"press\" AND TUI.HitTest(event, 10, 5, 12, 1) THEN PRINT \"clicked\"\n"
            "  PRINT TUI.MouseDisable()\n"
            "  FOR theme IN TUI.ThemeNames()\n"
            "      PRINT TUI.ThemeRule(theme, theme, 40)\n"
            "  NEXT\n"
            "\n"
            "Layout helpers intentionally use ASCII borders for maximum compatibility\n"
            "with old terminals, SSH sessions, logs, and Windows consoles. Mouse input\n"
            "uses xterm SGR reporting on POSIX terminals; coordinates are 1-based. Always\n"
            "disable mouse reporting before exiting. Clear and\n"
            "cursor helpers return ANSI sequences for scripts that want screen control.\n"},
        {"tutorial",
            "Interactive tutorial\n"
            "\n"
            "Run the tutorial from the REPL:\n"
            "  TUTORIAL\n"
            "\n"
            "Run it from the command line:\n"
            "  arcosh --tutorial\n"
            "  arcosh --tutorial game\n"
            "  arcosh --tutorial tool\n"
            "  arcosh --tutorial adventure\n"
            "  arcosh --tutorial adventure1\n"
            "  arcosh --tutorial adventure2\n"
            "  arcosh --tutorial adventure3\n"
            "\n"
            "Available tutorials:\n"
            "  sysadmin    ArcoSH administrator workflow with status panels and tables\n"
            "  game        Build a small interactive guessing game with scroll panels\n"
            "  tool        Build a useful system report tool with circuit-style reports\n"
            "  adventure   ArcoAdventures hub: original tutorial missions in The Arco Files world\n"
            "  adventure1  Badge Bureau variables, strings, and decisions\n"
            "  adventure2  Snackstorm loops, totals, and automation limits\n"
            "  adventure3  Evidence locker classes and ArcoCompy packing\n"
            "\n"
            "Practice prompt controls:\n"
            "  hint        Show the expected command again\n"
            "  skip        Run the expected command and continue\n"
            "  quit        Stop the tutorial cleanly\n"
            "\n"
            "Editable tutorial source:\n"
            "  tutorials/arcosh_sysadmin.abas\n"
            "  tutorials/arcosh_game.abas\n"
            "  tutorials/arcosh_tool.abas\n"
            "  tutorials/arcoadventures_intro.abas\n"
            "  tutorials/arcoadventure_badge_bureau.abas\n"
            "  tutorials/arcoadventure_snackstorm.abas\n"
            "  tutorials/arcoadventure_evidence_locker.abas\n"
            "\n"
            "The tutorials themselves are written in ArcoBASIC. They teach:\n"
            "  PRINT and direct ArcoBASIC at the prompt\n"
            "  bare host commands\n"
            "  RUN() output capture and ExitCode checks\n"
            "  File.Exists, Host.OSName, Process.List, and profile paths\n"
            "  TRY/CATCH recovery\n"
            "  variables, interpolation, arrays, loops, and conditions\n"
            "  line-numbered scripts with GOTO and STOP\n"
            "  TUI boxes, themed rules, scrolls, menus, progress bars, and tables\n"
            "  small games and useful sysadmin tools\n"},
        {"if",
            "IF statement\n"
            "\n"
            "Syntax:\n"
            "  IF condition THEN\n"
            "      statements\n"
            "  ELSE\n"
            "      statements\n"
            "  END IF\n"
            "\n"
            "ELSE is optional.\n"
            "\n"
            "Examples:\n"
            "  IF x == 10 THEN ExitTheProgram()\n"
            "  IF x == 10 THEN PRINT \"ten\" : PRINT \"again\"\n"
            "\n"
            "  IF x > 10 THEN\n"
            "      PRINT \"big\"\n"
            "  END IF\n"
            "\n"
            "  IF File.Exists(\"readme.md\") THEN\n"
            "      PRINT \"found\"\n"
            "  ELSE\n"
            "      PRINT \"missing\"\n"
            "  END IF\n"
            "\n"
            "Line-numbered REPL example:\n"
            "  10 x = 5\n"
            "  20 IF x > 3 THEN\n"
            "  30 PRINT \"yes\"\n"
            "  40 END IF\n"
            "  RUN\n"},
        {"while",
            "WHILE loop\n"
            "\n"
            "Syntax:\n"
            "  WHILE condition\n"
            "      statements\n"
            "  WEND\n"
            "\n"
            "Example:\n"
            "  x = 0\n"
            "  WHILE x < 3\n"
            "      x = x + 1\n"
            "      IF x == 2 THEN CONTINUE WHILE\n"
            "      PRINT x\n"
            "      IF x > 10 THEN EXIT WHILE\n"
            "  WEND\n"},
        {"select",
            "SELECT CASE statement\n"
            "\n"
            "Syntax:\n"
            "  SELECT CASE expression\n"
            "  CASE value\n"
            "      statements\n"
            "  CASE other, another\n"
            "      statements\n"
            "  CASE 1 TO 10\n"
            "      statements\n"
            "  CASE ELSE\n"
            "      statements\n"
            "  END SELECT\n"
            "\n"
            "Example:\n"
            "  SELECT CASE status\n"
            "  CASE \"ok\"\n"
            "      PRINT \"ready\"\n"
            "  CASE \"warn\", \"slow\"\n"
            "      PRINT \"check it\"\n"
            "  CASE ELSE\n"
            "      PRINT \"unknown\"\n"
            "  END SELECT\n"},
        {"for",
            "FOR loops\n"
            "\n"
            "Numeric loop:\n"
            "  FOR i = 1 TO 10\n"
            "      PRINT i\n"
            "  NEXT\n"
            "\n"
            "With STEP:\n"
            "  FOR i = 10 TO 1 STEP -1\n"
            "      PRINT i\n"
            "  NEXT\n"
            "\n"
            "Array loop:\n"
            "  FOR item IN [\"a\", \"b\"]\n"
            "      PRINT item\n"
            "  NEXT\n"
            "\n"
            "Loop control:\n"
            "  IF i == 3 THEN CONTINUE FOR\n"
            "  IF i == 8 THEN EXIT FOR\n"},
        {"do",
            "DO loops\n"
            "\n"
            "Pre-test loops:\n"
            "  DO WHILE condition\n"
            "      statements\n"
            "  LOOP\n"
            "\n"
            "  DO UNTIL condition\n"
            "      statements\n"
            "  LOOP\n"
            "\n"
            "Post-test loops:\n"
            "  DO\n"
            "      statements\n"
            "  LOOP WHILE condition\n"
            "\n"
            "  DO\n"
            "      statements\n"
            "  LOOP UNTIL condition\n"
            "\n"
            "Loop control:\n"
            "  IF done THEN EXIT DO\n"
            "  IF skip THEN CONTINUE DO\n"},
        {"print",
            "PRINT statement\n"
            "\n"
            "Syntax:\n"
            "  PRINT expression\n"
            "\n"
            "Examples:\n"
            "  PRINT \"hello\"\n"
            "  PRINT 2 + 3\n"
            "  PRINT Color.Green(\"ok\")\n"},
        {"let",
            "Assignment\n"
            "\n"
            "Syntax:\n"
            "  name = expression\n"
            "  LET name = expression\n"
            "\n"
            "Examples:\n"
            "  x = 10\n"
            "  LET name = \"Ada\"\n"
            "  result = RUN(\"printf hello\")\n"},
        {"arrays",
            "Arrays\n"
            "\n"
            "Arrays are dynamic vector-style containers. They can grow, shrink,\n"
            "and hold mixed ArcoBASIC values: numbers, strings, booleans, objects,\n"
            "class instances, nested arrays, and NULL.\n"
            "\n"
            "Syntax:\n"
            "  values = [1, 2, 3]\n"
            "\n"
            "Examples:\n"
            "  values = Array.New()\n"
            "  filled = Array.New(3, \"pending\")\n"
            "  PRINT values[0]\n"
            "  values[1] = 99\n"
            "  Array.Add(values, 4)\n"
            "  Array.Append(values, {\"Name\": \"Ada\"})\n"
            "  Array.Insert(values, 1, \"middle\")\n"
            "  last = Array.Pop(values)\n"
            "  first = Array.Shift(values)\n"
            "  Array.Unshift(values, \"first\")\n"
            "  removed = Array.RemoveAt(values, 2)\n"
            "  found_and_removed = Array.Remove(values, \"middle\")\n"
            "  Array.Extend(values, [\"more\", \"items\"])\n"
            "  Array.Resize(values, 10, NULL)\n"
            "  Array.Clear(values)\n"
            "  PRINT Array.Length(values)\n"
            "  PRINT Array.Empty(values)\n"
            "  PRINT Array.First(values)\n"
            "  PRINT Array.Last(values)\n"
            "  index = Array.Find(values, 2)\n"
            "  reversed = Array.Reverse(values)\n"
            "  joined = Array.Join(values, \",\")\n"
            "  has_two = Array.Contains(values, 2)\n"
            "  sorted = Array.Sort(values)\n"
            "  PRINT LEN(values)\n"
            "  PRINT 2 IN values\n"
            "  FOR value IN values\n"
            "      PRINT value\n"
            "  NEXT\n"},
        {"strings",
            "String helpers\n"
            "\n"
            "Helpers:\n"
            "  String.Trim(text)\n"
            "  String.Split(text, delimiter)\n"
            "  String.Replace(text, from, to)\n"
            "  String.Contains(text, part)\n"
            "  String.StartsWith(text, prefix)\n"
            "  String.EndsWith(text, suffix)\n"
            "  String.Lines(text)\n"
            "  Format(\"{0} {1}\", a, b)\n"
            "  Upper(text)\n"
            "  Lower(text)\n"
            "\n"
            "String escapes:\n"
            "  \\n newline, \\r carriage return, \\t tab, \\\" quote, \\\\ slash\n"
            "\n"
            "Interpolated strings:\n"
            "  name = \"Ada\"\n"
            "  PRINT $\"Hello {name}\"\n"
            "  PRINT $\"Today is {DATE()}\"\n"
            "  PRINT $\"Math works: {1 + 2}\"\n"
            "  PRINT $\"Literal braces: {{ok}}\"\n"
            "\n"
            "Examples:\n"
            "  PRINT String.Trim(\"  hello  \")\n"
            "  File.WriteText(\"log.txt\", \"started\\n\")\n"
            "  parts = String.Split(\"a,b,c\", \",\")\n"
            "  PRINT parts[0]\n"
            "  PRINT String.Replace(\"a-b\", \"-\", \"+\")\n"},
        {"objects",
            "Objects\n"
            "\n"
            "Syntax:\n"
            "  person = {\"Name\": \"Ada\", \"Age\": 36}\n"
            "\n"
            "Read properties with dot syntax:\n"
            "  PRINT person.Name\n"
            "  person.Name = \"Grace\"\n"
            "\n"
            "Object helpers:\n"
            "  Object.Keys(person)\n"
            "  Object.Has(person, \"Name\")\n"
            "  Object.Get(person, \"Missing\", \"fallback\")\n"
            "  copy = Object.Set(person, \"Role\", \"Admin\")\n"
            "\n"
            "RUN returns an object:\n"
            "  result = RUN(\"printf hello\")\n"
            "  PRINT result.Output\n"
            "  PRINT result.ExitCode\n"},
        {"contains",
            "Membership checks\n"
            "\n"
            "CONTAINS checks whether the left value contains the right value:\n"
            "  PRINT \"abcdef\" CONTAINS \"bcd\"\n"
            "  PRINT [1, 2, 3] CONTAINS 2\n"
            "\n"
            "IN checks whether the left value is in the right value:\n"
            "  PRINT 2 IN [1, 2, 3]\n"},
        {"functions",
            "Function calls\n"
            "\n"
            "Function names are case-insensitive.\n"
            "\n"
            "Syntax:\n"
            "  Name(arg1, arg2)\n"
            "\n"
            "Core helpers:\n"
            "  LEN(value)\n"
            "  Upper(text)\n"
            "  Lower(text)\n"
            "  TYPEOF(value)\n"
            "  NUMBER(text)\n"
            "  STRING(value)\n"
            "\n"
            "Shell helpers:\n"
            "  File.Exists(path)\n"
            "  Host.OSName()\n"
            "  Time.Now()\n"
            "  Time.Timestamp()\n"
            "  Sleep(milliseconds)\n"
            "  Help.Topic(topic)\n"
            "  Color.Green(text)\n"
            "  ArcoSH.StartJob(command)\n"
            "  ArcoSH.Jobs()\n"
            "  ArcoSH.KillJob(id)\n"
            "  ArcoSH.WaitJob(id)\n"
            "  ExitTheProgram(code)\n"
            "  Bit.And(value, mask)\n"
            "  Bit.ShiftLeft(value, amount)\n"},
        {"function",
            "User functions\n"
            "\n"
            "Syntax:\n"
            "  FUNCTION Name(arg1, arg2)\n"
            "      RETURN expression\n"
            "  END FUNCTION\n"
            "\n"
            "Example:\n"
            "  FUNCTION Sum(a, b)\n"
            "      RETURN a + b\n"
            "  END FUNCTION\n"
            "\n"
            "  PRINT Sum(2, 3)\n"
            "\n"
            "Function parameters and assignments inside the function are local.\n"},
        {"types",
            "Type and conversion helpers\n"
            "\n"
            "Helpers:\n"
            "  TYPEOF(value)   Returns Null, Boolean, Number, String, Array, or Object\n"
            "  ISNULL(value)   TRUE when value is NULL\n"
            "  NUMBER(value)   Converts text/bool/number to Number\n"
            "  STRING(value)   Converts a value to display text\n"
            "\n"
            "Examples:\n"
            "  PRINT TYPEOF([1, 2, 3])\n"
            "  PRINT NUMBER(\"42\") + 1\n"
            "  PRINT ISNULL(NULL)\n"},
        {"try",
            "TRY / CATCH\n"
            "\n"
            "Syntax:\n"
            "  TRY\n"
            "      risky statements\n"
            "  CATCH err\n"
            "      PRINT err.Message\n"
            "  END TRY\n"
            "\n"
            "Example:\n"
            "  TRY\n"
            "      PRINT missing_value\n"
            "  CATCH err\n"
            "      PRINT err.Message\n"
            "  END TRY\n"
            "\n"
            "Exit() and RETURN are control flow and are not swallowed by CATCH.\n"},
        {"bitwise",
            "Bitwise operations\n"
            "\n"
            "Preferred readable style:\n"
            "  SHIFT(value, amount)       Positive shifts left, negative shifts right\n"
            "  BIT(value, position)       TRUE when a bit is set\n"
            "  SETBIT(value, position)\n"
            "  CLEARBIT(value, position)\n"
            "  TOGGLEBIT(value, position)\n"
            "  BITCOUNT(value)\n"
            "  ROTATELEFT(value, amount)\n"
            "  ROTATERIGHT(value, amount)\n"
            "\n"
            "Flag style:\n"
            "  permissions ADD CAN_WRITE\n"
            "  permissions REMOVE CAN_WRITE\n"
            "  permissions TOGGLE CAN_WRITE\n"
            "  IF permissions HAS CAN_WRITE THEN\n"
            "\n"
            "Definitions:\n"
            "  FLAGS FileAttributes\n"
            "      Hidden = SHIFT(1, 1)\n"
            "  END FLAGS\n"
            "\n"
            "Conversions:\n"
            "  BitsToString(value, width)\n"
            "  BitsToBinary(value, width)\n"
            "  StringToBits(text)\n"
            "  BitsTable(value)\n"
            "  HexToString(value)\n"
            "  StringToHex(text)\n"
            "\n"
            "Numeric bases:\n"
            "  %10101010, 0b10101010, &HFF00FF, 0xFF00FF\n"
            "\n"
            "Power-user operators remain available:\n"
            "  AND, OR, XOR, NOT, BITAND, BITOR, BITXOR, BITNOT, SHL, SHR\n"
            "  &, |, ^, ~, <<, >>\n"
            "\n"
            "For short-circuit boolean logic, use ANDALSO / ORELSE, && / ||, and !.\n"
            "\n"
            "Compound assignments also work:\n"
            "  x += 1\n"
            "  x -= 1\n"
            "  x *= 2\n"
            "  x /= 2\n"
            "  x &= mask\n"
            "  x |= mask\n"
            "  x ^= mask\n"
            "  x <<= 1\n"
            "  x >>= 1\n"},
        {"exit",
            "Exit the program\n"
            "\n"
            "Use STOP to end the current ArcoBASIC program and return to ArcoSH:\n"
            "  IF done THEN STOP\n"
            "\n"
            "Use Exit to terminate the arcosh process or script process with an optional code:\n"
            "  Exit()\n"
            "  Exit(1)\n"
            "  ExitProgram(1)\n"
            "  ExitTheProgram(1)\n"
            "\n"
            "Example:\n"
            "  IF x == 10 THEN ExitTheProgram()\n"
            "\n"
            "In arcosh script mode, the process exits with that code.\n"},
        {"directives",
            "Compiler directives\n"
            "\n"
            "Directives start with # and affect the file or compilation unit.\n"
            "\n"
            "Metadata:\n"
            "  #VERSION \"1.0.0\"\n"
            "  #AUTHOR \"Daedalus\"\n"
            "  #DESCRIPTION \"Backup utility\"\n"
            "  #ENTRY Main\n"
            "\n"
            "Targets and capabilities:\n"
            "  #TARGET windows linux\n"
            "  #REQUIRE filesystem.read\n"
            "  #FEATURE unsafe\n"
            "  #STRICT ON\n"
            "\n"
            "Diagnostics and notes:\n"
            "  #WARNING \"Legacy code path\"\n"
            "  #ERROR \"Unsupported target\"\n"
            "  #TODO \"Replace temporary parser\"\n"
            "  #NOTE \"Windows requires elevation\"\n"
            "\n"
            "Conditional compilation:\n"
            "  #DEFINE DEBUG\n"
            "  #IFDEF DEBUG\n"
            "      PRINT \"Debug\"\n"
            "  #ELSE\n"
            "      PRINT \"Release\"\n"
            "  #ENDIF\n"
            "\n"
            "Current compiler symbols include TARGET_WINDOWS, TARGET_LINUX,\n"
            "TARGET_MACOS, and DEBUG/RELEASE style symbols as available.\n"},
        {"attributes",
            "Attributes\n"
            "\n"
            "Attributes start with @ and describe the next symbol. Symbol parsing is\n"
            "still early, so ArcoBASIC accepts and records attributes now while fuller\n"
            "function/class metadata lands later.\n"
            "\n"
            "Examples:\n"
            "  @EXPERIMENTAL\n"
            "  @EXPERIMENTAL(\"API not finalized\")\n"
            "  @DEPRECATED(\"Use NewAPI\")\n"
            "  @DOC(\"Restarts a service\")\n"
            "  @UNSAFE\n"
            "  @TESTONLY\n"
            "  @BENCHMARK\n"
            "  @INLINE\n"
            "  @NOINLINE\n"
            "  @SERIALIZABLE\n"
            "  @THREADSAFE\n"
            "  @OBSOLETE(\"Removed in v2.0\")\n"},
        {"stdlib",
            "Standard library modules\n"
            "\n"
            "Import modules with #IMPORT. ArcoBASIC searches the current directory,\n"
            "stdlib/, ../stdlib/, and ARCOBASIC_STDLIB. Extensionless names try\n"
            ".abas, .arc, and .bas automatically.\n"
            "\n"
            "Modules:\n"
            "  #IMPORT \"text\"      Text.IsBlank, Text.Join, Text.Lines\n"
            "  #IMPORT \"files\"     Files.ReadLines, Files.WriteLines, Files.AppendLine\n"
            "  #IMPORT \"shell\"     Shell.Output, Shell.Ok, Shell.Status, Shell.StartJob\n"
            "  #IMPORT \"sysadmin\"  SysAdmin.CommandExists, SysAdmin.AppendLog\n"
            "  #IMPORT \"compy\"     ArcoCompy.Pack, ArcoCompy.Unpack, Save, Load\n"
            "  #IMPORT \"compydb\"   ArcoCompyDB.Schema, PackRecord, TryUnpackRecord\n"
            "  #IMPORT \"arcodb\"    ArcoDB.Open, Keep, RecallBy, Catalog, Write\n"
            "  #IMPORT \"commons\"   Commons.Router, FeedItem, Report, AuditEntry\n"
            "  #IMPORT \"arcology\"  Arcology.Open, CreateCommunity, FeedForUser\n"
            "\n"
            "Alias imports add a shorter namespace while keeping compatibility names:\n"
            "  #IMPORT \"text\" AS Txt\n"
            "  PRINT Txt.IsBlank(\"   \")\n"
            "\n"
            "Example:\n"
            "  #IMPORT \"compy\"\n"
            "  packed = ArcoCompy.Pack({\"Name\": \"Ada\", \"Level\": 7})\n"
            "  restored = ArcoCompy.Unpack(packed)\n"
            "  PRINT restored.Name\n"
            "\n"
            "  #IMPORT \"compydb\"\n"
            "  schema = ArcoCompyDB.Schema(\"Customer\", [\"id\", \"name\"])\n"
            "  packed = ArcoCompyDB.PackRecord(schema, {\"id\": 1042, \"name\": \"Wanda\"})\n"
            "\n"
            "  #IMPORT \"arcodb\"\n"
            "  db = ArcoDB.Open(\"people.arcodb\")\n"
            "  ArcoDB.Catalog(db, schema, \"name\")\n"
            "  id = ArcoDB.Keep(db, schema, {\"id\": 1042, \"name\": \"Wanda\"})\n"
            "\n"
            "The modules are regular viewable .abas files under stdlib/.\n"},
        {"doctor",
            "ArcoSH doctor\n"
            "\n"
            "Run a self-check without loading rc files:\n"
            "  arcosh --doctor\n"
            "\n"
            "Checks:\n"
            "  ~/.arcosh or ARCOSH_HOME profile paths\n"
            "  plugin and script directories\n"
            "  stdlib directory discovery\n"
            "  #IMPORT \"text\" execution\n"
            "  common host commands such as sh, printf, and grep\n"
            "  terminal/job-control readiness\n"
            "\n"
            "Use this before making ArcoSH your login shell or when debugging startup.\n"},
        {"lines",
            "Line-numbered REPL programs\n"
            "\n"
            "Type numbered lines to build a small program:\n"
            "  10 x = 0\n"
            "  20 WHILE x < 3\n"
            "  30 x = x + 1\n"
            "  40 PRINT x\n"
            "  50 WEND\n"
            "\n"
            "Commands:\n"
            "  LIST  Show stored numbered lines\n"
            "  RUN   Execute stored numbered lines\n"
            "  NEW   Clear stored numbered lines\n"
            "\n"
            "LOAD script.abas switches the REPL to a loaded script instead.\n"
            "Use NEW to clear either a loaded script or numbered scratch program.\n"
            "\n"
            "Replace a line by typing the same number again.\n"
            "Delete a line by typing only its number.\n"},
        {"launch",
            "Launching ArcoBASIC scripts from ArcoSH\n"
            "\n"
            "Single-step launch from the REPL:\n"
            "  @script.abas arg1 arg2\n"
            "  RUN script.abas arg1 arg2\n"
            "\n"
            "Classic load/run flow:\n"
            "  LOAD script.abas\n"
            "  LIST\n"
            "  RUN\n"
            "  LOAD script.abas; RUN\n"
            "  NEW\n"
            "\n"
            "@ and RUN script.abas populate Args, Script.Path, Script.Name, and Script.ArgCount.\n"
            "RUN \"printf hello\" still runs a host command; only script-looking RUN targets launch files.\n"
            "Profile scripts in ~/.arcosh/scripts can also be launched by name.\n"},
        {"basic",
            "ArcoBASIC syntax\n"
            "\n"
            "Statements:\n"
            "  PRINT expression\n"
            "  name = expression\n"
            "  LET name = expression\n"
            "  statement : statement\n"
            "  IF condition THEN ... ELSE ... END IF\n"
            "  SELECT CASE value ... CASE item ... END SELECT\n"
            "  WHILE condition ... WEND\n"
            "  DO WHILE condition ... LOOP\n"
            "  DO ... LOOP UNTIL condition\n"
            "  FOR i = 1 TO 10 STEP 2 ... NEXT\n"
            "  FOR item IN array ... NEXT\n"
            "  EXIT FOR, CONTINUE FOR, EXIT WHILE, CONTINUE WHILE, EXIT DO, CONTINUE DO\n"
            "  CLASS Name ... END CLASS\n"
            "\n"
            "Expressions:\n"
            "  1 + 2 * 3\n"
            "  10 MOD 3\n"
            "  10 % 3\n"
            "  safe ANDALSO object.Ready\n"
            "  found || fallback\n"
            "  !done\n"
            "  \"abc\" CONTAINS \"b\"\n"
            "  2 IN [1, 2, 3]\n"
            "  person.Name\n"
            "\n"
            "Core helpers:\n"
            "  LEN(value), Upper(text), Lower(text)\n"
            "\n"
            "Classic line-numbered input:\n"
            "  10 x = 0\n"
            "  20 WHILE x < 3\n"
            "  30 x = x + 1\n"
            "  40 PRINT x\n"
            "  50 WEND\n"
            "  RUN\n"},
        {"classes",
            "Classes\n"
            "\n"
            "Define lightweight object classes with fields and methods:\n"
            "  CLASS Counter\n"
            "      SHARED NextId = 0\n"
            "      PRIVATE Secret = \"internal\"\n"
            "      Value AS Number = 0\n"
            "      CONSTRUCTOR(start AS Number)\n"
            "          SELF.Value = start\n"
            "      END CONSTRUCTOR\n"
            "      SHARED FUNCTION Issue()\n"
            "          Counter.NextId = Counter.NextId + 1\n"
            "          RETURN Counter.NextId\n"
            "      END FUNCTION\n"
            "      FUNCTION Increment(amount AS Number = 1) AS Number\n"
            "          SELF.Value = SELF.Value + amount\n"
            "          RETURN SELF.Value\n"
            "      END FUNCTION\n"
            "  END CLASS\n"
            "\n"
            "Create and use instances:\n"
            "  counter = Counter(10)\n"
            "  PRINT counter.Increment()\n"
            "  PRINT Counter.Issue()\n"
            "  PRINT Counter.NextId\n"
            "\n"
            "Inheritance:\n"
            "  CLASS Cat EXTENDS Animal\n"
            "      FUNCTION Speak()\n"
            "          RETURN SUPER.Speak() + \" and meows\"\n"
            "      END FUNCTION\n"
            "  END CLASS\n"
            "\n"
            "Inspection:\n"
            "  PRINT CLASSOF(cat)\n"
            "  PRINT ISA(cat, \"Animal\")\n"
            "  PRINT IMPLEMENTS(writer, \"Writer\")\n"
            "\n"
            "Interfaces and abstract methods:\n"
            "  INTERFACE Writer\n"
            "      FUNCTION Write(text AS String) AS String\n"
            "  END INTERFACE\n"
            "  CLASS BufferWriter IMPLEMENTS Writer\n"
            "      FUNCTION Write(text AS String) AS String\n"
            "          RETURN text\n"
            "      END FUNCTION\n"
            "  END CLASS\n"
            "  CLASS Shape\n"
            "      ABSTRACT FUNCTION Area()\n"
            "  END CLASS\n"
            "\n"
            "Current alpha model:\n"
            "  Instances are objects marked with their class name.\n"
            "  CONSTRUCTOR is the preferred initializer syntax.\n"
            "  Init remains supported as the underlying compatibility hook.\n"
            "  Methods use SELF to read and write instance fields.\n"
            "  SHARED fields and methods live on ClassName.Member.\n"
            "  Fields, parameters, and function returns may use AS Type runtime checks.\n"
            "  Type checks support core values plus class and interface names.\n"
            "  Typed fields may start as NULL; non-null assignments must match.\n"
            "  PUBLIC is the default; PRIVATE blocks outside field and method access.\n"
            "  PROTECTED allows access from the declaring class and subclasses.\n"
            "  IMPLEMENTS validates required interface methods and typed signatures.\n"
            "  Classes with unresolved ABSTRACT methods cannot be instantiated.\n"
            "  EXTENDS inherits fields and falls back to parent methods.\n"},
        {"shell",
            "ArcoSH usage\n"
            "\n"
            "Run a script:\n"
            "  arcosh script.arcsh arg1 arg2\n"
            "  ./script.abas arg1 arg2\n"
            "  @script.abas arg1 arg2\n"
            "  RUN script.abas arg1 arg2\n"
            "  LOAD script.abas\n"
            "  RUN\n"
            "\n"
            "Executable script header:\n"
            "  #!/usr/bin/env arcosh\n"
            "  PRINT \"hello\"\n"
            "\n"
            "Run a host command and exit:\n"
            "  arcosh -c \"ls -la\"\n"
            "\n"
            "Skip startup files for recovery/debugging:\n"
            "  arcosh --no-rc\n"
            "  arcosh --norc\n"
            "  arcosh --safe\n"
            "\n"
            "Load one explicit startup file:\n"
            "  arcosh --rc ~/.arcosh/minimal.abas\n"
            "\n"
            "Create ~/.arcosh defaults:\n"
            "  arcosh --init-profile\n"
            "\n"
            "Open the login shell wizard:\n"
            "  arcosh --install-shell\n"
            "\n"
            "Check install/profile health:\n"
            "  arcosh --doctor\n"
            "\n"
            "Start the REPL:\n"
            "  arcosh\n"
            "  arcosh --repl\n"
            "\n"
            "Start the interactive tutorial:\n"
            "  arcosh --tutorial\n"
            "\n"
            "REPL commands:\n"
            "  HELP [topic]\n"
            "  TUTORIAL\n"
            "  INSTALL-LOGIN\n"
            "  VERSION\n"
            "  CLS\n"
            "  complete PREFIX\n"
            "  history, history clear\n"
            "  alias, alias name=value, unalias name\n"
            "  source file.abas [args...]\n"
            "  type NAME, which NAME\n"
            "  jobs, jobs -c, fg [job-id], bg [job-id], kill [-signal] [job-id], disown [job-id]\n"
            "  cd [path], cd -, pwd\n"
            "  export NAME=value, unset NAME, env\n"
            "  LIST, NEW, RUN for line-numbered inline programs\n"
            "  GOTO line-number works in numbered programs\n"
            "  STOP ends the current program and returns to ArcoSH\n"
            "  COLOR ON | COLOR OFF\n"
            "  oops <command> after an unknown command to retry with corrected spelling\n"
            "  EXIT\n"
            "  Any other bare command, such as ls or git status, runs as a foreground host command.\n"
            "  Pipes, redirection, and final & background jobs are available for bare commands.\n"
            "  Scripts can still use RUN(\"command\") when they need captured Output and ExitCode.\n"
            "  Temporary assignment prefixes work for one host command: NAME=value command\n"
            "  Multiline IF/WHILE/FOR/FUNCTION/TRY/FLAGS blocks can be typed directly.\n"
            "\n"
            "Shell scripts can use the same ArcoBASIC syntax plus host built-ins.\n"},
        {"login",
            "Install ArcoSH as a login shell\n"
            "\n"
            "Run the interactive wizard:\n"
            "  arcosh --install-shell\n"
            "  INSTALL-LOGIN         (from inside an ArcoSH session)\n"
            "\n"
            "The wizard is written in ArcoBASIC and can be inspected or edited:\n"
            "  scripts/arcosh/install-login-shell.abas\n"
            "  /usr/share/arcosh/scripts/install-login-shell.abas\n"
            "\n"
            "It can:\n"
            "  create ~/.arcosh directories\n"
            "  write a prompt preset to ~/.arcosh/rc.abas\n"
            "  add arcosh to /etc/shells with sudo\n"
            "  run chsh for the selected user\n"
            "\n"
            "The default mode is a dry run. Use arcosh --safe for recovery if a\n"
            "startup file causes problems.\n"},
        {"sudo",
            "Elevated permissions\n"
            "\n"
            "Scripts can request sudo authentication before running privileged steps:\n"
            "  IF ArcoSH.RequireSudo() THEN\n"
            "      RUN(\"sudo systemctl restart example\")\n"
            "  END IF\n"
            "\n"
            "Custom prompt:\n"
            "  ok = ArcoSH.RequireSudo(\"Enter sudo password to continue > \")\n"
            "  ok = Sudo.Require(\"Enter sudo password to continue > \")\n"
            "\n"
            "The helper runs sudo -v so later sudo commands can reuse the credential\n"
            "timestamp. It returns TRUE on success and FALSE on failure.\n"
            "\n"
            "In an interactive ArcoSH terminal, bare commands that start with sudo\n"
            "run in the foreground with inherited terminal input/output so password\n"
            "prompts render normally instead of being captured by RUN().\n"},
        {"executable",
            "Executable ArcoBASIC scripts\n"
            "\n"
            "Create a script with an arcosh shebang, mark it executable, then run it\n"
            "directly from your host shell:\n"
            "  #!/usr/bin/env arcosh\n"
            "  PRINT \"hello from ArcoSH\"\n"
            "  FOR arg IN Args\n"
            "      PRINT arg\n"
            "  NEXT\n"
            "\n"
            "Host shell:\n"
            "  chmod +x script.abas\n"
            "  ./script.abas first second\n"
            "\n"
            "ArcoSH ignores the shebang line before parsing, so the same file can still\n"
            "be run with:\n"
            "  arcosh script.abas first second\n"},
        {"aliases",
            "Aliases, source, and script arguments\n"
            "\n"
            "Aliases:\n"
            "  alias ll=ls -la\n"
            "  alias ll\n"
            "  alias\n"
            "  unalias ll\n"
            "\n"
            "Aliases can also be configured from rc files:\n"
            "  ArcoSH.Alias(\"ll\", \"ls -la\")\n"
            "  ArcoSH.Unalias(\"ll\")\n"
            "\n"
            "Source a script into the current runtime:\n"
            "  source ~/.arcosh/scripts/helpers.abas\n"
            "  . ~/.arcosh/scripts/helpers.abas\n"
            "\n"
            "Script arguments:\n"
            "  arcosh deploy.abas staging web01\n"
            "  source deploy.abas staging web01\n"
            "\n"
            "Scripts receive:\n"
            "  Args              Array of argument strings\n"
            "  Script.Path       Current script path\n"
            "  Script.Name       Current script filename\n"
            "  Script.ArgCount   Number of arguments\n"
            "\n"
            "Inspect command resolution:\n"
            "  type ll\n"
            "  which ls\n"},
        {"editing",
            "Line editing and history\n"
            "\n"
            "Interactive ArcoSH sessions support basic terminal line editing without\n"
            "requiring readline as an external dependency.\n"
            "\n"
            "Keys:\n"
            "  Up / Down       Previous and next history entry\n"
            "  Tab             Complete commands, help topics, scripts, and paths\n"
            "  Left / Right    Move cursor\n"
            "  Home / Ctrl-A   Move to start of line\n"
            "  End / Ctrl-E    Move to end of line\n"
            "  Backspace       Delete previous character\n"
            "  Delete          Delete character under cursor\n"
            "  Ctrl-C          Cancel the current input line\n"
            "  Ctrl-D          Exit when the input line is empty\n"
            "\n"
            "History file:\n"
            "  ~/.arcosh/history\n"
            "\n"
            "Commands:\n"
            "  complete PREFIX Show completion candidates\n"
            "  history         Show current command history\n"
            "  history clear   Clear in-memory and saved history\n"},
        {"profile",
            "ArcoSH profile directory\n"
            "\n"
            "ArcoSH reads user configuration from ~/.arcosh. Set ARCOSH_HOME to\n"
            "override this location for tests, portable installs, or separate profiles.\n"
            "\n"
            "Bootstrap a starter profile:\n"
            "  arcosh --init-profile\n"
            "\n"
            "Startup files:\n"
            "  ~/.arcosh/rc.abas      Loaded on shell startup\n"
            "  ~/.arcosh/rc.arcsh     Also loaded when present\n"
            "  ~/.arcosh/login.abas   Loaded after plugins for --login / -l shells\n"
            "\n"
            "Plugins:\n"
            "  ~/.arcosh/plugins/*.abas\n"
            "  ~/.arcosh/plugins/*.arcsh\n"
            "  Plugins are loaded in filename order after rc files.\n"
            "\n"
            "User-space mods:\n"
            "  ~/.arcosh/mods/*.abas       Installed mods\n"
            "  ~/.arcosh/mods/enabled.txt  Active mod state for next startup\n"
            "  Mod.Install(path)\n"
            "  Mod.List()\n"
            "  Mod.Activate(name)\n"
            "  Mod.Deactivate(name)\n"
            "  Mod.Load(name)              Load an installed mod now\n"
            "  Active mods load after plugins and before login files.\n"
            "\n"
            "Reusable scripts:\n"
            "  ~/.arcosh/scripts/\n"
            "  This directory is prepended to PATH. ArcoBASIC scripts in it can also\n"
            "  be launched by name from the REPL, for example:\n"
            "      ~/.arcosh/scripts/status.abas\n"
            "      arcosh> status\n"
            "\n"
            "Script helpers:\n"
            "  PRINT ArcoSH.Home()\n"
            "  PRINT ArcoSH.PluginsDir()\n"
            "  PRINT ArcoSH.ModsDir()\n"
            "  PRINT ArcoSH.ScriptsDir()\n"
            "  ArcoSH.Source(ArcoSH.Home() + \"/extra.abas\")\n"
            "  ArcoSH.SetPrompt(\"{user}@{host}:{cwd:short} [{status}]> \")\n"
            "\n"
            "Prompt tokens:\n"
            "  {user}, {host}, {cwd}, {cwd:short}, {status}, {shell}\n"
            "\n"
            "Shell variable expansion works in shell commands and shell built-ins:\n"
            "  cd $HOME\n"
            "  printf ${USER}\n"
            "  printf $?\n"
            "\n"
            "Use arcosh --no-rc or --safe to start without profile files.\n"
            "Use arcosh --rc FILE to load one explicit startup file.\n"},
        {"run",
            "RUN command helper\n"
            "\n"
            "Statement form prints command output:\n"
            "  RUN \"printf hello\"\n"
            "\n"
            "REPL script launch form runs .abas files:\n"
            "  RUN script.abas arg1 arg2\n"
            "  @script.abas arg1 arg2\n"
            "\n"
            "Expression form returns an object:\n"
            "  result = RUN(\"printf hello\")\n"
            "  PRINT result.Output\n"
            "  PRINT result.ExitCode\n"
            "  PRINT result.Error\n"
            "\n"
            "Bare REPL commands use the host shell, so pipes and redirection work:\n"
            "  ls | grep .abas\n"
            "  printf hello > /tmp/hello.txt\n"
            "  cat < /tmp/hello.txt\n"
            "\n"
            "Background jobs:\n"
            "  sleep 60 &\n"
            "  jobs\n"
            "  fg 1\n"
            "  bg 1\n"
            "  kill 1\n"
            "  disown 1\n"
            "\n"
            "REPL correction:\n"
            "  If a command fails as unknown, retry it with corrected spelling:\n"
            "  gti status\n"
            "  oops git\n"
            "  The corrected command keeps the original arguments.\n"},
        {"jobs",
            "Background jobs\n"
            "\n"
            "Start a bare host command in the background with final &:\n"
            "  sleep 60 &\n"
            "\n"
            "Inspect jobs:\n"
            "  jobs\n"
            "  jobs -c        Remove completed jobs from the job table\n"
            "\n"
            "Wait for a job in the foreground:\n"
            "  fg 1\n"
            "  fg %1\n"
            "\n"
            "Show/background an existing job:\n"
            "  bg 1\n"
            "\n"
            "Terminate a job:\n"
            "  kill 1\n"
            "  kill %1\n"
            "  kill -9 1\n"
            "\n"
            "Forget a job without terminating it:\n"
            "  disown 1\n"
            "\n"
            "ArcoSH tracks process ids, process groups, exit status, and signal termination.\n"
            "\n"
            "Script helpers:\n"
            "  job = ArcoSH.StartJob(\"sleep 60\")\n"
            "  PRINT job.Id\n"
            "  FOR job IN ArcoSH.Jobs()\n"
            "      PRINT job.Command\n"
            "  NEXT\n"
            "  ArcoSH.KillJob(job.Id)\n"
            "  status = ArcoSH.WaitJob(job.Id)\n"
            "  ArcoSH.DisownJob(job.Id)\n"
            "\n"
            "Foreground terminal handoff is available for interactive commands.\n"
            "Ctrl-Z suspension is planned next.\n"},
        {"files",
            "File helpers\n"
            "\n"
            "  File.Exists(path)    Returns TRUE when path exists\n"
            "  File.List(directory) Returns sorted file metadata objects\n"
            "  File.ReadText(path)  Reads a text file\n"
            "  File.WriteText(path, text)\n"
            "  File.AppendText(path, text)\n"
            "  File.Find(pattern)   Returns matching paths for * and ? wildcards\n"
            "  Directory.Create(path)\n"
            "  Directory.Exists(path)\n"
            "\n"
            "Path helpers:\n"
            "  Path.Join(a, b, ...)\n"
            "  Path.Home()\n"
            "  Path.BaseName(path)\n"
            "  Path.DirName(path)\n"
            "  Path.Extension(path)\n"
            "\n"
            "Example:\n"
            "  Directory.Create(\"logs\")\n"
            "  path = Path.Join(\"logs\", \"today.txt\")\n"
            "  File.WriteText(path, \"started\\n\")\n"
            "  File.AppendText(path, \"done\\n\")\n"
            "  FOR file IN File.Find(\"logs/*.txt\")\n"
            "      PRINT file\n"
            "  NEXT\n"},
        {"host",
            "Host helpers\n"
            "\n"
            "  Host.OSName()       Returns Windows, macOS, Linux, or Unknown\n"
            "  Host.Hostname()     Returns the current host name\n"
            "  Host.IsWindows()    Returns TRUE on Windows\n"
            "  Host.Processes()    Returns process objects with Pid and Name fields\n"
            "  Host.Printers()     Returns printer objects with Name and Default fields\n"
            "\n"
            "Process helpers:\n"
            "  Process.List()\n"
            "  Process.Exists(name)\n"
            "  Process.Kill(pid)\n"
            "  Process.Kill(pid, signal)\n"
            "\n"
            "System helpers:\n"
            "  System.Capabilities()\n"
            "  System.CommandExists(name)\n"
            "  System.Open(pathOrUrl)\n"
            "  System.Launch(command)\n"},
        {"system",
            "System runtime helpers\n"
            "\n"
            "Use System.Capabilities() before calling OS-backed features. It returns\n"
            "an object with OS, Hostname, GUI, Networking, OpenCommand, Printing,\n"
            "PrinterCommand, Processes, and Shell fields.\n"
            "\n"
            "Helpers:\n"
            "  System.Capabilities()\n"
            "  System.CommandExists(\"git\")\n"
            "  System.Open(\"report.html\")       Opens with the desktop handler\n"
            "  System.Open(\"https://example\")   Opens with the default browser\n"
            "  System.Launch(\"gedit report.txt\")\n"
            "\n"
            "Printing:\n"
            "  FOR printer IN Printer.List()\n"
            "      PRINT printer.Name\n"
            "  NEXT\n"
            "  PRINT Printer.Default()\n"
            "  result = Printer.PrintFile(\"report.pdf\")\n"
            "\n"
            "System.Open and Printer.PrintFile return clear runtime errors when the\n"
            "host does not expose the needed OS command.\n"},
        {"network",
            "Network helpers\n"
            "\n"
            "HTTP networking is available when ArcoBASIC is built with libcurl.\n"
            "Check Network.Available() or System.Capabilities().Networking before\n"
            "presenting network-dependent workflows.\n"
            "\n"
            "Helpers:\n"
            "  Network.Available()\n"
            "  response = Network.Get(url)\n"
            "  response = Network.Get(url, {\"Accept\": \"application/json\"})\n"
            "  response = Network.Post(url, body, \"application/json\")\n"
            "  response = Network.Download(url, path)\n"
            "  encoded = Network.UrlEncode(\"hello world\")\n"
            "  decoded = Network.UrlDecode(\"hello%20world\")\n"
            "  query = Network.QueryString({\"q\": \"arco basic\"})\n"
            "  dns = Network.ResolveDNS(\"localhost\")\n"
            "  dns = Net.ResolveDNS(\"localhost\")\n"
            "  client = Net.TcpConnect(\"example.com\", 80)\n"
            "  Net.TcpSend(client.Client, \"GET / HTTP/1.0\\r\\nHost: example.com\\r\\n\\r\\n\")\n"
            "  chunk = Net.TcpRead(client.Client, 4096)\n"
            "  Net.TcpClose(client.Client)\n"
            "\n"
            "Responses are objects with Ok, Status, Body, Headers, Error, and Url.\n"
            "Download also adds Path after a successful write.\n"
            "DNS responses include Ok, Host, Addresses, and Error.\n"
            "TCP connect responses include Ok, Client, Host, Port, Address, and Error.\n"
            "\n"
            "Example:\n"
            "  IF Network.Available() THEN\n"
            "      page = Network.Get(\"https://example.com\")\n"
            "      IF page.Ok THEN PRINT page.Body\n"
            "  END IF\n"},
        {"gui",
            "Desktop GUI helpers (Linux preview)\n"
            "\n"
            "The public API is independent of Wayland, X11, GTK, Qt, KDE, GNOME,\n"
            "and Xfce. The current GLFW transport chooses Wayland or X11 at runtime.\n"
            "Coordinates are logical window pixels and colors use 0.0 to 1.0 channels.\n"
            "\n"
            "  GUI.Available()              GUI.Backend()\n"
            "  GUI.Application(id, name, iconPath)\n"
            "  window = GUI.Window(title, width, height)\n"
            "  GUI.Clear(window, r, g, b)\n"
            "  GUI.Rectangle(window, x, y, width, height, r, g, b)\n"
            "  GUI.RoundedRectangle(window, x, y, width, height, radius, r, g, b)\n"
            "  GUI.Line(window, x1, y1, x2, y2, thickness, r, g, b)\n"
            "  GUI.Circle(window, centerX, centerY, radius, r, g, b)\n"
            "  GUI.Text(window, text, x, y, size, r, g, b)\n"
            "  GUI.Image(window, pngPath, x, y, width, height, opacity)\n"
            "  size = GUI.MeasureText(window, text, size)\n"
            "  GUI.SetClip(window, x, y, width, height) / GUI.ResetClip(window)\n"
            "  GUI.ClipboardText(window) / GUI.SetClipboardText(window, text)\n"
            "  GUI.SetCursor(window, \"default\" | \"text\" | \"hand\")\n"
            "  path = GUI.OpenFileDialog(window, title, initialPath)\n"
            "  path = GUI.SaveFileDialog(window, title, initialPath)\n"
            "  answer = GUI.Confirm(window, title, message)\n"
            "  GUI.Present(window)\n"
            "  event = GUI.PollEvent()\n"
            "  event = GUI.WaitEvent(0.05)\n"
            "  GUI.ShouldClose(window)      GUI.Size(window)\n"
            "  GUI.SetTitle(window, title)  GUI.Close(window)\n"
            "\n"
            "Events include close, resize, key, text, pointer-move, pointer-button,\n"
            "and scroll. See examples/gui_window.abas for an interactive button.\n"},
        {"printers",
            "Printer helpers\n"
            "\n"
            "  Host.Printers()\n"
            "  Printer.List()\n"
            "  Printer.Default()\n"
            "  Printer.PrintFile(path)\n"
            "  Printer.PrintFile(path, printerName)\n"
            "\n"
            "Linux and macOS use CUPS tools such as lpstat, lp, and lpr. Windows uses\n"
            "the system print verb when available. Check System.Capabilities().Printing\n"
            "before presenting print UI.\n"},
        {"values",
            "Values\n"
            "\n"
            "Implemented value types:\n"
            "  NULL, TRUE/FALSE, numbers, strings, arrays, objects\n"
            "\n"
            "Arrays:\n"
            "  items = [1, 2, 3]\n"
            "  PRINT LEN(items)\n"
            "\n"
            "Objects:\n"
            "  person = {\"Name\": \"Ada\", \"Age\": 36}\n"
            "  PRINT person.Name\n"},
        {"examples",
            "Examples\n"
            "\n"
            "  PRINT \"Hello\"\n"
            "\n"
            "  IF File.Exists(\"readme.md\") THEN\n"
            "      PRINT File.ReadText(\"readme.md\")\n"
            "  END IF\n"
            "\n"
            "  result = RUN(\"printf shell-test\")\n"
            "  IF result.ExitCode = 0 THEN\n"
            "      PRINT result.Output\n"
            "  END IF\n"}
        ,
        {"colors",
            "Color output\n"
            "\n"
            "REPL commands:\n"
            "  COLOR ON\n"
            "  COLOR OFF\n"
            "\n"
            "CLI flags:\n"
            "  arcosh --no-color\n"
            "  arcosh --color\n"
            "\n"
            "Script helpers:\n"
            "  PRINT Color.Green(\"ok\")\n"
            "  PRINT Color.Red(\"failed\")\n"
            "  PRINT Color.Bold(\"important\")\n"
            "  FOR name IN Color.Names()\n"
            "      PRINT name\n"
            "  NEXT\n"
            "\n"
            "External commands:\n"
            "  When color is enabled, ArcoSH asks common tools to keep colors even\n"
            "  though command output is captured through a pipe. Supported now:\n"
            "  ls, grep, egrep, fgrep, rg, git, diff.\n"
            "  Use --no-color or COLOR OFF for plain output.\n"
            "\n"
            "Examples:\n"
            "  ls\n"
            "  grep TODO readme.md\n"
            "  git status\n"
            "\n"
            "Limitation:\n"
            "  Only the first command in a pipeline is rewritten today.\n"
            "  Prefer explicit flags for later pipeline stages.\n"
            "  Example: ls | grep --color=always md\n"}
    };
    return catalog;
}

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

#ifndef _WIN32
int run_foreground_shell_command(const std::string& command) {
    std::cout << std::flush;
    std::cerr << std::flush;

    struct sigaction previous_int {};
    struct sigaction previous_ttou {};
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    sigaction(SIGINT, &ignore, &previous_int);
    sigaction(SIGTTOU, &ignore, &previous_ttou);

    const pid_t shell_pgrp = getpgrp();
    const pid_t previous_foreground_pgrp = isatty(STDIN_FILENO) ? tcgetpgrp(STDIN_FILENO) : -1;

    const pid_t pid = fork();
    if (pid < 0) {
        sigaction(SIGINT, &previous_int, nullptr);
        sigaction(SIGTTOU, &previous_ttou, nullptr);
        throw std::runtime_error("could not start command");
    }
    if (pid == 0) {
        setpgid(0, 0);
        restore_default_sigint();
        sigaction(SIGTTOU, &previous_ttou, nullptr);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    setpgid(pid, pid);
    if (previous_foreground_pgrp >= 0) {
        tcsetpgrp(STDIN_FILENO, pid);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        if (previous_foreground_pgrp >= 0) {
            tcsetpgrp(STDIN_FILENO, previous_foreground_pgrp);
        } else if (isatty(STDIN_FILENO)) {
            tcsetpgrp(STDIN_FILENO, shell_pgrp);
        }
        sigaction(SIGINT, &previous_int, nullptr);
        sigaction(SIGTTOU, &previous_ttou, nullptr);
        throw std::runtime_error("could not wait for command");
    }

    if (previous_foreground_pgrp >= 0) {
        tcsetpgrp(STDIN_FILENO, previous_foreground_pgrp);
    } else if (isatty(STDIN_FILENO)) {
        tcsetpgrp(STDIN_FILENO, shell_pgrp);
    }
    sigaction(SIGINT, &previous_int, nullptr);
    sigaction(SIGTTOU, &previous_ttou, nullptr);
    return status_from_wait_status(status);
}

std::string foreground_shell_command(const std::string& command) {
    const auto words = split_shell_words(command);
    if (!words.empty() && lowercase(words.front()) == "sudo") {
        return "trap - INT; env -u SUDO_ASKPASS " + command;
    }
    return "trap - INT; " + force_command_color(command);
}
#endif

Value run_command(const std::string& command) {
    std::array<char, 256> chunk{};
    std::string output;
#ifndef _WIN32
    const auto words = split_shell_words(command);
    if (!words.empty() && lowercase(words.front()) == "sudo" && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        const int status = run_foreground_shell_command(foreground_shell_command(command));
        return Value::Object{{"Output", ""}, {"ExitCode", static_cast<double>(status)}, {"Error", ""}};
    }
#endif
    const std::string captured_command =
#ifdef _WIN32
        force_command_color(command) + " 2>&1";
#else
        "trap - INT; " + force_command_color(command) + " 2>&1";
#endif
#ifdef _WIN32
    FILE* pipe = _popen(captured_command.c_str(), "r");
#else
    FILE* pipe = popen(captured_command.c_str(), "r");
#endif
    if (!pipe) {
        throw std::runtime_error("could not start command");
    }
    while (fgets(chunk.data(), static_cast<int>(chunk.size()), pipe) != nullptr) {
        output += chunk.data();
    }
#ifdef _WIN32
    const int raw_code = _pclose(pipe);
    const int code = raw_code;
#else
    const int raw_code = pclose(pipe);
    const int code = status_from_wait_status(raw_code);
#endif
    if (!output.empty() && output.back() == '\n') {
        output.pop_back();
    }
    return Value::Object{{"Output", output}, {"ExitCode", static_cast<double>(code)}, {"Error", ""}};
}

bool require_sudo_auth(const std::string& prompt) {
#ifdef _WIN32
    (void)prompt;
    return false;
#else
    const std::string safe_prompt = prompt.empty() ? "ArcoSH sudo password > " : prompt;
    // Print and flush the label ourselves instead of handing it to sudo's "-p": sudo
    // writes its prompt from a separate process, so its write and arcosh's own buffered
    // PRINT output (e.g. the wizard's preceding notice box) race for the terminal with
    // no ordering guarantee, and password reading can start before either is visible.
    // Flushing here first, then printing our own label with an explicit flush, and
    // finally telling sudo to render an empty prompt ("-p ''") makes the visible order
    // deterministic no matter how the two processes' writes would otherwise interleave.
    std::cout << safe_prompt << std::flush;
    // "trap - INT" restores default SIGINT handling for the shell child: arcosh
    // ignores SIGINT for its own REPL loop, and that SIG_IGN disposition survives
    // fork+exec unless explicitly reset.
    const std::string command =
        "trap - INT; env -u SUDO_ASKPASS timeout --foreground 60 sudo -v -p ''";
    const int status = run_foreground_shell_command(command);
    std::cout << '\n';
    return status == 0;
#endif
}

void reap_background_jobs() {
#ifndef _WIN32
    for (auto& job : g_jobs) {
        if (!job.running) {
            continue;
        }
        int status = 0;
        const pid_t result = waitpid(static_cast<pid_t>(job.pid), &status, WNOHANG);
        if (result == static_cast<pid_t>(job.pid)) {
            job.running = false;
            job.status = status_from_wait_status(status);
            job.signal = signal_from_wait_status(status);
        }
    }
#endif
}

void prune_done_jobs() {
    reap_background_jobs();
    g_jobs.erase(std::remove_if(g_jobs.begin(), g_jobs.end(), [](const ShellJob& job) {
        return !job.running;
    }), g_jobs.end());
}

int start_background_job(const std::string& command, std::ostream& output) {
#ifdef _WIN32
    (void)command;
    output << style("background jobs are not supported on Windows yet", "yellow") << '\n';
    return 1;
#else
    const std::string job_command = remove_background_marker(command);
    if (job_command.empty()) {
        output << style("usage: command &", "yellow") << '\n';
        return 1;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        output << style("could not start background job", "red") << '\n';
        return 1;
    }
    if (pid == 0) {
        restore_default_sigint();
        setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", job_command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    setpgid(pid, pid);
    ShellJob job;
    job.id = g_next_job_id++;
    job.pid = static_cast<int>(pid);
    job.command = job_command;
    g_jobs.push_back(job);
    output << '[' << job.id << "] " << job.pid << '\n';
    return 0;
#endif
}

void print_jobs(std::ostream& output) {
    reap_background_jobs();
    for (const auto& job : g_jobs) {
        output << '[' << job.id << "] "
               << (job.running ? "Running" : "Done")
               << " pid=" << job.pid;
        if (!job.running) {
            output << ' ' << command_status_text(job.status, job.signal);
        }
        output << "  " << job.command << '\n';
    }
}

std::optional<std::size_t> find_job_index(int id) {
    for (std::size_t i = 0; i < g_jobs.size(); ++i) {
        if (g_jobs[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<int> latest_job_id() {
    if (g_jobs.empty()) {
        return std::nullopt;
    }
    return g_jobs.back().id;
}

std::optional<int> parse_job_id(const std::string& spec, std::ostream& output, const std::string& usage) {
    if (trim(spec).empty()) {
        const auto latest = latest_job_id();
        if (!latest) {
            output << style("no current job", "yellow") << '\n';
        }
        return latest;
    }
    try {
        const std::string text = trim(spec);
        return std::stoi(text[0] == '%' ? text.substr(1) : text);
    } catch (const std::exception&) {
        output << style(usage, "yellow") << '\n';
        return std::nullopt;
    }
}

int foreground_job(int id, std::ostream& output) {
#ifdef _WIN32
    (void)id;
    output << style("job control is not supported on Windows yet", "yellow") << '\n';
    return 1;
#else
    reap_background_jobs();
    const auto index = find_job_index(id);
    if (!index) {
        output << style("fg: job not found", "yellow") << '\n';
        return 1;
    }
    auto& job = g_jobs[*index];
    if (!job.running) {
        output << '[' << job.id << "] Done " << command_status_text(job.status, job.signal) << "  " << job.command << '\n';
        return job.status;
    }
    output << job.command << '\n';
    int status = 0;
    if (waitpid(static_cast<pid_t>(job.pid), &status, 0) < 0) {
        output << style("fg: wait failed", "red") << '\n';
        return 1;
    }
    job.running = false;
    job.status = status_from_wait_status(status);
    job.signal = signal_from_wait_status(status);
    return job.status;
#endif
}

int background_job(int id, std::ostream& output) {
    reap_background_jobs();
    const auto index = find_job_index(id);
    if (!index) {
        output << style("bg: job not found", "yellow") << '\n';
        return 1;
    }
    const auto& job = g_jobs[*index];
    output << '[' << job.id << "] " << (job.running ? "Running" : "Done");
    if (!job.running) {
        output << ' ' << command_status_text(job.status, job.signal);
    }
    output << "  " << job.command << '\n';
    return job.running ? 0 : job.status;
}

int kill_job(int id, int signal, std::ostream& output) {
#ifdef _WIN32
    (void)id;
    (void)signal;
    output << style("job kill is not supported on Windows yet", "yellow") << '\n';
    return 1;
#else
    reap_background_jobs();
    const auto index = find_job_index(id);
    if (!index) {
        output << style("kill: job not found", "yellow") << '\n';
        return 1;
    }
    auto& job = g_jobs[*index];
    if (!job.running) {
        output << '[' << job.id << "] Done " << command_status_text(job.status, job.signal) << "  " << job.command << '\n';
        return job.status;
    }
    if (kill(-job.pid, signal) != 0 && kill(job.pid, signal) != 0) {
        output << style(std::string("kill: ") + std::strerror(errno), "red") << '\n';
        return 1;
    }
    output << '[' << job.id << "] Sent signal " << signal << "  " << job.command << '\n';
    return 0;
#endif
}

int disown_job(int id, std::ostream& output) {
    const auto index = find_job_index(id);
    if (!index) {
        output << style("disown: job not found", "yellow") << '\n';
        return 1;
    }
    output << '[' << g_jobs[*index].id << "] disowned  " << g_jobs[*index].command << '\n';
    g_jobs.erase(g_jobs.begin() + static_cast<std::ptrdiff_t>(*index));
    return 0;
}

Value job_to_value(const ShellJob& job) {
    return Value::Object{
        {"Id", static_cast<double>(job.id)},
        {"Pid", static_cast<double>(job.pid)},
        {"Command", job.command},
        {"Running", job.running},
        {"Status", static_cast<double>(job.status)},
        {"Signal", static_cast<double>(job.signal)}
    };
}

Value jobs_value() {
    reap_background_jobs();
    Value::Array jobs;
    for (const auto& job : g_jobs) {
        jobs.push_back(job_to_value(job));
    }
    return jobs;
}

std::string shell_quote(const std::string& value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (char c : value) {
        if (c == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(c);
        }
    }
    quoted += "\"";
    return quoted;
#else
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(c);
        }
    }
    quoted += "'";
    return quoted;
#endif
}

bool command_exists(const std::string& command) {
    return find_external_command(command).has_value();
}

std::string os_name() {
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

std::string hostname() {
    const char* env_host = std::getenv("HOSTNAME");
    if (env_host && *env_host) {
        return env_host;
    }
#ifndef _WIN32
    char buffer[256]{};
    if (gethostname(buffer, sizeof(buffer) - 1) == 0) {
        return buffer;
    }
#endif
    return "localhost";
}

std::string username() {
    if (const char* user = std::getenv("USER")) {
        if (*user) {
            return user;
        }
    }
    if (const char* username_env = std::getenv("USERNAME")) {
        if (*username_env) {
            return username_env;
        }
    }
    return "user";
}

std::string short_cwd() {
    const std::string cwd = std::filesystem::current_path().string();
    const std::string home = home_directory();
    if (!home.empty() && (cwd == home || starts_with(cwd, home + "/"))) {
        return "~" + cwd.substr(home.size());
    }
    return cwd;
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
}

std::string prompt_pattern(Runtime& runtime) {
    try {
        return runtime.get_global("ArcoSH.Prompt").to_string();
    } catch (const std::exception&) {
        return "{cwd:short}> ";
    }
}

std::string render_prompt(Runtime& runtime, int multiline_depth, int last_status) {
    if (multiline_depth > 0) {
        return style("...", "brightblack") + style(">", "brightblack") + " ";
    }

    runtime.set_global("ArcoSH.LastStatus", static_cast<double>(last_status));
    std::string prompt = prompt_pattern(runtime);
    replace_all(prompt, "{user}", username());
    replace_all(prompt, "{host}", hostname());
    replace_all(prompt, "{cwd}", std::filesystem::current_path().string());
    replace_all(prompt, "{cwd:short}", short_cwd());
    replace_all(prompt, "{status}", std::to_string(last_status));
    replace_all(prompt, "{shell}", "arcosh");
    return prompt;
}

bool wildcard_match(const std::string& pattern, const std::string& text) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string::npos;
    std::size_t match = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            p++;
            t++;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            match = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++match;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        p++;
    }
    return p == pattern.size();
}

Value find_files(const std::string& pattern) {
    Value::Array files;
    const std::filesystem::path pattern_path(pattern);
    const auto directory = pattern_path.has_parent_path() ? pattern_path.parent_path() : std::filesystem::path(".");
    const auto filename_pattern = pattern_path.filename().string();
    if (!std::filesystem::exists(directory)) {
        return files;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const std::string name = entry.path().filename().string();
        if (wildcard_match(filename_pattern, name)) {
            files.emplace_back(entry.path().string());
        }
    }
    return files;
}

Value process_list() {
    Value::Array processes;
#ifdef __linux__
    for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string pid_text = entry.path().filename().string();
        if (pid_text.empty() || pid_text.find_first_not_of("0123456789") != std::string::npos) {
            continue;
        }
        std::ifstream comm(entry.path() / "comm");
        std::string name;
        std::getline(comm, name);
        if (!name.empty()) {
            processes.emplace_back(Value::Object{{"Pid", std::stod(pid_text)}, {"Name", name}});
        }
        if (processes.size() >= 256) {
            break;
        }
    }
#endif
    return processes;
}

std::string system_open_command() {
#ifdef _WIN32
    return "start";
#elif defined(__APPLE__)
    return command_exists("open") ? "open" : "";
#else
    if (command_exists("xdg-open")) {
        return "xdg-open";
    }
    if (command_exists("gio")) {
        return "gio open";
    }
    return "";
#endif
}

bool gui_available() {
#ifdef _WIN32
    return true;
#elif defined(__APPLE__)
    return true;
#else
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    return (display && *display) || (wayland && *wayland);
#endif
}

bool network_available() {
#if defined(ARCO_NETWORK_CURL)
    return true;
#else
    return false;
#endif
}

std::string hex_byte(unsigned char value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string output;
    output.push_back(digits[value >> 4]);
    output.push_back(digits[value & 0x0f]);
    return output;
}

bool printing_available() {
#ifdef _WIN32
    return command_exists("powershell") || command_exists("powershell.exe");
#else
    return command_exists("lp") || command_exists("lpr");
#endif
}

Value system_capabilities() {
    return Value::Object{
        {"OS", os_name()},
        {"Hostname", hostname()},
        {"GUI", gui_available() && gui::available()},
        {"GUIBackend", gui::backend()},
        {"OpenCommand", system_open_command()},
        {"Networking", network_available()},
        {"Printing", printing_available()},
        {"PrinterCommand", command_exists("lp") ? "lp" : (command_exists("lpr") ? "lpr" : "")},
        {"Processes", true},
        {"Shell", true}
    };
}

Value open_target(const std::string& target) {
    const std::string opener = system_open_command();
    if (opener.empty()) {
        throw std::runtime_error("System.Open is not available on this host");
    }
#ifdef _WIN32
    const std::string command = "cmd /c start \"\" " + shell_quote(target);
#else
    const std::string command = opener + " " + shell_quote(target) + " >/dev/null 2>&1 &";
#endif
    const int status = std::system(command.c_str());
    return status_from_wait_status(status) == 0;
}

Value printer_list() {
    Value::Array printers;
#ifdef _WIN32
    Value result = run_command("powershell -NoProfile -Command \"Get-Printer | Select-Object -ExpandProperty Name\"");
    const std::string output = result.get_property("Output").to_string();
    std::istringstream input(output);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (!line.empty()) {
            printers.emplace_back(Value::Object{{"Name", line}, {"Default", false}});
        }
    }
#else
    if (!command_exists("lpstat")) {
        return printers;
    }
    Value names_result = run_command("lpstat -e");
    if (names_result.get_property("ExitCode").as_number() != 0.0) {
        return printers;
    }
    std::string default_name;
    Value default_result = run_command("lpstat -d");
    if (default_result.get_property("ExitCode").as_number() == 0.0) {
        const std::string output = default_result.get_property("Output").to_string();
        const auto colon = output.find(':');
        if (colon != std::string::npos) {
            default_name = trim(output.substr(colon + 1));
        }
    }
    std::istringstream input(names_result.get_property("Output").to_string());
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (!line.empty()) {
            printers.emplace_back(Value::Object{{"Name", line}, {"Default", line == default_name}});
        }
    }
#endif
    return printers;
}

Value default_printer() {
    const auto printers = printer_list().as_array();
    for (const auto& printer : printers) {
        if (printer.is_object() && printer.get_property("Default").truthy()) {
            return printer.get_property("Name");
        }
    }
    return "";
}

Value print_file(const std::string& path, const std::string& printer) {
    if (!std::filesystem::exists(path) || std::filesystem::is_directory(path)) {
        throw std::runtime_error("Printer.PrintFile could not find file: " + path);
    }
#ifdef _WIN32
    std::string command = "powershell -NoProfile -Command \"Start-Process -FilePath " + shell_quote(path) + " -Verb Print\"";
#else
    std::string command;
    if (command_exists("lp")) {
        command = "lp ";
        if (!printer.empty()) {
            command += "-d " + shell_quote(printer) + " ";
        }
        command += shell_quote(path);
    } else if (command_exists("lpr")) {
        command = "lpr ";
        if (!printer.empty()) {
            command += "-P " + shell_quote(printer) + " ";
        }
        command += shell_quote(path);
    } else {
        throw std::runtime_error("Printer.PrintFile is not available on this host");
    }
#endif
    return run_command(command);
}

} // namespace

void set_color_enabled(bool enabled) {
    g_color_enabled = enabled;
}

bool color_enabled() {
    return g_color_enabled;
}

std::string colorize(const std::string& text, const std::string& color) {
    return style(text, color);
}

std::string help_text(const std::string& topic) {
    const std::string requested = trim(topic);
    const std::string key = requested.empty() ? "help" : lowercase(requested);
    const auto& catalog = help_catalog();

    if (starts_with_word(key, "search")) {
        const std::string query = lowercase(trim(requested.substr(6)));
        std::ostringstream matches;
        if (query.empty()) {
            matches << "Usage:\n  HELP search text\n";
            return tui_box("ARCO MANUAL // SEARCH", matches.str());
        }
        matches << "Search: " << query << "\n\n";
        bool any = false;
        for (const auto& [name, text] : catalog) {
            if (lowercase(name).find(query) != std::string::npos || lowercase(text).find(query) != std::string::npos) {
                matches << "  " << name << "\n";
                any = true;
            }
        }
        if (!any) {
            matches << "  no matching help topics\n";
        }
        matches << "\nTry:\n  HELP <topic>\n";
        return tui_box("ARCO MANUAL // SEARCH", matches.str());
    }

    const auto found = catalog.find(key);
    if (found != catalog.end()) {
        return tui_box("ARCO MANUAL // " + key, found->second + "\nQuick keys:\n  HELP topics    HELP search <text>    TUTORIAL");
    }

    std::ostringstream body;
    body << "Unknown help topic: " << topic << "\n\n";
    body << "Closest options:\n";
    int shown = 0;
    for (const auto& [name, text] : catalog) {
        (void)text;
        if (!key.empty() && (name.find(key.substr(0, 1)) != std::string::npos || key.find(name.substr(0, 1)) != std::string::npos)) {
            body << "  " << name << "\n";
            shown++;
            if (shown >= 8) {
                break;
            }
        }
    }
    if (shown == 0) {
        body << "  topics\n  basic\n  shell\n  tutorial\n";
    }
    body << "\nRun HELP topics to list available topics.\n";
    return tui_box("ARCO MANUAL // NOT FOUND", body.str());
}

Value help_topics() {
    Value::Array topics;
    for (const auto& [topic, text] : help_catalog()) {
        (void)text;
        topics.emplace_back(topic);
    }
    return topics;
}

void register_shell_builtins(Runtime& runtime) {
    auto exit_program = [](const std::vector<Value>& args) -> Value {
        if (args.size() > 1) {
            throw std::runtime_error("Exit expects 0 or 1 arguments");
        }
        const int code = args.empty() ? 0 : static_cast<int>(args[0].as_number());
        throw ExitSignal(code);
    };

    runtime.register_function("Exit", exit_program);
    runtime.register_function("exit", exit_program);
    runtime.register_function("ExitProgram", exit_program);
    runtime.register_function("exitProgram", exit_program);
    runtime.register_function("ExitTheProgram", exit_program);
    runtime.register_function("exitTheProgram", exit_program);

    runtime.register_function("ENV", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("ENV expects 1 argument");
        }
        const char* value = std::getenv(args[0].to_string().c_str());
        return value ? Value(value) : Value("");
    });

    auto input_function = [](const std::vector<Value>& args) -> Value {
        if (args.size() > 1) {
            throw std::runtime_error("Input expects 0 or 1 arguments");
        }
        if (!args.empty()) {
            std::cout << args[0].to_string() << std::flush;
        }
        std::string answer;
        if (!std::getline(std::cin, answer)) {
            return "";
        }
        return answer;
    };
    runtime.register_function("Input", input_function);
    runtime.register_function("ReadLine", input_function);

    runtime.register_function("RUN", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("RUN expects 1 argument");
        }
        return run_command(args[0].to_string());
    });

    auto require_sudo_function = [](const std::vector<Value>& args) -> Value {
        if (args.size() > 1) {
            throw std::runtime_error("ArcoSH.RequireSudo expects 0 or 1 arguments");
        }
        const std::string prompt = args.empty() ? "ArcoSH sudo password > " : args[0].to_string();
        return require_sudo_auth(prompt);
    };
    runtime.register_function("ArcoSH.RequireSudo", require_sudo_function);
    runtime.register_function("Sudo.Require", require_sudo_function);

    runtime.register_function("Host.OSName", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("Host.OSName expects no arguments");
        }
        return os_name();
    });

    runtime.register_function("Host.Hostname", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("Host.Hostname expects no arguments");
        }
        return hostname();
    });

    runtime.register_function("Host.IsWindows", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("Host.IsWindows expects no arguments");
        }
#ifdef _WIN32
        return true;
#else
        return false;
#endif
    });

    runtime.register_function("System.Capabilities", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("System.Capabilities expects no arguments");
        }
        return system_capabilities();
    });

    runtime.register_function("System.CommandExists", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("System.CommandExists expects 1 argument");
        }
        return command_exists(args[0].to_string());
    });

    runtime.register_function("GUI.Available", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("GUI.Available expects no arguments");
        return gui_available() && gui::available();
    });
    runtime.register_function("GUI.Backend", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("GUI.Backend expects no arguments");
        return gui::backend();
    });
    runtime.register_function("GUI.Application", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 3) throw std::runtime_error("GUI.Application expects app id, display name, and optional icon path");
        gui::set_application(args[0].to_string(), args[1].to_string(), args.size() == 3 ? args[2].to_string() : "");
        return {};
    });
    runtime.register_function("GUI.Window", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 3) throw std::runtime_error("GUI.Window expects title, width, and height");
        return gui::create_window(args[0].to_string(), static_cast<int>(args[1].as_number()), static_cast<int>(args[2].as_number()));
    });
    runtime.register_function("GUI.Close", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.Close expects a window");
        gui::destroy_window(static_cast<int>(args[0].as_number())); return {};
    });
    runtime.register_function("GUI.ShouldClose", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.ShouldClose expects a window");
        return gui::should_close(static_cast<int>(args[0].as_number()));
    });
    runtime.register_function("GUI.SetShouldClose", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("GUI.SetShouldClose expects a window and boolean");
        gui::set_should_close(static_cast<int>(args[0].as_number()), args[1].truthy()); return {};
    });
    runtime.register_function("GUI.SetTitle", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("GUI.SetTitle expects a window and title");
        gui::set_title(static_cast<int>(args[0].as_number()), args[1].to_string()); return {};
    });
    runtime.register_function("GUI.Size", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.Size expects a window");
        return gui::window_size(static_cast<int>(args[0].as_number()));
    });
    runtime.register_function("GUI.Clear", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 4 || args.size() > 5) throw std::runtime_error("GUI.Clear expects window, red, green, blue, and optional alpha");
        gui::clear(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(), args.size() == 5 ? args[4].as_number() : 1.0); return {};
    });
    runtime.register_function("GUI.Rectangle", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 8 || args.size() > 9) throw std::runtime_error("GUI.Rectangle expects window, x, y, width, height, red, green, blue, and optional alpha");
        gui::rectangle(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(), args[4].as_number(),
                       args[5].as_number(), args[6].as_number(), args[7].as_number(), args.size() == 9 ? args[8].as_number() : 1.0); return {};
    });
    runtime.register_function("GUI.RoundedRectangle", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 9 || args.size() > 10) throw std::runtime_error("GUI.RoundedRectangle expects window, x, y, width, height, radius, red, green, blue, and optional alpha");
        gui::rounded_rectangle(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(), args[4].as_number(), args[5].as_number(),
                               args[6].as_number(), args[7].as_number(), args[8].as_number(), args.size() == 10 ? args[9].as_number() : 1.0); return {};
    });
    runtime.register_function("GUI.Line", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 9 || args.size() > 10) throw std::runtime_error("GUI.Line expects window, x1, y1, x2, y2, thickness, red, green, blue, and optional alpha");
        gui::line(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(), args[4].as_number(), args[5].as_number(),
                  args[6].as_number(), args[7].as_number(), args[8].as_number(), args.size() == 10 ? args[9].as_number() : 1.0); return {};
    });
    runtime.register_function("GUI.Circle", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 7 || args.size() > 8) throw std::runtime_error("GUI.Circle expects window, centerX, centerY, radius, red, green, blue, and optional alpha");
        gui::circle(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(),
                    args[4].as_number(), args[5].as_number(), args[6].as_number(), args.size() == 8 ? args[7].as_number() : 1.0); return {};
    });
    runtime.register_function("GUI.Text", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 8 || args.size() > 9) throw std::runtime_error("GUI.Text expects window, text, x, y, size, red, green, blue, and optional alpha");
        gui::text(static_cast<int>(args[0].as_number()), args[1].to_string(), args[2].as_number(), args[3].as_number(), args[4].as_number(),
                  args[5].as_number(), args[6].as_number(), args[7].as_number(), args.size() == 9 ? args[8].as_number() : 1.0); return {};
    });
    runtime.register_function("GUI.Image", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 6 || args.size() > 7) throw std::runtime_error("GUI.Image expects window, path, x, y, width, height, and optional opacity");
        gui::image(static_cast<int>(args[0].as_number()), args[1].to_string(), args[2].as_number(), args[3].as_number(),
                   args[4].as_number(), args[5].as_number(), args.size() == 7 ? args[6].as_number() : 1.0); return {};
    });
    runtime.register_function("GUI.MeasureText", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 3) throw std::runtime_error("GUI.MeasureText expects window, text, and size");
        return gui::measure_text(static_cast<int>(args[0].as_number()), args[1].to_string(), args[2].as_number());
    });
    runtime.register_function("GUI.SetClip", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 5) throw std::runtime_error("GUI.SetClip expects window, x, y, width, and height");
        gui::set_clip(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(), args[4].as_number()); return {};
    });
    runtime.register_function("GUI.ResetClip", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.ResetClip expects a window");
        gui::reset_clip(static_cast<int>(args[0].as_number())); return {};
    });
    runtime.register_function("GUI.ClipboardText", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.ClipboardText expects a window");
        return gui::clipboard_text(static_cast<int>(args[0].as_number()));
    });
    runtime.register_function("GUI.SetClipboardText", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("GUI.SetClipboardText expects a window and text");
        gui::set_clipboard_text(static_cast<int>(args[0].as_number()), args[1].to_string()); return {};
    });
    runtime.register_function("GUI.SetCursor", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("GUI.SetCursor expects a window and cursor name");
        gui::set_cursor(static_cast<int>(args[0].as_number()), lowercase(args[1].to_string())); return {};
    });
    runtime.register_function("GUI.OpenFileDialog", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 1 || args.size() > 3) throw std::runtime_error("GUI.OpenFileDialog expects window, optional title, and optional initial path");
        return gui::open_file_dialog(static_cast<int>(args[0].as_number()), args.size() >= 2 ? args[1].to_string() : "Open File", args.size() == 3 ? args[2].to_string() : "");
    });
    runtime.register_function("GUI.SaveFileDialog", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 1 || args.size() > 3) throw std::runtime_error("GUI.SaveFileDialog expects window, optional title, and optional initial path");
        return gui::save_file_dialog(static_cast<int>(args[0].as_number()), args.size() >= 2 ? args[1].to_string() : "Save File", args.size() == 3 ? args[2].to_string() : "");
    });
    runtime.register_function("GUI.Confirm", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 3) throw std::runtime_error("GUI.Confirm expects window, title, and message");
        return gui::confirm(static_cast<int>(args[0].as_number()), args[1].to_string(), args[2].to_string());
    });
    runtime.register_function("GUI.Present", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.Present expects a window");
        gui::present(static_cast<int>(args[0].as_number())); return {};
    });
    runtime.register_function("GUI.PollEvent", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("GUI.PollEvent expects no arguments");
        return gui::poll_event();
    });
    runtime.register_function("GUI.WaitEvent", [&runtime](const std::vector<Value>& args) -> Value {
        if (args.size() > 1) throw std::runtime_error("GUI.WaitEvent expects optional timeout seconds");
        Value event = gui::wait_event(args.empty() ? 0.05 : args[0].as_number());
        runtime.reset_instruction_count();
        return event;
    });

    runtime.register_function("System.Open", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("System.Open expects 1 argument");
        }
        return open_target(args[0].to_string());
    });

    runtime.register_function("System.Launch", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("System.Launch expects 1 argument");
        }
        const auto words = split_shell_words(args[0].to_string());
        if (words.empty()) {
            throw std::runtime_error("System.Launch expects a command");
        }
        if (!find_external_command(words.front())) {
            throw std::runtime_error("System.Launch command not found: " + words.front());
        }
#ifdef _WIN32
        const std::string command = "cmd /c start \"\" " + args[0].to_string();
#else
        const std::string command = args[0].to_string() + " >/dev/null 2>&1 &";
#endif
        const int status = std::system(command.c_str());
        return status_from_wait_status(status) == 0;
    });

    runtime.register_function("Host.Printers", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("Host.Printers expects no arguments");
        }
        return printer_list();
    });

    runtime.register_function("Printer.List", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("Printer.List expects no arguments");
        }
        return printer_list();
    });

    runtime.register_function("Printer.Default", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("Printer.Default expects no arguments");
        }
        return default_printer();
    });

    runtime.register_function("Printer.PrintFile", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || args.size() > 2) {
            throw std::runtime_error("Printer.PrintFile expects path and optional printer name");
        }
        return print_file(args[0].to_string(), args.size() == 2 ? args[1].to_string() : "");
    });

    runtime.register_function("File.Exists", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("File.Exists expects 1 argument");
        }
        return std::filesystem::exists(args[0].to_string());
    });

    runtime.register_function("File.List", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("File.List expects a directory path");
        const std::filesystem::path directory(args[0].to_string());
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) throw std::runtime_error("not a directory: " + directory.string());
        std::vector<std::filesystem::directory_entry> entries;
        for (std::filesystem::directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied, error), end;
             !error && iterator != end; iterator.increment(error)) entries.push_back(*iterator);
        if (error) throw std::runtime_error("could not list directory: " + directory.string() + ": " + error.message());
        std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
            std::error_code left_error, right_error;
            const bool left_directory = left.is_directory(left_error);
            const bool right_directory = right.is_directory(right_error);
            if (left_directory != right_directory) return left_directory;
            return lowercase(left.path().filename().string()) < lowercase(right.path().filename().string());
        });
        Value::Array result;
        for (const auto& entry : entries) {
            std::error_code entry_error;
            const bool directory_entry = entry.is_directory(entry_error);
            const auto name = entry.path().filename().string();
            double size = 0;
            if (!directory_entry) {
                const auto bytes = entry.file_size(entry_error);
                if (!entry_error) size = static_cast<double>(bytes);
            }
            result.emplace_back(Value::Object{{"Name", name}, {"Path", entry.path().string()}, {"IsDirectory", directory_entry},
                                               {"IsFile", entry.is_regular_file(entry_error)}, {"IsHidden", !name.empty() && name.front() == '.'},
                                               {"Size", size}, {"Extension", entry.path().extension().string()}});
        }
        return result;
    });

    runtime.register_function("File.ReadText", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("File.ReadText expects 1 argument");
        }
        return read_file(args[0].to_string());
    });

    runtime.register_function("File.WriteText", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("File.WriteText expects path and text");
        }
        std::ofstream output(args[0].to_string(), std::ios::trunc);
        if (!output) {
            throw std::runtime_error("could not write " + args[0].to_string());
        }
        output << args[1].to_string();
        return true;
    });

    runtime.register_function("File.AppendText", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("File.AppendText expects path and text");
        }
        std::ofstream output(args[0].to_string(), std::ios::app);
        if (!output) {
            throw std::runtime_error("could not append " + args[0].to_string());
        }
        output << args[1].to_string();
        return true;
    });

    runtime.register_function("File.Find", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("File.Find expects 1 argument");
        }
        return find_files(args[0].to_string());
    });

    runtime.register_function("Directory.Create", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Directory.Create expects 1 argument");
        }
        return std::filesystem::create_directories(args[0].to_string()) || std::filesystem::exists(args[0].to_string());
    });

    runtime.register_function("Directory.Exists", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Directory.Exists expects 1 argument");
        }
        return std::filesystem::is_directory(args[0].to_string());
    });

    runtime.register_function("Path.Join", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) {
            throw std::runtime_error("Path.Join expects at least 1 argument");
        }
        std::filesystem::path path(args[0].to_string());
        for (std::size_t i = 1; i < args.size(); ++i) {
            path /= args[i].to_string();
        }
        return path.string();
    });
    runtime.register_function("Path.Home", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("Path.Home expects no arguments");
        return home_directory();
    });
    runtime.register_function("ArcoSH.AssetsDir", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("ArcoSH.AssetsDir expects no arguments");
        return runtime_assets_directory().string();
    });

    runtime.register_function("Path.BaseName", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Path.BaseName expects 1 argument");
        }
        return std::filesystem::path(args[0].to_string()).filename().string();
    });

    runtime.register_function("Path.DirName", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Path.DirName expects 1 argument");
        }
        return std::filesystem::path(args[0].to_string()).parent_path().string();
    });

    runtime.register_function("Path.Extension", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Path.Extension expects 1 argument");
        }
        return std::filesystem::path(args[0].to_string()).extension().string();
    });

    runtime.register_function("ArcoSH.Home", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("ArcoSH.Home expects no arguments");
        }
        return arcosh_home_directory().string();
    });

    runtime.register_function("ArcoSH.ExecutablePath", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("ArcoSH.ExecutablePath expects no arguments");
        }
        if (const auto path = executable_path()) {
            return path->string();
        }
        return "";
    });

    runtime.register_function("ArcoSH.PluginsDir", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("ArcoSH.PluginsDir expects no arguments");
        }
        return arcosh_plugins_directory().string();
    });

    runtime.register_function("ArcoSH.ModsDir", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("ArcoSH.ModsDir expects no arguments");
        }
        return arcosh_mods_directory().string();
    });

    runtime.register_function("ArcoSH.ScriptsDir", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("ArcoSH.ScriptsDir expects no arguments");
        }
        return arcosh_scripts_directory().string();
    });

    runtime.register_function("ArcoSH.SetPrompt", [&runtime](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("ArcoSH.SetPrompt expects 1 argument");
        }
        runtime.set_global("ArcoSH.Prompt", args[0].to_string());
        return args[0].to_string();
    });

    runtime.register_function("ArcoSH.GetPrompt", [&runtime](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("ArcoSH.GetPrompt expects no arguments");
        }
        return prompt_pattern(runtime);
    });

    runtime.register_function("ArcoSH.Alias", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("ArcoSH.Alias expects name and command");
        }
        g_aliases[args[0].to_string()] = args[1].to_string();
        return true;
    });

    runtime.register_function("ArcoSH.Unalias", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("ArcoSH.Unalias expects 1 argument");
        }
        g_aliases.erase(args[0].to_string());
        return true;
    });

    runtime.register_function("ArcoSH.Aliases", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("ArcoSH.Aliases expects no arguments");
        }
        Value::Object aliases;
        for (const auto& [name, command] : g_aliases) {
            aliases[name] = command;
        }
        return aliases;
    });

    runtime.register_function("ArcoSH.Source", [&runtime](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("ArcoSH.Source expects 1 argument");
        }
        const auto result = runtime.run_string(read_file(args[0].to_string()));
        if (!result.ok) {
            throw std::runtime_error(result.error);
        }
        if (result.exited) {
            throw ExitSignal(result.exit_code);
        }
        return true;
    });

    auto mod_list_function = [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("Mod.List expects no arguments");
        }
        return list_mods();
    };
    auto mod_install_function = [](const std::vector<Value>& args) -> Value {
        if (args.empty() || args.size() > 2) {
            throw std::runtime_error("Mod.Install expects path and optional name");
        }
        return install_mod(args[0].to_string(), args.size() == 2 ? args[1].to_string() : "");
    };
    auto mod_activate_function = [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Mod.Activate expects mod name");
        }
        return activate_mod(args[0].to_string());
    };
    auto mod_deactivate_function = [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Mod.Deactivate expects mod name");
        }
        return deactivate_mod(args[0].to_string());
    };
    auto mod_load_function = [&runtime](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Mod.Load expects mod name");
        }
        const auto file = find_installed_mod(args[0].to_string());
        if (!file) {
            throw std::runtime_error("mod is not installed: " + args[0].to_string());
        }
        const auto result = runtime.run_string(read_file(file->string()));
        if (!result.ok) {
            throw std::runtime_error(result.error);
        }
        if (result.exited) {
            throw ExitSignal(result.exit_code);
        }
        return mod_info_value(*file);
    };
    runtime.register_function("ArcoSH.ModList", mod_list_function);
    runtime.register_function("ArcoSH.ModInstall", mod_install_function);
    runtime.register_function("ArcoSH.ModActivate", mod_activate_function);
    runtime.register_function("ArcoSH.ModDeactivate", mod_deactivate_function);
    runtime.register_function("ArcoSH.ModLoad", mod_load_function);
    runtime.register_function("Mod.List", mod_list_function);
    runtime.register_function("Mod.Install", mod_install_function);
    runtime.register_function("Mod.Activate", mod_activate_function);
    runtime.register_function("Mod.Deactivate", mod_deactivate_function);
    runtime.register_function("Mod.Load", mod_load_function);

    runtime.register_function("ArcoSH.StartJob", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("ArcoSH.StartJob expects command");
        }
        std::ostringstream ignored;
        const int status = start_background_job(args[0].to_string() + " &", ignored);
        if (status != 0 || g_jobs.empty()) {
            throw std::runtime_error("could not start background job");
        }
        return job_to_value(g_jobs.back());
    });

    runtime.register_function("ArcoSH.Jobs", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("ArcoSH.Jobs expects no arguments");
        }
        return jobs_value();
    });

    runtime.register_function("ArcoSH.KillJob", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || args.size() > 2) {
            throw std::runtime_error("ArcoSH.KillJob expects job id and optional signal");
        }
        std::ostringstream ignored;
        const int signal = args.size() == 2 ? static_cast<int>(args[1].as_number()) : SIGTERM;
        return kill_job(static_cast<int>(args[0].as_number()), signal, ignored) == 0;
    });

    runtime.register_function("ArcoSH.WaitJob", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("ArcoSH.WaitJob expects job id");
        }
        std::ostringstream ignored;
        return static_cast<double>(foreground_job(static_cast<int>(args[0].as_number()), ignored));
    });

    runtime.register_function("ArcoSH.DisownJob", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("ArcoSH.DisownJob expects job id");
        }
        std::ostringstream ignored;
        return disown_job(static_cast<int>(args[0].as_number()), ignored) == 0;
    });

    runtime.register_function("Host.Processes", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("Host.Processes expects no arguments");
        }
        return process_list();
    });

    runtime.register_function("Process.List", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("Process.List expects no arguments");
        }
        return process_list();
    });

    runtime.register_function("Process.Exists", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Process.Exists expects 1 argument");
        }
        const std::string name = args[0].to_string();
        for (const auto& process : process_list().as_array()) {
            if (process.is_object() && process.get_property("Name").to_string() == name) {
                return true;
            }
        }
        return false;
    });

    runtime.register_function("Process.Kill", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 1 || args.size() > 2) {
            throw std::runtime_error("Process.Kill expects pid and optional signal");
        }
        const int pid = static_cast<int>(args[0].as_number());
        const int signal = args.size() == 2 ? static_cast<int>(args[1].as_number()) : 15;
#ifdef _WIN32
        (void)pid;
        (void)signal;
        throw std::runtime_error("Process.Kill is not implemented on Windows");
#else
        return kill(pid, signal) == 0;
#endif
    });

    runtime.register_function("Help.Topic", [](const std::vector<Value>& args) -> Value {
        if (args.size() > 1) {
            throw std::runtime_error("Help.Topic expects 0 or 1 arguments");
        }
        return help_text(args.empty() ? "" : args[0].to_string());
    });

    runtime.register_function("Help.Topics", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("Help.Topics expects no arguments");
        }
        return help_topics();
    });

    runtime.register_function("TUI.Rule", [](const std::vector<Value>& args) -> Value {
        if (args.size() > 2) {
            throw std::runtime_error("TUI.Rule expects title and optional width");
        }
        const std::string title = args.empty() ? "" : args[0].to_string();
        const std::size_t width = args.size() == 2 ? static_cast<std::size_t>(std::max(8.0, args[1].as_number())) : 72;
        return tui_rule(title, width);
    });

    runtime.register_function("TUI.ThemeRule", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 3) {
            throw std::runtime_error("TUI.ThemeRule expects theme, title, and optional width");
        }
        const std::size_t width = args.size() == 3 ? static_cast<std::size_t>(std::max(8.0, args[2].as_number())) : 72;
        return tui_rule_with_theme(args[1].to_string(), width, tui_theme(args[0].to_string()));
    });

    runtime.register_function("TUI.Box", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("TUI.Box expects title and body");
        }
        return tui_box(args[0].to_string(), args[1].to_string());
    });

    runtime.register_function("TUI.ThemeBox", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 3) {
            throw std::runtime_error("TUI.ThemeBox expects theme, title, and body");
        }
        return tui_box_with_theme(args[1].to_string(), args[2].to_string(), tui_theme(args[0].to_string()));
    });

    runtime.register_function("TUI.Scroll", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("TUI.Scroll expects title and body");
        }
        return tui_scroll(args[0].to_string(), args[1].to_string());
    });

    runtime.register_function("TUI.Header", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("TUI.Header expects title");
        }
        return tui_box(args[0].to_string(), "ArcoSH terminal workspace\nType HELP topics or HELP search <text>.");
    });

    runtime.register_function("TUI.ThemeNames", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("TUI.ThemeNames expects no arguments");
        }
        Value::Array names;
        for (const auto& [name, theme] : tui_themes()) {
            (void)theme;
            names.emplace_back(name);
        }
        return names;
    });

    runtime.register_function("TUI.Badge", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("TUI.Badge expects state");
        }
        return tui_badge(args[0].to_string());
    });

    runtime.register_function("TUI.Status", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 3) {
            throw std::runtime_error("TUI.Status expects label, state, and optional detail");
        }
        return tui_status(args[0].to_string(), args[1].to_string(), args.size() == 3 ? args[2].to_string() : "");
    });

    runtime.register_function("TUI.Progress", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3 || args.size() > 4) {
            throw std::runtime_error("TUI.Progress expects label, current, total, and optional width");
        }
        const std::size_t width = args.size() == 4 ? static_cast<std::size_t>(std::max(8.0, args[3].as_number())) : 24;
        return tui_progress(args[0].to_string(), args[1].as_number(), args[2].as_number(), width);
    });

    runtime.register_function("TUI.List", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("TUI.List expects title and items array");
        }
        return tui_list(args[0].to_string(), args[1].as_array(), false);
    });

    runtime.register_function("TUI.Menu", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("TUI.Menu expects title and items array");
        }
        return tui_list(args[0].to_string(), args[1].as_array(), true);
    });

    runtime.register_function("TUI.KeyValues", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("TUI.KeyValues expects title and object");
        }
        return tui_key_values(args[0].to_string(), args[1].as_object());
    });

    runtime.register_function("TUI.Table", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("TUI.Table expects headers array and rows array");
        }
        return tui_table(args[0].as_array(), args[1].as_array());
    });

    runtime.register_function("TUI.Clear", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("TUI.Clear expects no arguments");
        }
        return "\033[2J\033[H";
    });

    runtime.register_function("TUI.Cursor", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("TUI.Cursor expects row and column");
        }
        return "\033[" + std::to_string(static_cast<int>(args[0].as_number())) + ";" +
               std::to_string(static_cast<int>(args[1].as_number())) + "H";
    });

    runtime.register_function("TUI.MouseEnable", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("TUI.MouseEnable expects no arguments");
        }
        // Basic button events, drag/motion events, and unambiguous SGR coordinates.
        return "\033[?1000h\033[?1002h\033[?1006h";
    });

    runtime.register_function("TUI.MouseDisable", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("TUI.MouseDisable expects no arguments");
        }
        return "\033[?1006l\033[?1002l\033[?1000l";
    });

    runtime.register_function("TUI.ParseEvent", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("TUI.ParseEvent expects one terminal sequence");
        }
        return tui_parse_event(args[0].to_string());
    });

    runtime.register_function("TUI.ReadEvent", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("TUI.ReadEvent expects no arguments");
        }
        return read_tui_event();
    });

    runtime.register_function("TUI.HitTest", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 5 || !args[0].is_object()) {
            throw std::runtime_error("TUI.HitTest expects event, x, y, width, and height");
        }
        const auto& event = args[0].as_object();
        const auto type = event.find("Type");
        const auto event_x = event.find("X");
        const auto event_y = event.find("Y");
        if (type == event.end() || event_x == event.end() || event_y == event.end() || type->second.to_string() != "mouse") {
            return false;
        }
        const double x = args[1].as_number();
        const double y = args[2].as_number();
        const double width = args[3].as_number();
        const double height = args[4].as_number();
        const double px = event_x->second.as_number();
        const double py = event_y->second.as_number();
        return width > 0 && height > 0 && px >= x && px < x + width && py >= y && py < y + height;
    });

    runtime.register_function("Color.Enabled", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("Color.Enabled expects no arguments");
        }
        return color_enabled();
    });

    runtime.register_function("Color.Names", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            throw std::runtime_error("Color.Names expects no arguments");
        }
        return color_names();
    });

    runtime.register_function("Color.Paint", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("Color.Paint expects 2 arguments");
        }
        return style(args[0].to_string(), args[1].to_string());
    });

    runtime.register_function("Color.Red", [](const std::vector<Value>& args) -> Value { return color_function(args, "red"); });
    runtime.register_function("Color.Green", [](const std::vector<Value>& args) -> Value { return color_function(args, "green"); });
    runtime.register_function("Color.Yellow", [](const std::vector<Value>& args) -> Value { return color_function(args, "yellow"); });
    runtime.register_function("Color.Blue", [](const std::vector<Value>& args) -> Value { return color_function(args, "blue"); });
    runtime.register_function("Color.Magenta", [](const std::vector<Value>& args) -> Value { return color_function(args, "magenta"); });
    runtime.register_function("Color.Cyan", [](const std::vector<Value>& args) -> Value { return color_function(args, "cyan"); });
    runtime.register_function("Color.White", [](const std::vector<Value>& args) -> Value { return color_function(args, "white"); });
    runtime.register_function("Color.Bold", [](const std::vector<Value>& args) -> Value { return color_function(args, "bold"); });
}

void set_script_context(Runtime& runtime, const std::string& path, const std::vector<std::string>& args) {
    Value::Array values;
    for (const auto& arg : args) {
        values.emplace_back(arg);
    }
    runtime.set_global("Args", values);
    runtime.set_global("Script.Path", path);
    runtime.set_global("Script.Name", std::filesystem::path(path).filename().string());
    runtime.set_global("Script.ArgCount", static_cast<double>(args.size()));
}

RunResult run_file(Runtime& runtime, const std::string& path) {
    return run_file(runtime, path, {});
}

RunResult run_file(Runtime& runtime, const std::string& path, const std::vector<std::string>& args) {
    try {
        set_script_context(runtime, path, args);
        return runtime.run_string(read_file(path));
    } catch (const std::exception& error) {
        return {false, error.what()};
    }
}

RunResult init_profile(std::ostream& output) {
    try {
        ensure_arcosh_home();
        const auto home = arcosh_home_directory();
        const auto rc = home / "rc.abas";
        if (!std::filesystem::exists(rc)) {
            std::ofstream rc_file(rc);
            rc_file
                << "' ArcoSH startup profile\n"
                << "' Loaded whenever ArcoSH starts. Use --safe, --norc, or --no-rc to skip this file.\n"
                << "' Run arcosh --doctor when startup behaves unexpectedly.\n"
                << "ArcoSH.SetPrompt(\"{user}@{host}:{cwd:short} [{status}]> \")\n"
                << "ArcoSH.Alias(\"ll\", \"ls -la\")\n"
                << "ArcoSH.Alias(\"la\", \"ls -A\")\n";
        }
        const auto example = arcosh_scripts_directory() / "hello.abas";
        if (!std::filesystem::exists(example)) {
            std::ofstream script_file(example);
            script_file
                << "PRINT \"Hello from \" + Script.Name\n"
                << "FOR arg IN Args\n"
                << "    PRINT arg\n"
                << "NEXT\n";
        }
        const auto sysinfo = arcosh_scripts_directory() / "sysinfo.abas";
        if (!std::filesystem::exists(sysinfo)) {
            std::ofstream script_file(sysinfo);
            script_file
                << "#IMPORT \"sysadmin\"\n"
                << "PRINT SysAdmin.HostSummary()\n"
                << "PRINT \"Processes visible: \" + STRING(LEN(Process.List()))\n"
                << "IF SysAdmin.CommandExists(\"uname\") THEN\n"
                << "    PRINT Shell.Output(\"uname -a\")\n"
                << "END IF\n";
        }
        output << "Initialized " << home.string() << '\n';
        output << "  rc: " << rc.string() << '\n';
        output << "  plugins: " << arcosh_plugins_directory().string() << '\n';
        output << "  mods: " << arcosh_mods_directory().string() << '\n';
        output << "  scripts: " << arcosh_scripts_directory().string() << '\n';
        return {};
    } catch (const std::exception& error) {
        return {false, error.what()};
    }
}

int doctor(std::ostream& output) {
    int failures = 0;
    auto ok = [&output](const std::string& message) {
        output << "[OK] " << message << '\n';
    };
    auto warn = [&output](const std::string& message) {
        output << "[WARN] " << message << '\n';
    };
    auto fail = [&output, &failures](const std::string& message) {
        output << "[FAIL] " << message << '\n';
        failures++;
    };

    output << "ArcoSH doctor\n";

    const auto home = arcosh_home_directory();
    if (std::filesystem::exists(home)) {
        ok("profile directory: " + home.string());
    } else {
        warn("profile directory missing: " + home.string() + " (run arcosh --init-profile)");
    }

    if (std::filesystem::exists(arcosh_plugins_directory())) {
        ok("plugins directory: " + arcosh_plugins_directory().string());
    } else {
        warn("plugins directory missing: " + arcosh_plugins_directory().string());
    }

    if (std::filesystem::exists(arcosh_mods_directory())) {
        ok("mods directory: " + arcosh_mods_directory().string());
    } else {
        warn("mods directory missing: " + arcosh_mods_directory().string());
    }

    if (std::filesystem::exists(arcosh_scripts_directory())) {
        ok("scripts directory: " + arcosh_scripts_directory().string());
    } else {
        warn("scripts directory missing: " + arcosh_scripts_directory().string());
    }

    bool found_stdlib = false;
    for (const auto& dir : stdlib_search_directories()) {
        if (std::filesystem::exists(dir / "text.abas") && std::filesystem::exists(dir / "sysadmin.abas")) {
            ok("stdlib directory: " + dir.string());
            found_stdlib = true;
            break;
        }
    }
    if (!found_stdlib) {
        fail("stdlib modules not found; set ARCOBASIC_STDLIB or install share/arcobasic/stdlib");
    }

    Runtime runtime;
    register_shell_builtins(runtime);
    std::ostringstream sink;
    runtime.set_output(sink);
    const auto import_result = runtime.run_string("#IMPORT \"text\"\nPRINT Text.IsBlank(\" \")\n");
    if (import_result.ok) {
        ok("stdlib import: #IMPORT \"text\"");
    } else {
        fail("stdlib import failed: " + import_result.error);
    }

    for (const auto& command : {"sh", "printf", "grep"}) {
        if (find_external_command(command)) {
            ok(std::string("host command: ") + command);
        } else {
            warn(std::string("host command not found: ") + command);
        }
    }

#ifndef _WIN32
    ok("POSIX process primitives available");
    if (isatty(STDIN_FILENO)) {
        ok("stdin is a terminal");
    } else {
        warn("stdin is not a terminal; line editing and foreground job behavior may be limited");
    }
#else
    warn("Windows process/job control is limited in this alpha");
#endif

    output << (failures == 0 ? "Doctor completed without failures.\n" : "Doctor found failures.\n");
    return failures == 0 ? 0 : 1;
}

RunResult load_startup(Runtime& runtime, std::ostream& output, bool login) {
    try {
        ensure_arcosh_home();
    } catch (const std::exception& error) {
        output << style(std::string("arcosh startup: ") + error.what(), "red") << '\n';
        return {};
    }

    std::vector<std::filesystem::path> files;
    const auto home = arcosh_home_directory();
    for (const auto& filename : {"rc.abas", "rc.arcsh", "rc.arc", "rc.bas"}) {
        const auto file = home / filename;
        if (std::filesystem::exists(file)) {
            files.push_back(file);
        }
    }
    for (const auto& plugin : sorted_script_files(arcosh_plugins_directory())) {
        files.push_back(plugin);
    }
    for (const auto& mod : enabled_mod_files()) {
        files.push_back(mod);
    }
    if (login) {
        for (const auto& filename : {"login.abas", "login.arcsh", "login.arc", "login.bas"}) {
            const auto file = home / filename;
            if (std::filesystem::exists(file)) {
                files.push_back(file);
            }
        }
    }

    for (const auto& file : files) {
        const auto result = run_file(runtime, file.string());
        if (result.exited) {
            return result;
        }
        if (!result.ok) {
            output << style("arcosh startup " + file.string() + ": " + result.error, "red") << '\n';
        }
    }
    return {};
}

int run_command_once(Runtime& runtime, const std::string& command, std::ostream& output) {
    ignore_sigint();
    try {
        if (has_unquoted_trailing_background_marker(command)) {
            return start_background_job(command, output);
        }
        const Value result = runtime.call_host_function("RUN", {command});
        const std::string text = result.to_string();
        if (!text.empty()) {
            output << text << '\n';
        }
        if (result.is_object()) {
            return static_cast<int>(result.get_property("ExitCode").as_number());
        }
        return 0;
    } catch (const std::exception& error) {
        output << style(error.what(), "red") << '\n';
        return 1;
    }
}

RunResult run_tutorial(Runtime& runtime, std::istream& input, std::ostream& output, const std::string& topic) {
    runtime.set_output(output);

    auto run_practice = [&output](const std::string& command, const std::string& mode) -> Value {
        const std::string selected_mode = lowercase(trim(mode));
        output << style("$ " + command, "brightblack") << '\n';
        if (selected_mode == "shell") {
            const Value result = run_command(command);
            const std::string text = result.to_string();
            if (!text.empty()) {
                output << text << '\n';
            }
            return result;
        }
        if (selected_mode != "arco") {
            throw std::runtime_error("Tutorial.Run mode must be arco or shell");
        }

        Runtime practice;
        register_shell_builtins(practice);
        std::ostringstream captured;
        practice.set_output(captured);
        const auto result = practice.run_string(command + "\n");
        const std::string text = captured.str();
        if (!text.empty()) {
            output << text;
        }
        if (!result.ok) {
            output << style(result.error, "red") << '\n';
            return false;
        }
        return true;
    };

    runtime.register_function("Tutorial.ReadLine", [&input, &output](const std::vector<Value>& args) -> Value {
        if (args.size() > 1) {
            throw std::runtime_error("Tutorial.ReadLine expects 0 or 1 arguments");
        }
        if (!args.empty()) {
            output << args[0].to_string() << std::flush;
        }
        std::string answer;
        if (!std::getline(input, answer)) {
            output << "\nTutorial stopped.\n";
            throw StopSignal();
        }
        return answer;
    });

    runtime.register_function("Tutorial.Run", [run_practice](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("Tutorial.Run expects command and mode");
        }
        return run_practice(args[0].to_string(), args[1].to_string());
    });

    runtime.register_function("Tutorial.Step", [&input, &output, run_practice](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("Tutorial.Step expects command and mode");
        }
        const std::string expected = args[0].to_string();
        const std::string mode = args[1].to_string();
        output << style("Type:", "bold") << '\n';
        output << "  " << expected << '\n';
        output << style("Commands:", "brightblack") << " hint, skip, quit\n";
        output << style("tutorial", "cyan") << style(">", "brightblack") << " " << std::flush;

        std::string answer;
        while (true) {
            if (!std::getline(input, answer)) {
                output << "\nTutorial stopped.\n";
                throw StopSignal();
            }
            const std::string action = lowercase(trim(answer));
            if (action == "quit" || action == "exit") {
                output << "Tutorial stopped.\n";
                throw StopSignal();
            }
            if (action == "hint" || action == "help" || action == "?") {
                output << style("Hint:", "cyan") << " type this exactly, then press Enter:\n";
                output << "  " << expected << '\n';
                output << style("tutorial", "cyan") << style(">", "brightblack") << " " << std::flush;
                continue;
            }
            if (action == "skip") {
                output << style("Skipped.", "yellow") << " Running the expected command.\n";
                return run_practice(expected, mode);
            }
            if (same_command_text(answer, expected)) {
                output << style("Correct", "green") << '\n';
                return run_practice(answer, mode);
            }
            output << style("Expected:", "yellow") << " " << expected << '\n';
            output << style("Try again.", "yellow") << " Type hint, skip, or quit if needed.\n";
            output << style("tutorial", "cyan") << style(">", "brightblack") << " " << std::flush;
        }
    });

    try {
        std::string source = tutorial_source();
        const std::string selected = lowercase(trim(topic));
        std::string filename = "arcosh_sysadmin.abas";
        if (selected == "game" || selected == "games") {
            filename = "arcosh_game.abas";
            source.clear();
        } else if (selected == "tool" || selected == "tools" || selected == "utility" || selected == "useful") {
            filename = "arcosh_tool.abas";
            source.clear();
        } else if (selected == "adventure" || selected == "adventures" || selected == "arcoadventure" || selected == "arcoadventures") {
            filename = "arcoadventures_intro.abas";
            source.clear();
        } else if (selected == "adventure1" || selected == "badge" || selected == "badges" || selected == "badge-bureau") {
            filename = "arcoadventure_badge_bureau.abas";
            source.clear();
        } else if (selected == "adventure2" || selected == "snack" || selected == "snacks" || selected == "snackstorm") {
            filename = "arcoadventure_snackstorm.abas";
            source.clear();
        } else if (selected == "adventure3" || selected == "evidence" || selected == "locker" || selected == "evidence-locker") {
            filename = "arcoadventure_evidence_locker.abas";
            source.clear();
        } else if (!selected.empty() && selected != "sysadmin" && selected != "admin" && selected != "shell") {
            output << style("Unknown tutorial: " + topic, "yellow") << '\n';
            output << "Available tutorials: sysadmin, game, tool, adventure, adventure1, adventure2, adventure3\n";
            return {true, ""};
        }
        std::vector<std::filesystem::path> tutorial_paths = {
            std::filesystem::path("tutorials") / filename,
            std::filesystem::path("../tutorials") / filename,
            std::filesystem::path("share/arcosh/tutorials") / filename,
            std::filesystem::path("/usr/local/share/arcosh/tutorials") / filename,
            std::filesystem::path("/usr/share/arcosh/tutorials") / filename
        };
        if (const auto exe_dir = executable_directory()) {
            tutorial_paths.push_back(*exe_dir / "../share/arcosh/tutorials" / filename);
        }
        for (const auto& path : tutorial_paths) {
            if (std::filesystem::exists(path)) {
                source = read_file(path.string());
                break;
            }
        }
        if (source.empty()) {
            output << style("Tutorial file not found: " + filename, "red") << '\n';
            output << "Install ArcoSH tutorials or run from the repository checkout.\n";
            return {true, ""};
        }
        return runtime.run_string(source);
    } catch (const std::exception& error) {
        return {false, error.what()};
    }
}

RunResult run_login_shell_wizard(Runtime& runtime) {
    std::vector<std::filesystem::path> wizard_paths = {
        std::filesystem::path("scripts/arcosh/install-login-shell.abas"),
        std::filesystem::path("../scripts/arcosh/install-login-shell.abas"),
        std::filesystem::path("share/arcosh/scripts/install-login-shell.abas"),
        std::filesystem::path("/usr/local/share/arcosh/scripts/install-login-shell.abas"),
        std::filesystem::path("/usr/share/arcosh/scripts/install-login-shell.abas")
    };
    if (const auto exe_dir = executable_directory()) {
        wizard_paths.push_back(*exe_dir / "../share/arcosh/scripts/install-login-shell.abas");
    }

    for (const auto& path : wizard_paths) {
        if (std::filesystem::exists(path)) {
            return run_file(runtime, path.string());
        }
    }

    return {false, "ArcoSH login shell wizard not found. Expected install-login-shell.abas in share/arcosh/scripts."};
}

int repl(Runtime& runtime, std::istream& input, std::ostream& output, bool interactive) {
    ignore_sigint();
    std::string line;
    std::string last_unknown_command;
    std::map<int, std::string> numbered_program;
    std::string loaded_program_source;
    std::string loaded_program_path;
    std::string multiline_source;
    int multiline_depth = 0;
    int last_status = 0;
    ShellHistory history;
    try {
        ensure_arcosh_home();
        history.load();
    } catch (const std::exception&) {
    }
    struct HistorySaver {
        ShellHistory& history;
        ~HistorySaver() {
            history.save();
        }
    } history_saver{history};
    LineEditor editor(input, output, history, interactive);

    while (true) {
        const std::string prompt = render_prompt(runtime, multiline_depth, last_status);
        const auto maybe_line = editor.read_line(prompt);
        if (!maybe_line) {
            break;
        }
        reap_background_jobs();
        line = *maybe_line;
        if (multiline_depth == 0) {
            history.add(line);
        }
        if (multiline_depth > 0) {
            multiline_source += line + "\n";
            multiline_depth += multiline_delta(line);
            if (multiline_depth <= 0) {
                const auto result = runtime.run_string(multiline_source);
                if (!result.ok) {
                    output << style(result.error, "red") << '\n';
                    last_status = 1;
                } else {
                    last_status = result.exited ? result.exit_code : 0;
                }
                if (result.exited) {
                    return result.exit_code;
                }
                multiline_source.clear();
                multiline_depth = 0;
            }
            continue;
        }
        if (line == "EXIT" || line == "exit") {
            break;
        }
        if (line == "HELP" || line == "help") {
            output << help_text("");
            continue;
        }
        if (line.rfind("HELP ", 0) == 0 || line.rfind("help ", 0) == 0) {
            output << help_text(line.substr(5));
            continue;
        }
        if (line == "VERSION" || line == "version") {
            output << style("ArcoSH", "green") << " alpha 0.1\n";
            continue;
        }
        if (line == "HISTORY" || line == "history") {
            const auto& entries = history.entries();
            for (std::size_t i = 0; i < entries.size(); ++i) {
                output << (i + 1) << "  " << entries[i] << '\n';
            }
            continue;
        }
        if (line == "HISTORY CLEAR" || line == "history clear") {
            history.clear();
            output << "History cleared\n";
            continue;
        }
        if (line == "JOBS" || line == "jobs") {
            print_jobs(output);
            last_status = 0;
            continue;
        }
        if (line == "JOBS -C" || line == "jobs -c") {
            prune_done_jobs();
            last_status = 0;
            continue;
        }
        if (starts_with_word(lowercase(line), "fg")) {
            const std::string spec = shell_args_after_first_word(line);
            const auto id = parse_job_id(spec, output, "usage: fg [job-id]");
            if (!id) {
                last_status = 1;
                continue;
            }
            last_status = foreground_job(*id, output);
            continue;
        }
        if (starts_with_word(lowercase(line), "bg")) {
            const std::string spec = shell_args_after_first_word(line);
            const auto id = parse_job_id(spec, output, "usage: bg [job-id]");
            if (!id) {
                last_status = 1;
                continue;
            }
            last_status = background_job(*id, output);
            continue;
        }
        if (starts_with_word(lowercase(line), "kill")) {
            const auto words = split_shell_words(shell_args_after_first_word(line));
            int signal = SIGTERM;
            std::string spec;
            if (!words.empty() && !words[0].empty() && words[0][0] == '-') {
                try {
                    signal = std::stoi(words[0].substr(1));
                } catch (const std::exception&) {
                    output << style("usage: kill [-signal] [job-id]", "yellow") << '\n';
                    last_status = 1;
                    continue;
                }
                if (words.size() > 1) {
                    spec = words[1];
                }
            } else if (!words.empty()) {
                spec = words[0];
            }
            const auto id = parse_job_id(spec, output, "usage: kill [-signal] [job-id]");
            if (!id) {
                last_status = 1;
                continue;
            }
            last_status = kill_job(*id, signal, output);
            continue;
        }
        if (starts_with_word(lowercase(line), "disown")) {
            const auto id = parse_job_id(shell_args_after_first_word(line), output, "usage: disown [job-id]");
            if (!id) {
                last_status = 1;
                continue;
            }
            last_status = disown_job(*id, output);
            continue;
        }
        if (line == "COMPLETE" || line == "complete" || starts_with_word(lowercase(line), "complete")) {
            const std::string query = shell_args_after_first_word(line);
            for (const auto& candidate : completion_candidates(query, query.size())) {
                output << candidate << '\n';
            }
            last_status = 0;
            continue;
        }
        if (line == "ALIAS" || line == "alias" || starts_with_word(lowercase(line), "alias")) {
            const std::string spec = shell_args_after_first_word(line);
            if (spec.empty()) {
                for (const auto& [name, command] : g_aliases) {
                    output << name << "='" << command << "'\n";
                }
                last_status = 0;
                continue;
            }
            const auto equals = spec.find('=');
            if (equals == std::string::npos) {
                const auto found = g_aliases.find(spec);
                if (found == g_aliases.end()) {
                    output << style("alias not found: " + spec, "yellow") << '\n';
                    last_status = 1;
                } else {
                    output << found->first << "='" << found->second << "'\n";
                    last_status = 0;
                }
                continue;
            }
            g_aliases[trim(spec.substr(0, equals))] = trim(spec.substr(equals + 1));
            last_status = 0;
            continue;
        }
        if (line == "UNALIAS" || line == "unalias" || starts_with_word(lowercase(line), "unalias")) {
            const std::string name = shell_args_after_first_word(line);
            if (name.empty()) {
                output << style("usage: unalias NAME", "yellow") << '\n';
                last_status = 1;
            } else {
                g_aliases.erase(name);
                last_status = 0;
            }
            continue;
        }
        if (starts_with_word(lowercase(line), "type") || starts_with_word(lowercase(line), "which")) {
            const bool which_only = starts_with_word(lowercase(line), "which");
            const std::string name = shell_args_after_first_word(line);
            if (name.empty()) {
                output << style(which_only ? "usage: which NAME" : "usage: type NAME", "yellow") << '\n';
                last_status = 1;
                continue;
            }
            if (const auto alias = g_aliases.find(name); alias != g_aliases.end()) {
                output << (which_only ? alias->second : name + " is an alias for '" + alias->second + "'") << '\n';
                last_status = 0;
                continue;
            }
            if (const auto profile_script = resolve_profile_script_command(name)) {
                output << (which_only ? profile_script->string() : name + " is a profile script at " + profile_script->string()) << '\n';
                last_status = 0;
                continue;
            }
            bool found_builtin = false;
            for (const auto& builtin : shell_builtin_commands()) {
                if (lowercase(builtin) == lowercase(name)) {
                    output << (which_only ? builtin : name + " is an ArcoSH built-in") << '\n';
                    last_status = 0;
                    found_builtin = true;
                    break;
                }
            }
            if (found_builtin) {
                continue;
            }
            if (const auto external = find_external_command(name)) {
                output << (which_only ? external->string() : name + " is " + external->string()) << '\n';
                last_status = 0;
                continue;
            }
            output << style(name + " not found", "yellow") << '\n';
            last_status = 1;
            continue;
        }
        if (starts_with_word(lowercase(line), "source") || starts_with_word(lowercase(line), ".")) {
            const auto words = split_shell_words(expand_shell_variables(line, last_status));
            if (words.size() < 2) {
                output << style("usage: source FILE [args...]", "yellow") << '\n';
                last_status = 1;
                continue;
            }
            std::vector<std::string> args(words.begin() + 2, words.end());
            const auto result = run_file(runtime, words[1], args);
            if (!result.ok) {
                output << style(result.error, "red") << '\n';
                last_status = 1;
            } else {
                last_status = result.exited ? result.exit_code : 0;
            }
            if (result.exited) {
                return result.exit_code;
            }
            continue;
        }
        auto run_script = [&](const std::filesystem::path& path, const std::vector<std::string>& args) -> std::optional<int> {
            const auto result = run_file(runtime, path.string(), args);
            if (!result.ok) {
                output << style(result.error, "red") << '\n';
                last_status = 1;
            } else {
                last_status = result.exited ? result.exit_code : 0;
            }
            if (result.exited) {
                return result.exit_code;
            }
            return std::nullopt;
        };
        auto run_loaded_program = [&]() -> std::optional<int> {
            set_script_context(runtime, loaded_program_path, {});
            const auto result = runtime.run_string(loaded_program_source);
            if (!result.ok) {
                output << style(result.error, "red") << '\n';
                last_status = 1;
            } else {
                last_status = result.exited ? result.exit_code : 0;
            }
            if (result.exited) {
                return result.exit_code;
            }
            return std::nullopt;
        };
        if (!trim(line).empty() && trim(line)[0] == '@') {
            const std::string launch_line = expand_shell_variables(trim(line).substr(1), last_status);
            const auto words = split_shell_words(launch_line);
            if (words.empty()) {
                output << style("usage: @script.abas [args...]", "yellow") << '\n';
                last_status = 1;
                continue;
            }
            const auto path = resolve_script_launch_target(words[0]);
            std::vector<std::string> args(words.begin() + 1, words.end());
            if (const auto exit_code = run_script(*path, args)) {
                return *exit_code;
            }
            continue;
        }
        if (line == "PWD" || line == "pwd") {
            output << std::filesystem::current_path().string() << '\n';
            continue;
        }
        if (starts_with_word(lowercase(line), "cd")) {
            const std::filesystem::path old_directory = std::filesystem::current_path();
            std::string target = expand_shell_variables(shell_args_after_first_word(line), last_status);
            if (target.empty()) {
                target = home_directory();
            } else if (target == "-") {
                if (g_previous_directory.empty()) {
                    output << style("cd: OLDPWD not set", "yellow") << '\n';
                    last_status = 1;
                    continue;
                }
                target = g_previous_directory.string();
            }
            try {
                std::filesystem::current_path(expand_shell_path(target));
                g_previous_directory = old_directory;
                last_status = 0;
            } catch (const std::exception& error) {
                output << style(std::string("cd: ") + error.what(), "red") << '\n';
                last_status = 1;
            }
            continue;
        }
        if (starts_with_word(lowercase(line), "export")) {
            const std::string assignment = shell_args_after_first_word(line);
            const auto equals = assignment.find('=');
            if (equals == std::string::npos) {
                output << style("usage: export NAME=value", "yellow") << '\n';
                continue;
            }
            if (!set_environment_variable(assignment.substr(0, equals), expand_shell_variables(assignment.substr(equals + 1), last_status))) {
                output << style("export failed", "red") << '\n';
            }
            continue;
        }
        if (starts_with_word(lowercase(line), "unset")) {
            const std::string name = shell_args_after_first_word(line);
            if (name.empty()) {
                output << style("usage: unset NAME", "yellow") << '\n';
                continue;
            }
            if (!unset_environment_variable(name)) {
                output << style("unset failed", "red") << '\n';
            }
            continue;
        }
        if (line == "TUTORIAL" || line == "tutorial" || starts_with_word(lowercase(line), "tutorial")) {
            const auto result = run_tutorial(runtime, input, output, shell_args_after_first_word(line));
            if (!result.ok) {
                output << style(result.error, "red") << '\n';
                last_status = 1;
            } else {
                last_status = result.exited ? result.exit_code : 0;
            }
            if (result.exited) {
                return result.exit_code;
            }
            continue;
        }
        if (line == "INSTALL-LOGIN" || line == "install-login") {
            const auto result = run_login_shell_wizard(runtime);
            if (!result.ok) {
                output << style(result.error, "red") << '\n';
                last_status = 1;
            } else {
                last_status = result.exited ? result.exit_code : 0;
            }
            if (result.exited) {
                return result.exit_code;
            }
            continue;
        }
        if (starts_with_word(lowercase(line), "load")) {
            std::string load_command = line;
            bool run_after_load = false;
            if (const auto separator = line.find(';'); separator != std::string::npos) {
                load_command = trim(line.substr(0, separator));
                const std::string next_command = lowercase(trim(line.substr(separator + 1)));
                if (next_command == "run") {
                    run_after_load = true;
                } else {
                    output << style("usage: LOAD script.abas[; RUN]", "yellow") << '\n';
                    last_status = 1;
                    continue;
                }
            }
            const auto words = split_shell_words(expand_shell_variables(load_command, last_status));
            if (words.size() != 2) {
                output << style("usage: LOAD script.abas[; RUN]", "yellow") << '\n';
                last_status = 1;
                continue;
            }
            try {
                const auto path = resolve_script_launch_target(words[1]);
                loaded_program_source = read_file(path->string());
                loaded_program_path = path->string();
                numbered_program.clear();
                output << "Loaded " << loaded_program_path << '\n';
                last_status = 0;
            } catch (const std::exception& error) {
                output << style(error.what(), "red") << '\n';
                last_status = 1;
            }
            if (run_after_load && last_status == 0) {
                if (const auto exit_code = run_loaded_program()) {
                    return *exit_code;
                }
            }
            continue;
        }
        if (line == "LIST" || line == "list") {
            if (!loaded_program_source.empty()) {
                output << loaded_program_source;
                if (!loaded_program_source.empty() && loaded_program_source.back() != '\n') {
                    output << '\n';
                }
            } else {
                output << stored_program_source(numbered_program);
            }
            continue;
        }
        if (line == "NEW" || line == "new") {
            numbered_program.clear();
            loaded_program_source.clear();
            loaded_program_path.clear();
            continue;
        }
        if (line == "RUN" || line == "run") {
            RunResult result;
            if (!loaded_program_source.empty()) {
                if (const auto exit_code = run_loaded_program()) {
                    return *exit_code;
                }
                continue;
            } else {
                result = runtime.run_string(stored_program_source(numbered_program));
            }
            if (!result.ok) {
                output << style(result.error, "red") << '\n';
                last_status = 1;
            } else {
                last_status = result.exited ? result.exit_code : 0;
            }
            if (result.exited) {
                return result.exit_code;
            }
            continue;
        }
        if (starts_with_word(lowercase(line), "run")) {
            const auto words = split_shell_words(expand_shell_variables(line, last_status));
            if (words.size() >= 2 && looks_like_script_launch_target(words[1])) {
                const auto path = resolve_script_launch_target(words[1]);
                std::vector<std::string> args(words.begin() + 2, words.end());
                if (const auto exit_code = run_script(*path, args)) {
                    return *exit_code;
                }
                continue;
            }
        }
        if (line == "COLOR ON" || line == "color on") {
            set_color_enabled(true);
            output << style("Color enabled", "green") << '\n';
            continue;
        }
        if (line == "COLOR OFF" || line == "color off") {
            set_color_enabled(false);
            output << "Color disabled\n";
            continue;
        }
        if (line == "CLS" || line == "cls") {
            output << "\033[2J\033[H";
            continue;
        }
        if (line == "ENV" || line == "env") {
            for (const auto& entry : environment_entries()) {
                output << entry << '\n';
            }
            last_status = 0;
            continue;
        }
        if (const auto numbered = parse_numbered_line(line)) {
            if (numbered->second.empty()) {
                numbered_program.erase(numbered->first);
            } else {
                numbered_program[numbered->first] = numbered->second;
            }
            continue;
        }
        if (starts_multiline_block(line)) {
            multiline_source = line + "\n";
            multiline_depth = 1;
            continue;
        }
        if (starts_with_word(lowercase(line), "oops")) {
            const std::string fixed_command = shell_args_after_first_word(line);
            if (last_unknown_command.empty()) {
                output << style("No unknown command to correct", "yellow") << '\n';
                continue;
            }
            if (fixed_command.empty()) {
                output << style("usage: oops <correct-command>", "yellow") << '\n';
                continue;
            }

            const std::string previous_args = shell_args_after_first_word(last_unknown_command);
            line = fixed_command + (previous_args.empty() ? "" : " " + previous_args);
            last_unknown_command.clear();
        }
        const std::string shell_line = expand_shell_variables(expand_alias(line), last_status);
        auto run_script_command = [&](const std::filesystem::path& path) -> std::optional<int> {
            const auto words = split_shell_words(shell_line);
            std::vector<std::string> args;
            if (words.size() > 1) {
                args.assign(words.begin() + 1, words.end());
            }
            return run_script(path, args);
        };
        if (const auto profile_script = resolve_profile_script_command(shell_line)) {
            if (const auto exit_code = run_script_command(*profile_script)) {
                return *exit_code;
            }
            continue;
        }
        if (const auto local_script = resolve_local_script_command(shell_line)) {
            if (const auto exit_code = run_script_command(*local_script)) {
                return *exit_code;
            }
            continue;
        }
        if (should_run_as_shell_command(shell_line)) {
            if (has_unquoted_trailing_background_marker(shell_line)) {
                last_status = start_background_job(shell_line, output);
                continue;
            }
#ifndef _WIN32
            if (interactive && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
                try {
                    last_status = run_foreground_shell_command(foreground_shell_command(shell_line));
                    if (last_status == 127) {
                        last_unknown_command = shell_line;
                        output << style("Unknown command. Type: oops <correct-command>", "yellow") << '\n';
                    }
                } catch (const std::exception& error) {
                    output << style(error.what(), "red") << '\n';
                    last_status = 1;
                }
                continue;
            }
#endif
            try {
                const Value result = runtime.call_host_function("RUN", {shell_line});
                const std::string command_output = result.to_string();
                if (!command_output.empty()) {
                    output << command_output << '\n';
                }
                if (result.is_object() && result.get_property("ExitCode").as_number() == 127.0) {
                    last_unknown_command = shell_line;
                    output << style("Unknown command. Type: oops <correct-command>", "yellow") << '\n';
                }
                last_status = result.is_object() ? static_cast<int>(result.get_property("ExitCode").as_number()) : 0;
            } catch (const std::exception& error) {
                output << style(error.what(), "red") << '\n';
                last_status = 1;
            }
            continue;
        }
        const auto result = runtime.run_string(line + "\n");
        if (!result.ok) {
            output << style(result.error, "red") << '\n';
            last_status = 1;
        } else {
            last_status = result.exited ? result.exit_code : 0;
        }
        if (result.exited) {
            return result.exit_code;
        }
    }
    return 0;
}

} // namespace arco::shell
