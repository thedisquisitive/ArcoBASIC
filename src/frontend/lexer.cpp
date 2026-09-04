#include "lexer.hpp"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace arco {

namespace {

std::string upper(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

// Comment bodies keep only their content, not the leading/trailing whitespace around the ' or
// REM marker -- regenerating "REM <text>" from a stored " text  " would otherwise double up
// spacing on round-trip.
std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
}

const std::unordered_map<std::string, TokenType> kKeywords = {
    {"PRINT", TokenType::Print},
    {"RUN", TokenType::Run},
    {"LET", TokenType::Let},
    {"IF", TokenType::If},
    {"THEN", TokenType::Then},
    {"ELSE", TokenType::Else},
    {"SELECT", TokenType::Select},
    {"CASE", TokenType::Case},
    {"END", TokenType::EndKeyword},
    {"WHILE", TokenType::While},
    {"WEND", TokenType::Wend},
    {"DO", TokenType::Do},
    {"LOOP", TokenType::Loop},
    {"UNTIL", TokenType::Until},
    {"FOR", TokenType::For},
    {"FUNCTION", TokenType::Function},
    {"RETURN", TokenType::Return},
    {"TRY", TokenType::Try},
    {"CATCH", TokenType::Catch},
    {"THROW", TokenType::Throw},
    {"ADDRESSOF", TokenType::AddressOf},
    {"GOTO", TokenType::Goto},
    {"STOP", TokenType::Stop},
    {"CLASS", TokenType::Class},
    {"EXTENDS", TokenType::Extends},
    {"INTERFACE", TokenType::Interface},
    {"IMPLEMENTS", TokenType::Implements},
    {"SUPER", TokenType::Super},
    {"SHARED", TokenType::Shared},
    {"PUBLIC", TokenType::Public},
    {"PRIVATE", TokenType::Private},
    {"PROTECTED", TokenType::Protected},
    {"ABSTRACT", TokenType::Abstract},
    {"CONSTRUCTOR", TokenType::Constructor},
    {"AS", TokenType::As},
    {"IN", TokenType::In},
    {"TO", TokenType::To},
    {"STEP", TokenType::Step},
    {"NEXT", TokenType::Next},
    {"TRUE", TokenType::TrueKeyword},
    {"FALSE", TokenType::FalseKeyword},
    {"NULL", TokenType::NullKeyword},
    {"CONTAINS", TokenType::Contains},
    {"BITAND", TokenType::BitAnd},
    {"AND", TokenType::BitAnd},
    {"BITOR", TokenType::BitOr},
    {"OR", TokenType::BitOr},
    {"BITXOR", TokenType::BitXor},
    {"XOR", TokenType::BitXor},
    {"BITNOT", TokenType::BitNot},
    {"NOT", TokenType::BitNot},
    {"ANDALSO", TokenType::AndAlso},
    {"ORELSE", TokenType::OrElse},
    {"HAS", TokenType::Has},
    {"ADD", TokenType::Add},
    {"REMOVE", TokenType::Remove},
    {"TOGGLE", TokenType::Toggle},
    {"FLAGS", TokenType::Flags},
    {"SHL", TokenType::ShiftLeftWord},
    {"SHR", TokenType::ShiftRightWord},
    {"SAR", TokenType::ShiftArithmeticRightWord},
};

} // namespace

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

