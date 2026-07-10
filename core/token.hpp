#pragma once

#include <string>

namespace arco {

enum class TokenType {
    End,
    Newline,
    Identifier,
    Number,
    String,
    InterpolatedString,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    Comma,
    Colon,
    Plus,
    PlusEqual,
    Minus,
    MinusEqual,
    Star,
    StarEqual,
    Slash,
    SlashEqual,
    Ampersand,
    AmpersandEqual,
    Pipe,
    PipeEqual,
    Caret,
    CaretEqual,
    Tilde,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    ShiftLeft,
    ShiftLeftEqual,
    Greater,
    GreaterEqual,
    ShiftRight,
    ShiftRightEqual,
    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    Has,
    Add,
    Remove,
    Toggle,
    Flags,
    ShiftLeftWord,
    ShiftRightWord,
    Contains,
    Print,
    Run,
    Let,
    If,
    Then,
    Else,
    EndKeyword,
    While,
    Wend,
    For,
    Function,
    Return,
    Try,
    Catch,
    Goto,
    Stop,
    In,
    To,
    Step,
    Next,
    TrueKeyword,
    FalseKeyword,
    NullKeyword,
};

struct Token {
    TokenType type = TokenType::End;
    std::string lexeme;
    double number = 0.0;
    int line = 1;
    int column = 1;
};

} // namespace arco
