#pragma once

#include "token.hpp"

#include <string>
#include <vector>

namespace arco {

class Lexer {
public:
    explicit Lexer(std::string source);
    std::vector<Token> scan_tokens();

private:
    bool at_end() const;
    char advance();
    char peek() const;
    char peek_next() const;
    bool match(char expected);
    void add(TokenType type);
    void newline();
    void identifier();
    void number();
    void binary_number(std::size_t prefix_length);
    void hex_number(std::size_t prefix_length);
    void string();
    void interpolated_string();

    std::string source_;
    std::vector<Token> tokens_;
    std::size_t start_ = 0;
    std::size_t current_ = 0;
    int line_ = 1;
    int column_ = 1;
    int token_column_ = 1;
};

} // namespace arco