std::vector<Token> Lexer::scan_tokens() {
    while (!at_end()) {
        start_ = current_;
        token_column_ = column_;
        const char c = advance();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                break;
            case '\n':
                newline();
                break;
            case '\'': {
                const std::size_t text_start = current_;
                while (peek() != '\n' && !at_end()) {
                    advance();
                }
                tokens_.push_back(Token{TokenType::Comment, trim(source_.substr(text_start, current_ - text_start)), 0.0, line_, token_column_});
                break;
            }
            case '(':
                add(TokenType::LeftParen);
                break;
            case ')':
                add(TokenType::RightParen);
                break;
            case '[':
                add(TokenType::LeftBracket);
                break;
            case ']':
                add(TokenType::RightBracket);
                break;
            case '{':
                add(TokenType::LeftBrace);
                break;
            case '}':
                add(TokenType::RightBrace);
                break;
            case ',':
                add(TokenType::Comma);
                break;
            case ':':
                add(TokenType::Colon);
                break;
            case '.':
                // Reached only when '.' is NOT immediately consumed as part of a number literal
                // (number()'s own '.' handling) or as a continuation character inside identifier()
                // 's dotted-name fusion -- i.e. a '.' following a ')', ']', or another already-
                // lexed token, as in `expr.Method()` chained onto a call/index result.
                add(TokenType::Dot);
                break;
            case '+':
                add(match('=') ? TokenType::PlusEqual : TokenType::Plus);
                break;
            case '-':
                add(match('=') ? TokenType::MinusEqual : TokenType::Minus);
                break;
            case '*':
                add(match('=') ? TokenType::StarEqual : TokenType::Star);
                break;
            case '/':
                add(match('=') ? TokenType::SlashEqual : TokenType::Slash);
                break;
            case '\\':
                add(TokenType::Backslash);
                break;
            case '$':
                if (match('"')) {
                    interpolated_string();
                    break;
                }
                throw std::runtime_error("unexpected character at line " + std::to_string(line_));
            case '%':
                if (peek() == '0' || peek() == '1') {
                    binary_number(1);
                } else {
                    add(TokenType::Mod);
                }
                break;
            case '&':
                if (peek() == 'H' || peek() == 'h') {
                    advance();
                    hex_number(2);
                } else {
                    add(match('&') ? TokenType::LogicalAnd : (match('=') ? TokenType::AmpersandEqual : TokenType::Ampersand));
                }
                break;
            case '|':
                add(match('|') ? TokenType::LogicalOr : (match('=') ? TokenType::PipeEqual : TokenType::Pipe));
                break;
            case '^':
                add(match('=') ? TokenType::CaretEqual : TokenType::Caret);
                break;
            case '~':
                add(TokenType::Tilde);
                break;
            case '!':
                add(match('=') ? TokenType::NotEqual : TokenType::Bang);
                break;
            case '=':
                match('=');
                add(TokenType::Equal);
                break;
            case '<':
                if (match('<')) {
                    add(match('=') ? TokenType::ShiftLeftEqual : TokenType::ShiftLeft);
                } else {
                    add(match('=') ? TokenType::LessEqual : (match('>') ? TokenType::NotEqual : TokenType::Less));
                }
                break;
            case '>':
                if (match('>')) {
                    add(match('=') ? TokenType::ShiftRightEqual : TokenType::ShiftRight);
                } else {
                    add(match('=') ? TokenType::GreaterEqual : TokenType::Greater);
                }
                break;
            case '"':
                string();
                break;
            case '#':
                if (token_column_ == 1 && peek() == '!') {
                    while (peek() != '\n' && !at_end()) {
                        advance();
                    }
                    break;
                }
                throw std::runtime_error("unexpected character at line " + std::to_string(line_));
            default:
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    number();
                } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                    identifier();
                } else {
                    throw std::runtime_error("unexpected character at line " + std::to_string(line_));
                }
                break;
        }
    }
    tokens_.push_back(Token{TokenType::End, "", 0.0, line_, column_});
    return tokens_;
}

bool Lexer::at_end() const {
    return current_ >= source_.size();
}

char Lexer::advance() {
    column_++;
    return source_[current_++];
}

char Lexer::peek() const {
    return at_end() ? '\0' : source_[current_];
}

char Lexer::peek_next() const {
    return current_ + 1 >= source_.size() ? '\0' : source_[current_ + 1];
}

bool Lexer::match(char expected) {
    if (at_end() || source_[current_] != expected) {
        return false;
    }
    current_++;
    column_++;
    return true;
}

void Lexer::add(TokenType type) {
    tokens_.push_back(Token{type, source_.substr(start_, current_ - start_), 0.0, line_, token_column_});
}

void Lexer::newline() {
    tokens_.push_back(Token{TokenType::Newline, "\n", 0.0, line_, token_column_});
    line_++;
    column_ = 1;
}

