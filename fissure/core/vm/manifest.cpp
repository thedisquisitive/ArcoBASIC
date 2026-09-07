#include "fissure/manifest.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

namespace fissure {

namespace {

std::string trim(const std::string& text) {
    std::size_t start = text.find_first_not_of(" \t\r");
    if (start == std::string::npos) return "";
    std::size_t end = text.find_last_not_of(" \t\r");
    return text.substr(start, end - start + 1);
}

// Strips a leading ArcoBASIC line-comment marker (`'`) and surrounding space, or returns nullopt
// if the line isn't a comment at all.
std::optional<std::string> comment_body(const std::string& line) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] != '\'') return std::nullopt;
    return trim(trimmed.substr(1));
}

// `#DIRECTIVE rest-of-line` -> {"DIRECTIVE", "rest-of-line"}, or nullopt if `body` isn't a
// directive line at all (an ordinary explanatory comment, which manifests are free to mix in).
std::optional<std::pair<std::string, std::string>> parse_directive(const std::string& body) {
    if (body.empty() || body[0] != '#') return std::nullopt;
    std::size_t space = body.find_first_of(" \t");
    std::string directive = body.substr(1, space == std::string::npos ? std::string::npos : space - 1);
    std::string rest = space == std::string::npos ? "" : trim(body.substr(space + 1));
    for (char& c : directive) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return std::make_pair(directive, rest);
}

std::string strip_quotes(const std::string& text) {
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

} // namespace

Manifest parse_manifest(const std::string& source) {
    Manifest manifest;
    std::istringstream input(source);
    std::string line;
    while (std::getline(input, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue; // blank lines don't end the header region
        auto body = comment_body(line);
        if (!body) break; // first non-comment line ends the manifest header region

        auto directive = parse_directive(*body);
        if (!directive) continue; // an ordinary comment inside the header region, not a directive

        const std::string& name = directive->first;
        const std::string& rest = directive->second;
        if (name == "FISSURE-PLUGIN") {
            manifest.present = true;
            try {
                manifest.version = rest.empty() ? 1 : std::stoi(rest);
            } catch (...) {
                manifest.version = 1;
            }
        } else if (name == "NAME") {
            manifest.name = strip_quotes(rest);
        } else if (name == "TYPE") {
            manifest.type = rest;
        } else if (name == "REQUIRES") {
            manifest.capabilities.insert(rest);
        }
    }
    return manifest;
}

} // namespace fissure