void Lexer::identifier() {
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_' || peek() == '.') {
        advance();
    }
    const std::string text = source_.substr(start_, current_ - start_);
    if (upper(text) == "REM") {
        const std::size_t text_start = current_;
        while (peek() != '\n' && !at_end()) {
            advance();
        }
        tokens_.push_back(Token{TokenType::Comment, trim(source_.substr(text_start, current_ - text_start)), 0.0, line_, token_column_});
        return;
    }
    const auto found = kKeywords.find(upper(text));
    TokenType type = found == kKeywords.end() ? TokenType::Identifier : found->second;
    if (upper(text) == "BITS") {
        std::size_t lookahead = current_;
        while (lookahead < source_.size() && (source_[lookahead] == ' ' || source_[lookahead] == '\t')) ++lookahead;
        if (lookahead < source_.size() && source_[lookahead] == '"') type = TokenType::Bits;
    }
    tokens_.push_back(Token{type, text, 0.0, line_, token_column_});
}

void Lexer::number() {
    if (source_[start_] == '0' && (peek() == 'b' || peek() == 'B')) {
        advance();
        binary_number(2);
        return;
    }
    if (source_[start_] == '0' && (peek() == 'x' || peek() == 'X')) {
        advance();
        hex_number(2);
        return;
    }
    while (std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) {
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }
    const std::string text = source_.substr(start_, current_ - start_);
    tokens_.push_back(Token{TokenType::Number, text, std::stod(text), line_, token_column_});
}

void Lexer::binary_number(std::size_t prefix_length) {
    while (peek() == '0' || peek() == '1') {
        advance();
    }
    const std::string text = source_.substr(start_, current_ - start_);
    if (text.size() == prefix_length) {
        throw std::runtime_error("expected binary digits at line " + std::to_string(line_));
    }
    const std::string digits = text.substr(prefix_length);
    tokens_.push_back(Token{TokenType::Number, text, static_cast<double>(std::stoll(digits, nullptr, 2)), line_, token_column_});
}

void Lexer::hex_number(std::size_t prefix_length) {
    while (std::isxdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }
    const std::string text = source_.substr(start_, current_ - start_);
    if (text.size() == prefix_length) {
        throw std::runtime_error("expected hexadecimal digits at line " + std::to_string(line_));
    }
    const std::string digits = text.substr(prefix_length);
    tokens_.push_back(Token{TokenType::Number, text, static_cast<double>(std::stoll(digits, nullptr, 16)), line_, token_column_});
}

void Lexer::string() {
    std::string value;
    while (peek() != '"' && !at_end()) {
        if (peek() == '\n') {
            throw std::runtime_error("unterminated string at line " + std::to_string(line_));
        }
        if (peek() == '\\') {
            advance();
            if (at_end()) {
                throw std::runtime_error("unterminated string at line " + std::to_string(line_));
            }
            const char escaped = advance();
            switch (escaped) {
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                case '"':
                    value.push_back('"');
                    break;
                case '\\':
                    value.push_back('\\');
                    break;
                case '0':
                    value.push_back('\0');
                    break;
                default:
                    value.push_back(escaped);
                    break;
            }
        } else {
            value.push_back(advance());
        }
    }
    if (at_end()) {
        throw std::runtime_error("unterminated string at line " + std::to_string(line_));
    }
    advance();
    tokens_.push_back(Token{TokenType::String, value, 0.0, line_, token_column_});
}

void Lexer::interpolated_string() {
    std::string value;
    while (peek() != '"' && !at_end()) {
        if (peek() == '\n') {
            throw std::runtime_error("unterminated interpolated string at line " + std::to_string(line_));
        }
        if (peek() == '\\') {
            advance();
            if (at_end()) {
                throw std::runtime_error("unterminated interpolated string at line " + std::to_string(line_));
            }
            const char escaped = advance();
            switch (escaped) {
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                case '"':
                    value.push_back('"');
                    break;
                case '\\':
                    value.push_back('\\');
                    break;
                case '0':
                    value.push_back('\0');
                    break;
                default:
                    value.push_back(escaped);
                    break;
            }
        } else {
            value.push_back(advance());
        }
    }
    if (at_end()) {
        throw std::runtime_error("unterminated interpolated string at line " + std::to_string(line_));
    }
    advance();
    tokens_.push_back(Token{TokenType::InterpolatedString, value, 0.0, line_, token_column_});
}

} // namespace arco
