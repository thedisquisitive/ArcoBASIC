#include "arco/fission.hpp"

#include "arco/calling_convention.hpp"
#include "arco/runtime.hpp"
#include "arco/pe_image.hpp"
#include "arco/uefi_bindings.hpp"
#include "arco/utf16.hpp"
#include "arco/x86_64_encoder.hpp"

#include "../core/lexer.hpp"
#include "../core/parser.hpp"

#include <filesystem>
#include <fstream>
#include <cmath>
#include <iomanip>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace arco::fission {
namespace {

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string escaped(const std::string& text) {
    std::ostringstream out;
    for (char c : text) {
        switch (c) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << c;
                break;
        }
    }
    return out.str();
}

std::string cpp_string_literal(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text) {
        switch (c) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            case '\0':
                out << "\\0";
                break;
            default:
                if (c < 32 || c > 126) {
                    out << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c)
                        << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(c);
                }
                break;
        }
    }
    out << '"';
    return out.str();
}

std::string shell_quote(const std::string& text) {
    std::string out = "'";
    for (char c : text) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

std::string token_text(const Token& token) {
    switch (token.type) {
        case TokenType::String:
        case TokenType::InterpolatedString:
            return "\"" + escaped(token.lexeme) + "\"";
        case TokenType::Number: {
            std::ostringstream out;
            out << std::setprecision(15) << token.number;
            return out.str();
        }
        case TokenType::TrueKeyword:
            return "true";
        case TokenType::FalseKeyword:
            return "false";
        case TokenType::NullKeyword:
            return "nothing";
        default:
            return token.lexeme;
    }
}

std::string join_expression(const std::vector<Token>& tokens, std::size_t begin, std::size_t end) {
    std::ostringstream out;
    bool first = true;
    for (std::size_t i = begin; i < end; ++i) {
        if (!first) {
            out << ' ';
        }
        first = false;
        out << token_text(tokens[i]);
    }
    return out.str();
}

std::size_t line_end(const std::vector<Token>& tokens, std::size_t index) {
    while (index < tokens.size() && tokens[index].type != TokenType::Newline && tokens[index].type != TokenType::End) {
        ++index;
    }
    return index;
}

bool is_line_number(const std::vector<Token>& tokens, std::size_t index) {
    return index + 1 < tokens.size() && tokens[index].type == TokenType::Number &&
           tokens[index + 1].type != TokenType::Newline && tokens[index + 1].type != TokenType::End;
}

std::string statement_comment(const std::vector<Token>& tokens, std::size_t begin, std::size_t end) {
    const auto text = join_expression(tokens, begin, end);
    return text.empty() ? "empty statement" : text;
}

std::vector<std::string> split_identifier_path(const std::string& name) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t dot = name.find('.', start);
        const std::size_t end = dot == std::string::npos ? name.size() : dot;
        if (end > start) {
            parts.push_back(name.substr(start, end - start));
        }
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    return parts;
}

bool is_property_path(const std::string& name) {
    const auto parts = split_identifier_path(name);
    return parts.size() > 1;
}

struct AmirInstruction {
    enum class Kind {
        Label,
        Source,
        Eval,
        Const,
        Load,
        Unary,
        Binary,
        CallValue,
        // A call through a declared function parameter (docs/systems/uefi-target.md section 5)
        // rather than a namespaced host/stdlib function -- e.g. calling a method reached off a
        // UEFI protocol pointer received as a parameter. Structurally identical to CallValue;
        // kept distinct so later work packages (ABI/codegen) can tell them apart without
        // re-deriving the classification. See Packet WP-004 "external or ABI-bound function
        // calls".
        CallExternal,
        Array,
        Object,
        Index,
        Store,
        StoreIndex,
        Call,
        Jump,
        Branch,
        TryBegin,
        TryEnd,
        DeclareFunction,
        DeclareClass,
        DeclareInterface,
        Return,
        Unsupported,
    };

    Kind kind;
    std::string result;
    std::string target;
    std::vector<std::string> operands;
    int source_line = 0;
};

struct AmirBlock {
    std::string name;
    std::vector<AmirInstruction> instructions;
};

struct AmirFunction {
    std::string name;
    std::string return_type;
    std::vector<std::string> params;
    std::vector<AmirBlock> blocks;
};

struct AmirModule {
    std::string source_name;
    int version = 0;
    std::vector<AmirFunction> functions;
    std::vector<std::string> diagnostics;
};

AmirInstruction amir_label(std::string target) {
    AmirInstruction instruction{AmirInstruction::Kind::Label};
    instruction.target = std::move(target);
    return instruction;
}

AmirInstruction amir_source(int source_line) {
    AmirInstruction instruction{AmirInstruction::Kind::Source};
    instruction.source_line = source_line;
    return instruction;
}

AmirInstruction amir_eval(std::string result, std::string expression) {
    AmirInstruction instruction{AmirInstruction::Kind::Eval};
    instruction.result = std::move(result);
    instruction.operands.push_back(std::move(expression));
    return instruction;
}

AmirInstruction amir_const(std::string result, std::string value) {
    AmirInstruction instruction{AmirInstruction::Kind::Const};
    instruction.result = std::move(result);
    instruction.operands.push_back(std::move(value));
    return instruction;
}

AmirInstruction amir_load(std::string result, std::string name) {
    AmirInstruction instruction{AmirInstruction::Kind::Load};
    instruction.result = std::move(result);
    instruction.target = std::move(name);
    return instruction;
}

AmirInstruction amir_unary(std::string result, std::string op, std::string value) {
    AmirInstruction instruction{AmirInstruction::Kind::Unary};
    instruction.result = std::move(result);
    instruction.target = std::move(op);
    instruction.operands.push_back(std::move(value));
    return instruction;
}

AmirInstruction amir_binary(std::string result, std::string op, std::string left, std::string right) {
    AmirInstruction instruction{AmirInstruction::Kind::Binary};
    instruction.result = std::move(result);
    instruction.target = std::move(op);
    instruction.operands.push_back(std::move(left));
    instruction.operands.push_back(std::move(right));
    return instruction;
}

AmirInstruction amir_call_value(std::string result, std::string target, std::vector<std::string> operands) {
    AmirInstruction instruction{AmirInstruction::Kind::CallValue};
    instruction.result = std::move(result);
    instruction.target = std::move(target);
    instruction.operands = std::move(operands);
    return instruction;
}

AmirInstruction amir_array(std::string result, std::vector<std::string> operands) {
    AmirInstruction instruction{AmirInstruction::Kind::Array};
    instruction.result = std::move(result);
    instruction.operands = std::move(operands);
    return instruction;
}

AmirInstruction amir_object(std::string result, std::vector<std::string> fields) {
    AmirInstruction instruction{AmirInstruction::Kind::Object};
    instruction.result = std::move(result);
    instruction.operands = std::move(fields);
    return instruction;
}

AmirInstruction amir_index(std::string result, std::string target, std::string index) {
    AmirInstruction instruction{AmirInstruction::Kind::Index};
    instruction.result = std::move(result);
    instruction.target = std::move(target);
    instruction.operands.push_back(std::move(index));
    return instruction;
}

AmirInstruction amir_store(std::string target, std::string value) {
    AmirInstruction instruction{AmirInstruction::Kind::Store};
    instruction.target = std::move(target);
    instruction.operands.push_back(std::move(value));
    return instruction;
}

AmirInstruction amir_store_index(std::string target, std::vector<std::string> operands) {
    AmirInstruction instruction{AmirInstruction::Kind::StoreIndex};
    instruction.target = std::move(target);
    instruction.operands = std::move(operands);
    return instruction;
}

AmirInstruction amir_call(std::string target, std::vector<std::string> operands) {
    AmirInstruction instruction{AmirInstruction::Kind::Call};
    instruction.target = std::move(target);
    instruction.operands = std::move(operands);
    return instruction;
}

AmirInstruction amir_jump(std::string target) {
    AmirInstruction instruction{AmirInstruction::Kind::Jump};
    instruction.target = std::move(target);
    return instruction;
}

AmirInstruction amir_branch(std::string condition, std::string true_target, std::string false_target) {
    AmirInstruction instruction{AmirInstruction::Kind::Branch};
    instruction.operands.push_back(std::move(condition));
    instruction.operands.push_back(std::move(true_target));
    instruction.operands.push_back(std::move(false_target));
    return instruction;
}

AmirInstruction amir_try_begin(std::string catch_target, std::string error_name) {
    AmirInstruction instruction{AmirInstruction::Kind::TryBegin};
    instruction.target = std::move(catch_target);
    instruction.operands.push_back(std::move(error_name));
    return instruction;
}

AmirInstruction amir_try_end() {
    return AmirInstruction{AmirInstruction::Kind::TryEnd};
}

AmirInstruction amir_declare_class(std::string name, std::vector<std::string> metadata) {
    AmirInstruction instruction{AmirInstruction::Kind::DeclareClass};
    instruction.target = std::move(name);
    instruction.operands = std::move(metadata);
    return instruction;
}

AmirInstruction amir_declare_function(std::string name, std::vector<std::string> metadata) {
    AmirInstruction instruction{AmirInstruction::Kind::DeclareFunction};
    instruction.target = std::move(name);
    instruction.operands = std::move(metadata);
    return instruction;
}

AmirInstruction amir_declare_interface(std::string name, std::vector<std::string> metadata) {
    AmirInstruction instruction{AmirInstruction::Kind::DeclareInterface};
    instruction.target = std::move(name);
    instruction.operands = std::move(metadata);
    return instruction;
}

AmirInstruction amir_return(std::string type_name, std::string value) {
    AmirInstruction instruction{AmirInstruction::Kind::Return};
    instruction.target = std::move(type_name);
    instruction.operands.push_back(std::move(value));
    return instruction;
}

AmirInstruction amir_unsupported(std::string text) {
    AmirInstruction instruction{AmirInstruction::Kind::Unsupported};
    instruction.operands.push_back(std::move(text));
    return instruction;
}

bool is_terminal_instruction(const AmirInstruction& instruction) {
    return instruction.kind == AmirInstruction::Kind::Return || instruction.kind == AmirInstruction::Kind::Jump ||
           instruction.kind == AmirInstruction::Kind::Branch;
}

// function.params entries are whole joined-token parameter texts (e.g. "systemTable AS
// UEFI.SystemTable" or just "count" when untyped); the parameter's bare name is always the
// first whitespace-delimited word regardless of whether a type annotation follows.
bool function_has_parameter(const AmirFunction& function, const std::string& name) {
    for (const std::string& param : function.params) {
        const auto space = param.find(' ');
        const std::string param_name = space == std::string::npos ? param : param.substr(0, space);
        if (param_name == name) {
            return true;
        }
    }
    return false;
}

std::string upper_ascii(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return text;
}

bool identifier_is(const Token& token, const std::string& word) {
    return token.type == TokenType::Identifier && upper_ascii(token.lexeme) == word;
}

bool is_loop_variable_token(const Token& token) {
    return token.type == TokenType::Identifier || token.type == TokenType::Run;
}

class ExpressionLowerer {
public:
    ExpressionLowerer(AmirBlock& block, const std::vector<Token>& tokens, std::size_t begin, std::size_t end, int& temporary)
        : block_(block), tokens_(tokens), end_(end), current_(begin), temporary_(temporary) {}

    bool lower(std::string& result) {
        result = expression();
        return ok_ && current_ == end_ && !result.empty();
    }

private:
    std::string expression() {
        return logical_or();
    }

    std::string logical_or() {
        std::string left = logical_and();
        while (match(TokenType::LogicalOr) || match(TokenType::OrElse)) {
            const Token op = previous();
            left = binary(op.type == TokenType::OrElse ? "ORELSE" : "||", left, logical_and());
        }
        return left;
    }

    std::string logical_and() {
        std::string left = bit_or();
        while (match(TokenType::LogicalAnd) || match(TokenType::AndAlso)) {
            const Token op = previous();
            left = binary(op.type == TokenType::AndAlso ? "ANDALSO" : "&&", left, bit_or());
        }
        return left;
    }

    std::string bit_or() {
        std::string left = bit_xor();
        while (match(TokenType::Pipe) || match(TokenType::BitOr)) {
            const Token op = previous();
            left = binary(operator_text(op), left, bit_xor());
        }
        return left;
    }

    std::string bit_xor() {
        std::string left = bit_and();
        while (match(TokenType::Caret) || match(TokenType::BitXor)) {
            const Token op = previous();
            left = binary(operator_text(op), left, bit_and());
        }
        return left;
    }

    std::string bit_and() {
        std::string left = equality();
        while (match(TokenType::Ampersand) || match(TokenType::BitAnd)) {
            const Token op = previous();
            left = binary(operator_text(op), left, equality());
        }
        return left;
    }

    std::string equality() {
        std::string left = comparison();
        while (match(TokenType::Equal) || match(TokenType::NotEqual)) {
            const Token op = previous();
            left = binary(op.type == TokenType::Equal ? "==" : "!=", left, comparison());
        }
        return left;
    }

    std::string comparison() {
        std::string left = shift();
        while (match_any({TokenType::Less, TokenType::LessEqual, TokenType::Greater, TokenType::GreaterEqual, TokenType::Contains})) {
            const Token op = previous();
            left = binary(operator_text(op), left, shift());
        }
        return left;
    }

    std::string shift() {
        std::string left = term();
        while (match(TokenType::ShiftLeft) || match(TokenType::ShiftLeftWord) || match(TokenType::ShiftRight) || match(TokenType::ShiftRightWord)) {
            const Token op = previous();
            left = binary(operator_text(op), left, term());
        }
        return left;
    }

    std::string term() {
        std::string left = factor();
        while (match(TokenType::Plus) || match(TokenType::Minus)) {
            const Token op = previous();
            left = binary(op.type == TokenType::Plus ? "+" : "-", left, factor());
        }
        return left;
    }

    std::string factor() {
        std::string left = unary();
        while (match(TokenType::Star) || match(TokenType::Slash) || match(TokenType::Mod) || match_identifier("MOD")) {
            const Token op = previous();
            left = binary(op.type == TokenType::Slash ? "/" : op.type == TokenType::Star ? "*" : "MOD", left, unary());
        }
        return left;
    }

    std::string unary() {
        if (match(TokenType::Minus) || match(TokenType::Bang) || match(TokenType::Tilde) || match(TokenType::BitNot) || match_identifier("NOT")) {
            const Token op = previous();
            return unary_instruction(operator_text(op), unary());
        }
        return postfix();
    }

    std::string postfix() {
        std::string target = primary();
        while (match(TokenType::LeftBracket)) {
            skip_newlines();
            std::string index = expression();
            skip_newlines();
            if (index.empty() || !match(TokenType::RightBracket)) {
                ok_ = false;
                return "";
            }
            target = index_value(target, index);
        }
        return target;
    }

    std::string primary() {
        if (match(TokenType::Number) || match(TokenType::String) || match(TokenType::TrueKeyword) || match(TokenType::FalseKeyword) ||
            match(TokenType::NullKeyword)) {
            return constant(token_text(previous()));
        }
        if (match(TokenType::Identifier)) {
            const std::string name = previous().lexeme;
            if (match(TokenType::LeftParen)) {
                return call_value(name);
            }
            if (is_property_path(name)) {
                return property_path(name);
            }
            return load(name);
        }
        if (match(TokenType::Run)) {
            const std::string name = "RUN";
            if (match(TokenType::LeftParen)) {
                return call_value(name);
            }
            return load(name);
        }
        if (match(TokenType::LeftParen)) {
            skip_newlines();
            std::string result = expression();
            skip_newlines();
            if (!match(TokenType::RightParen)) {
                ok_ = false;
            }
            return result;
        }
        if (match(TokenType::LeftBracket)) {
            return array_literal();
        }
        if (match(TokenType::LeftBrace)) {
            return object_literal();
        }
        ok_ = false;
        return "";
    }

    std::string call_value(const std::string& name) {
        std::vector<std::string> args;
        skip_newlines();
        if (!check(TokenType::RightParen)) {
            while (true) {
                std::string arg = expression();
                if (arg.empty()) {
                    ok_ = false;
                    return "";
                }
                args.push_back(std::move(arg));
                skip_newlines();
                if (!match(TokenType::Comma)) {
                    break;
                }
                skip_newlines();
                if (check(TokenType::RightParen)) {
                    break;
                }
            }
        }
        if (!match(TokenType::RightParen)) {
            ok_ = false;
            return "";
        }

        const std::string result = next_temp();
        block_.instructions.push_back(amir_call_value(result, name, std::move(args)));
        return result;
    }

    std::string array_literal() {
        std::vector<std::string> items;
        skip_newlines();
        if (!check(TokenType::RightBracket)) {
            while (true) {
                std::string item = expression();
                if (item.empty()) {
                    ok_ = false;
                    return "";
                }
                items.push_back(std::move(item));
                skip_newlines();
                if (!match(TokenType::Comma)) {
                    break;
                }
                skip_newlines();
                if (check(TokenType::RightBracket)) {
                    break;
                }
            }
        }
        if (!match(TokenType::RightBracket)) {
            ok_ = false;
            return "";
        }

        const std::string result = next_temp();
        block_.instructions.push_back(amir_array(result, std::move(items)));
        return result;
    }

    std::string object_literal() {
        std::vector<std::string> fields;
        skip_newlines();
        if (!check(TokenType::RightBrace)) {
            while (true) {
                std::string key;
                if (match(TokenType::String)) {
                    key = token_text(previous());
                } else if (match(TokenType::Identifier)) {
                    key = previous().lexeme;
                } else {
                    ok_ = false;
                    return "";
                }
                if (!match(TokenType::Colon)) {
                    ok_ = false;
                    return "";
                }
                skip_newlines();
                std::string value = expression();
                if (value.empty()) {
                    ok_ = false;
                    return "";
                }
                fields.push_back(std::move(key) + ":" + value);
                skip_newlines();
                if (!match(TokenType::Comma)) {
                    break;
                }
                skip_newlines();
                if (check(TokenType::RightBrace)) {
                    break;
                }
            }
        }
        if (!match(TokenType::RightBrace)) {
            ok_ = false;
            return "";
        }

        const std::string result = next_temp();
        block_.instructions.push_back(amir_object(result, std::move(fields)));
        return result;
    }

    std::string constant(const std::string& value) {
        const std::string result = next_temp();
        block_.instructions.push_back(amir_const(result, value));
        return result;
    }

    std::string index_value(const std::string& target, const std::string& index) {
        const std::string result = next_temp();
        block_.instructions.push_back(amir_index(result, target, index));
        return result;
    }

    std::string property_path(const std::string& name) {
        const auto parts = split_identifier_path(name);
        if (parts.empty()) {
            ok_ = false;
            return "";
        }
        std::string target = load(parts.front());
        for (std::size_t i = 1; i < parts.size(); ++i) {
            target = index_value(target, constant("\"" + escaped(parts[i]) + "\""));
        }
        return target;
    }

    std::string load(const std::string& name) {
        const std::string result = next_temp();
        block_.instructions.push_back(amir_load(result, name));
        return result;
    }

    std::string unary_instruction(const std::string& op, const std::string& value) {
        if (value.empty()) {
            ok_ = false;
            return "";
        }
        const std::string result = next_temp();
        block_.instructions.push_back(amir_unary(result, op, value));
        return result;
    }

    std::string binary(const std::string& op, const std::string& left, const std::string& right) {
        if (left.empty() || right.empty()) {
            ok_ = false;
            return "";
        }
        const std::string result = next_temp();
        block_.instructions.push_back(amir_binary(result, op, left, right));
        return result;
    }

    std::string next_temp() {
        return "%t" + std::to_string(temporary_++);
    }

    bool at_end() const {
        return current_ >= end_;
    }

    const Token& previous() const {
        return tokens_[current_ - 1];
    }

    bool match(TokenType type) {
        if (at_end() || tokens_[current_].type != type) {
            return false;
        }
        ++current_;
        return true;
    }

    bool match_any(std::initializer_list<TokenType> types) {
        for (TokenType type : types) {
            if (match(type)) {
                return true;
            }
        }
        return false;
    }

    bool check(TokenType type) const {
        return !at_end() && tokens_[current_].type == type;
    }

    void skip_newlines() {
        while (match(TokenType::Newline)) {}
    }

    bool match_identifier(const std::string& word) {
        if (at_end() || !identifier_is(tokens_[current_], word)) {
            return false;
        }
        ++current_;
        return true;
    }

    std::string operator_text(const Token& token) const {
        switch (token.type) {
            case TokenType::Plus:
                return "+";
            case TokenType::Minus:
                return "-";
            case TokenType::Star:
                return "*";
            case TokenType::Slash:
                return "/";
            case TokenType::Mod:
                return "MOD";
            case TokenType::Ampersand:
            case TokenType::BitAnd:
                return "&";
            case TokenType::Pipe:
            case TokenType::BitOr:
                return "|";
            case TokenType::Caret:
            case TokenType::BitXor:
                return "^";
            case TokenType::Bang:
                return "!";
            case TokenType::Tilde:
            case TokenType::BitNot:
                return "~";
            case TokenType::Less:
                return "<";
            case TokenType::LessEqual:
                return "<=";
            case TokenType::Greater:
                return ">";
            case TokenType::GreaterEqual:
                return ">=";
            case TokenType::ShiftLeft:
            case TokenType::ShiftLeftWord:
                return "<<";
            case TokenType::ShiftRight:
            case TokenType::ShiftRightWord:
                return ">>";
            case TokenType::Contains:
                return "CONTAINS";
            default:
                if (identifier_is(token, "NOT")) {
                    return "NOT";
                }
                if (identifier_is(token, "MOD")) {
                    return "MOD";
                }
                return token.lexeme;
        }
    }

    AmirBlock& block_;
    const std::vector<Token>& tokens_;
    std::size_t end_;
    std::size_t current_;
    int& temporary_;
    bool ok_ = true;
};

std::string lower_expression(AmirBlock& block, const std::vector<Token>& tokens, std::size_t begin, std::size_t end, int& temporary) {
    const auto instruction_count = block.instructions.size();
    const int original_temporary = temporary;
    ExpressionLowerer lowerer(block, tokens, begin, end, temporary);
    std::string result;
    if (begin < end && lowerer.lower(result)) {
        return result;
    }

    block.instructions.resize(instruction_count);
    temporary = original_temporary;
    const std::string temp = "%t" + std::to_string(temporary++);
    const std::string expression = join_expression(tokens, begin, end);
    block.instructions.push_back(amir_eval(temp, expression.empty() ? "nothing" : expression));
    return temp;
}

std::size_t matching_bracket(const std::vector<Token>& tokens, std::size_t open, std::size_t end) {
    int depth = 0;
    for (std::size_t index = open; index < end; ++index) {
        if (tokens[index].type == TokenType::LeftBracket) {
            ++depth;
        } else if (tokens[index].type == TokenType::RightBracket) {
            --depth;
            if (depth == 0) {
                return index;
            }
        }
    }
    return end;
}

bool lower_assignment(AmirBlock& block, const std::vector<Token>& tokens, std::size_t target_begin, std::size_t end, int& temporary,
                       bool allow_type_annotation) {
    const auto instruction_count = block.instructions.size();
    const int original_temporary = temporary;
    const auto fail = [&]() {
        block.instructions.resize(instruction_count);
        temporary = original_temporary;
        return false;
    };

    if (target_begin >= end || tokens[target_begin].type != TokenType::Identifier) {
        return fail();
    }

    const std::string name = tokens[target_begin].lexeme;
    const auto property_parts = split_identifier_path(name);
    std::vector<std::string> indexes;
    std::size_t index = target_begin + 1;
    if (allow_type_annotation && index < end && tokens[index].type == TokenType::As) {
        // LET name AS Type = expr: the type annotation is validated by
        // Parser::validate_fixed_width_initializer during the parse pass that
        // always runs before A-MIR lowering (see reveal_amir/reveal_bytecode);
        // A-MIR itself does not yet carry fixed-width type information
        // (deferred to WP-004), so the annotation is skipped here and the
        // declaration lowers exactly like an untyped LET.
        if (index + 1 >= end || tokens[index + 1].type != TokenType::Identifier) {
            return fail();
        }
        index += 2;
    }
    if (property_parts.size() > 1) {
        for (std::size_t i = 1; i < property_parts.size(); ++i) {
            const std::string property = "%t" + std::to_string(temporary++);
            block.instructions.push_back(amir_const(property, "\"" + escaped(property_parts[i]) + "\""));
            indexes.push_back(property);
        }
    }
    while (index < end && tokens[index].type == TokenType::LeftBracket) {
        const std::size_t close = matching_bracket(tokens, index, end);
        if (close >= end) {
            return fail();
        }
        indexes.push_back(lower_expression(block, tokens, index + 1, close, temporary));
        index = close + 1;
    }

    if (index >= end || tokens[index].type != TokenType::Equal) {
        return fail();
    }

    const std::string value = lower_expression(block, tokens, index + 1, end, temporary);
    if (indexes.empty()) {
        block.instructions.push_back(amir_store(name, value));
    } else {
        indexes.push_back(value);
        block.instructions.push_back(amir_store_index(property_parts.empty() ? name : property_parts.front(), std::move(indexes)));
    }
    return true;
}

bool is_compound_assignment(TokenType type) {
    switch (type) {
        case TokenType::PlusEqual:
        case TokenType::MinusEqual:
        case TokenType::StarEqual:
        case TokenType::SlashEqual:
        case TokenType::AmpersandEqual:
        case TokenType::PipeEqual:
        case TokenType::CaretEqual:
        case TokenType::ShiftLeftEqual:
        case TokenType::ShiftRightEqual:
            return true;
        default:
            return false;
    }
}

std::string compound_operator(TokenType type) {
    switch (type) {
        case TokenType::PlusEqual:
            return "+";
        case TokenType::MinusEqual:
            return "-";
        case TokenType::StarEqual:
            return "*";
        case TokenType::SlashEqual:
            return "/";
        case TokenType::AmpersandEqual:
            return "&";
        case TokenType::PipeEqual:
            return "|";
        case TokenType::CaretEqual:
            return "^";
        case TokenType::ShiftLeftEqual:
            return "<<";
        case TokenType::ShiftRightEqual:
            return ">>";
        default:
            return "";
    }
}

bool lower_compound_assignment(AmirBlock& block, const std::vector<Token>& tokens, std::size_t begin, std::size_t end, int& temporary) {
    if (begin + 2 >= end || tokens[begin].type != TokenType::Identifier || !is_compound_assignment(tokens[begin + 1].type)) {
        return false;
    }

    const std::string current = "%t" + std::to_string(temporary++);
    block.instructions.push_back(amir_load(current, tokens[begin].lexeme));
    const std::string value = lower_expression(block, tokens, begin + 2, end, temporary);
    const std::string result = "%t" + std::to_string(temporary++);
    block.instructions.push_back(amir_binary(result, compound_operator(tokens[begin + 1].type), current, value));
    block.instructions.push_back(amir_store(tokens[begin].lexeme, result));
    return true;
}

class AmirBuilder {
public:
    AmirBuilder(const std::vector<Token>& tokens, std::string source_name) : tokens_(tokens) {
        module_.source_name = std::move(source_name);
    }

    AmirModule build() {
        AmirFunction main;
        main.name = "Main";
        main.return_type = "I32";
        main.blocks.push_back(AmirBlock{"Entry"});

        std::size_t index = 0;
        const std::size_t final_block = lower_range(main, 0, index, tokens_.size(), {});
        if (main.blocks.empty() || main.blocks[final_block].instructions.empty() || !is_terminal_instruction(main.blocks[final_block].instructions.back())) {
            main.blocks[final_block].instructions.push_back(amir_return("I32", "0"));
        }
        module_.functions.insert(module_.functions.begin(), std::move(main));
        validate_module();
        return module_;
    }

private:
    struct LoopTarget {
        TokenType kind;
        std::string continue_target;
        std::string exit_target;
    };

    std::string temp() {
        return "%t" + std::to_string(temporary_++);
    }

    std::string hidden_name(const std::string& prefix) {
        return "__fission_" + prefix + std::to_string(hidden_counter_++);
    }

    std::size_t add_block(AmirFunction& function, const std::string& prefix) {
        function.blocks.push_back(AmirBlock{prefix + std::to_string(block_counter_++)});
        return function.blocks.size() - 1;
    }

    AmirBlock& current_block(AmirFunction& function) {
        return function.blocks[current_block_];
    }

    AmirBlock& block(AmirFunction& function, std::size_t block_index) {
        return function.blocks[block_index];
    }

    void skip_newlines(std::size_t& index, std::size_t limit) const {
        while (index < limit && tokens_[index].type == TokenType::Newline) {
            ++index;
        }
    }

    bool at_terminator(std::size_t index, const std::vector<TokenType>& terminators) const {
        if (index >= tokens_.size()) {
            return true;
        }
        for (TokenType terminator : terminators) {
            if (tokens_[index].type == terminator) {
                return true;
            }
        }
        return false;
    }

    std::size_t after_line(std::size_t index, std::size_t limit) const {
        index = line_end(tokens_, index);
        if (index < limit && tokens_[index].type == TokenType::Newline) {
            ++index;
        }
        return index;
    }

    std::size_t logical_line_end(std::size_t index, std::size_t limit) const {
        int depth = 0;
        while (index < limit && tokens_[index].type != TokenType::End) {
            switch (tokens_[index].type) {
                case TokenType::LeftParen:
                case TokenType::LeftBracket:
                case TokenType::LeftBrace:
                    ++depth;
                    break;
                case TokenType::RightParen:
                case TokenType::RightBracket:
                case TokenType::RightBrace:
                    if (depth > 0) {
                        --depth;
                    }
                    break;
                case TokenType::Newline:
                    if (depth == 0) {
                        return index;
                    }
                    break;
                default:
                    break;
            }
            ++index;
        }
        return index;
    }

    std::size_t after_logical_line(std::size_t begin, std::size_t limit) const {
        std::size_t end = logical_line_end(begin, limit);
        if (end < limit && tokens_[end].type == TokenType::Newline) {
            ++end;
        }
        return end;
    }

    std::size_t find_line_token(std::size_t begin, std::size_t end, TokenType type) const {
        for (std::size_t index = begin; index < end; ++index) {
            if (tokens_[index].type == type) {
                return index;
            }
        }
        return end;
    }

    std::size_t find_end_pair(std::size_t body_begin, std::size_t limit, TokenType opener, TokenType closer) const {
        int depth = 0;
        for (std::size_t index = body_begin; index < limit;) {
            skip_newlines(index, limit);
            const std::size_t begin = is_line_number(tokens_, index) ? index + 1 : index;
            if (begin >= limit) {
                break;
            }
            if (tokens_[begin].type == opener) {
                ++depth;
            } else if (tokens_[begin].type == TokenType::EndKeyword && begin + 1 < limit && tokens_[begin + 1].type == closer) {
                if (depth == 0) {
                    return begin;
                }
                --depth;
            }
            index = after_line(begin, limit);
        }
        return limit;
    }

    std::size_t find_plain_terminator(std::size_t body_begin, std::size_t limit, TokenType opener, TokenType closer) const {
        int depth = 0;
        for (std::size_t index = body_begin; index < limit;) {
            skip_newlines(index, limit);
            const std::size_t begin = is_line_number(tokens_, index) ? index + 1 : index;
            if (begin >= limit) {
                break;
            }
            if (tokens_[begin].type == opener) {
                ++depth;
            } else if (tokens_[begin].type == closer) {
                if (depth == 0) {
                    return begin;
                }
                --depth;
            }
            index = after_line(begin, limit);
        }
        return limit;
    }

    std::size_t find_select_end(std::size_t body_begin, std::size_t limit) const {
        int depth = 0;
        for (std::size_t index = body_begin; index < limit;) {
            skip_newlines(index, limit);
            const std::size_t begin = is_line_number(tokens_, index) ? index + 1 : index;
            if (begin >= limit) {
                break;
            }
            if (tokens_[begin].type == TokenType::Select) {
                ++depth;
            } else if (tokens_[begin].type == TokenType::EndKeyword && begin + 1 < limit && tokens_[begin + 1].type == TokenType::Select) {
                if (depth == 0) {
                    return begin;
                }
                --depth;
            }
            index = after_line(begin, limit);
        }
        return limit;
    }

    std::vector<std::size_t> find_select_cases(std::size_t body_begin, std::size_t end_select, std::size_t limit) const {
        std::vector<std::size_t> cases;
        int depth = 0;
        for (std::size_t index = body_begin; index < end_select;) {
            skip_newlines(index, limit);
            const std::size_t begin = is_line_number(tokens_, index) ? index + 1 : index;
            if (begin >= end_select) {
                break;
            }
            if (tokens_[begin].type == TokenType::Select) {
                ++depth;
            } else if (tokens_[begin].type == TokenType::EndKeyword && begin + 1 < limit && tokens_[begin + 1].type == TokenType::Select) {
                if (depth > 0) {
                    --depth;
                }
            } else if (tokens_[begin].type == TokenType::Case && depth == 0) {
                cases.push_back(begin);
            }
            index = after_line(begin, limit);
        }
        return cases;
    }

    std::pair<std::size_t, std::size_t> find_if_parts(std::size_t body_begin, std::size_t limit) const {
        int depth = 0;
        std::size_t else_index = limit;
        for (std::size_t index = body_begin; index < limit;) {
            skip_newlines(index, limit);
            const std::size_t begin = is_line_number(tokens_, index) ? index + 1 : index;
            if (begin >= limit) {
                break;
            }
            if (tokens_[begin].type == TokenType::If) {
                ++depth;
            } else if (tokens_[begin].type == TokenType::Else && depth == 0) {
                else_index = begin;
            } else if (tokens_[begin].type == TokenType::EndKeyword && begin + 1 < limit && tokens_[begin + 1].type == TokenType::If) {
                if (depth == 0) {
                    return {else_index, begin};
                }
                --depth;
            }
            index = after_line(begin, limit);
        }
        return {else_index, limit};
    }

    std::string block_name(const AmirFunction& function, std::size_t block_index) const {
        return function.blocks[block_index].name;
    }

    std::size_t lower_range(AmirFunction& function, std::size_t initial_block, std::size_t& index, std::size_t limit, const std::vector<TokenType>& terminators) {
        const std::size_t saved_block = current_block_;
        current_block_ = initial_block;
        while (index < limit) {
            skip_newlines(index, limit);
            if (index >= limit || tokens_[index].type == TokenType::End || at_terminator(index, terminators)) {
                break;
            }
            lower_statement(function, index, limit);
            if (!function.blocks[current_block_].instructions.empty() && is_terminal_instruction(function.blocks[current_block_].instructions.back())) {
                skip_current_construct(index, limit);
            }
        }
        const std::size_t final_block = current_block_;
        current_block_ = saved_block;
        return final_block;
    }

    void skip_current_construct(std::size_t& index, std::size_t limit) const {
        while (index < limit && tokens_[index].type != TokenType::Newline && tokens_[index].type != TokenType::End) {
            ++index;
        }
        if (index < limit && tokens_[index].type == TokenType::Newline) {
            ++index;
        }
    }

    void lower_statement(AmirFunction& function, std::size_t& index, std::size_t limit) {
        const int source_line = tokens_[index].line;
        const bool has_line_label = is_line_number(tokens_, index);
        const std::size_t begin = has_line_label ? index + 1 : index;
        const std::size_t end = logical_line_end(begin, limit);
        if (begin >= end) {
            index = after_line(begin, limit);
            return;
        }

        if (has_line_label) {
            current_block(function).instructions.push_back(amir_label("L" + token_text(tokens_[index])));
        }
        current_block(function).instructions.push_back(amir_source(source_line));

        if (tokens_[begin].type == TokenType::If && lower_if(function, begin, end, index, limit)) {
            return;
        }
        if (tokens_[begin].type == TokenType::While && lower_while(function, begin, end, index, limit)) {
            return;
        }
        if (tokens_[begin].type == TokenType::Do && lower_do(function, begin, end, index, limit)) {
            return;
        }
        if (tokens_[begin].type == TokenType::For && lower_for(function, begin, end, index, limit)) {
            return;
        }
        if (tokens_[begin].type == TokenType::Select && lower_select(function, begin, end, index, limit)) {
            return;
        }
        if (tokens_[begin].type == TokenType::Try && lower_try(function, begin, end, index, limit)) {
            return;
        }
        if (tokens_[begin].type == TokenType::Function && lower_function(function, begin, end, index, limit)) {
            return;
        }
        if (tokens_[begin].type == TokenType::Class && lower_class_or_interface(function, true, begin, index, limit)) {
            return;
        }
        if (tokens_[begin].type == TokenType::Interface && lower_class_or_interface(function, false, begin, index, limit)) {
            return;
        }

        lower_simple_statement(function, begin, end);
        index = after_logical_line(begin, limit);
    }

    void lower_simple_statement(AmirFunction& function, std::size_t begin, std::size_t end) {
        AmirBlock& out = current_block(function);
        if (tokens_[begin].type == TokenType::Print) {
            const std::string value = lower_expression(out, tokens_, begin + 1, end, temporary_);
            out.instructions.push_back(amir_call("Runtime.Print", {value}));
        } else if (tokens_[begin].type == TokenType::Identifier && lower_compound_assignment(out, tokens_, begin, end, temporary_)) {
        } else if (tokens_[begin].type == TokenType::Identifier && lower_assignment(out, tokens_, begin, end, temporary_, false)) {
        } else if (tokens_[begin].type == TokenType::Let && lower_assignment(out, tokens_, begin + 1, end, temporary_, true)) {
        } else if (tokens_[begin].type == TokenType::Return) {
            if (begin + 1 < end) {
                const std::string value = lower_expression(out, tokens_, begin + 1, end, temporary_);
                // function.return_type is "VALUE" by default (untyped) and the declared type
                // name (e.g. "U64") when the function has an AS Type return clause -- see
                // parse_function_signature. Propagating it here (docs/systems/uefi-target.md
                // section 5 "function returns") is purely informational for the bytecode VM,
                // which only ever reads operands[1] regardless of this tag.
                out.instructions.push_back(amir_return(function.return_type, value));
            } else {
                out.instructions.push_back(amir_return("VALUE", "nothing"));
            }
        } else if (tokens_[begin].type == TokenType::Stop) {
            out.instructions.push_back(amir_return("I32", "0"));
        } else if (tokens_[begin].type == TokenType::Goto && begin + 1 < end && tokens_[begin + 1].type == TokenType::Number) {
            out.instructions.push_back(amir_jump("L" + token_text(tokens_[begin + 1])));
        } else if (lower_loop_control(out, begin, end)) {
        } else if ((tokens_[begin].type == TokenType::Identifier || tokens_[begin].type == TokenType::Run) && begin + 1 < end &&
                   tokens_[begin + 1].type == TokenType::LeftParen) {
            const std::size_t before = out.instructions.size();
            (void)lower_expression(out, tokens_, begin, end, temporary_);
            // Packet WP-004 "external or ABI-bound function calls": a dotted call whose
            // receiver is a parameter of the enclosing function (e.g. a UEFI protocol pointer
            // received as an argument) is a call through that value, not an ordinary namespaced
            // host/stdlib call -- reclassify the instruction lower_expression already built
            // rather than duplicating its call-argument parsing.
            if (out.instructions.size() > before) {
                AmirInstruction& produced = out.instructions.back();
                if (produced.kind == AmirInstruction::Kind::CallValue) {
                    const auto dot = produced.target.find('.');
                    const std::string receiver = dot == std::string::npos ? produced.target : produced.target.substr(0, dot);
                    if (function_has_parameter(function, receiver)) {
                        produced.kind = AmirInstruction::Kind::CallExternal;
                    }
                }
            }
        } else {
            out.instructions.push_back(amir_unsupported(statement_comment(tokens_, begin, end)));
        }
    }

    bool lower_loop_control(AmirBlock& out, std::size_t begin, std::size_t end) {
        if (begin + 1 >= end || !identifier_is(tokens_[begin], "EXIT") && !identifier_is(tokens_[begin], "CONTINUE")) {
            return false;
        }
        TokenType kind;
        if (tokens_[begin + 1].type == TokenType::For || tokens_[begin + 1].type == TokenType::While || tokens_[begin + 1].type == TokenType::Do) {
            kind = tokens_[begin + 1].type;
        } else {
            return false;
        }
        for (auto loop = loop_stack_.rbegin(); loop != loop_stack_.rend(); ++loop) {
            if (loop->kind == kind) {
                out.instructions.push_back(amir_jump(identifier_is(tokens_[begin], "EXIT") ? loop->exit_target : loop->continue_target));
                return true;
            }
        }
        out.instructions.push_back(amir_unsupported(statement_comment(tokens_, begin, end)));
        return true;
    }

    bool lower_if(AmirFunction& function, std::size_t begin, std::size_t line_end_index, std::size_t& index, std::size_t limit) {
        const std::size_t then_index = find_line_token(begin + 1, line_end_index, TokenType::Then);
        if (then_index >= line_end_index) {
            return false;
        }
        const std::size_t body_begin = after_line(begin, limit);
        const auto [else_index, end_if] = find_if_parts(body_begin, limit);
        if (end_if >= limit) {
            return false;
        }

        const std::size_t then_block = add_block(function, "IfThen");
        const std::size_t else_block = add_block(function, "IfElse");
        const std::size_t end_block = add_block(function, "IfEnd");
        const std::string condition = lower_expression(current_block(function), tokens_, begin + 1, then_index, temporary_);
        current_block(function).instructions.push_back(amir_branch(condition, block_name(function, then_block), block_name(function, else_block)));

        std::size_t then_cursor = body_begin;
        const std::size_t then_limit = else_index < end_if ? else_index : end_if;
        const std::size_t then_final = lower_range(function, then_block, then_cursor, then_limit, {});
        if (block(function, then_final).instructions.empty() || !is_terminal_instruction(block(function, then_final).instructions.back())) {
            block(function, then_final).instructions.push_back(amir_jump(block_name(function, end_block)));
        }

        std::size_t else_final = else_block;
        if (else_index < end_if) {
            std::size_t else_cursor = after_line(else_index, limit);
            else_final = lower_range(function, else_block, else_cursor, end_if, {});
        }
        if (block(function, else_final).instructions.empty() || !is_terminal_instruction(block(function, else_final).instructions.back())) {
            block(function, else_final).instructions.push_back(amir_jump(block_name(function, end_block)));
        }

        current_block_ = end_block;
        index = after_line(end_if, limit);
        return true;
    }

    bool lower_while(AmirFunction& function, std::size_t begin, std::size_t line_end_index, std::size_t& index, std::size_t limit) {
        const std::size_t body_begin = after_line(begin, limit);
        const std::size_t wend = find_plain_terminator(body_begin, limit, TokenType::While, TokenType::Wend);
        if (wend >= limit) {
            return false;
        }

        const std::size_t cond_block = add_block(function, "WhileCond");
        const std::size_t body_block = add_block(function, "WhileBody");
        const std::size_t end_block = add_block(function, "WhileEnd");
        current_block(function).instructions.push_back(amir_jump(block_name(function, cond_block)));

        current_block_ = cond_block;
        const std::string condition = lower_expression(current_block(function), tokens_, begin + 1, line_end_index, temporary_);
        current_block(function).instructions.push_back(amir_branch(condition, block_name(function, body_block), block_name(function, end_block)));

        std::size_t body_cursor = body_begin;
        loop_stack_.push_back(LoopTarget{TokenType::While, block_name(function, cond_block), block_name(function, end_block)});
        const std::size_t body_final = lower_range(function, body_block, body_cursor, wend, {});
        loop_stack_.pop_back();
        if (block(function, body_final).instructions.empty() || !is_terminal_instruction(block(function, body_final).instructions.back())) {
            block(function, body_final).instructions.push_back(amir_jump(block_name(function, cond_block)));
        }

        current_block_ = end_block;
        index = after_line(wend, limit);
        return true;
    }

    bool lower_do(AmirFunction& function, std::size_t begin, std::size_t line_end_index, std::size_t& index, std::size_t limit) {
        const std::size_t body_begin = after_line(begin, limit);
        const std::size_t loop_line = find_plain_terminator(body_begin, limit, TokenType::Do, TokenType::Loop);
        if (loop_line >= limit) {
            return false;
        }

        const bool has_pre_condition = begin + 1 < line_end_index && (tokens_[begin + 1].type == TokenType::While || tokens_[begin + 1].type == TokenType::Until);
        const bool pre_until = has_pre_condition && tokens_[begin + 1].type == TokenType::Until;
        const std::size_t body_block = add_block(function, "DoBody");
        const std::size_t end_block = add_block(function, "DoEnd");
        std::size_t cond_block = body_block;

        if (has_pre_condition) {
            cond_block = add_block(function, "DoCond");
            current_block(function).instructions.push_back(amir_jump(block_name(function, cond_block)));
            current_block_ = cond_block;
            const std::string condition = lower_expression(current_block(function), tokens_, begin + 2, line_end_index, temporary_);
            current_block(function).instructions.push_back(
                pre_until ? amir_branch(condition, block_name(function, end_block), block_name(function, body_block))
                          : amir_branch(condition, block_name(function, body_block), block_name(function, end_block)));
        } else {
            current_block(function).instructions.push_back(amir_jump(block_name(function, body_block)));
        }

        std::size_t body_cursor = body_begin;
        loop_stack_.push_back(LoopTarget{TokenType::Do, block_name(function, cond_block), block_name(function, end_block)});
        const std::size_t body_final = lower_range(function, body_block, body_cursor, loop_line, {});
        loop_stack_.pop_back();
        const std::size_t loop_end = line_end(tokens_, loop_line);
        if (loop_line + 1 < loop_end && (tokens_[loop_line + 1].type == TokenType::While || tokens_[loop_line + 1].type == TokenType::Until)) {
            const bool post_until = tokens_[loop_line + 1].type == TokenType::Until;
            current_block_ = body_final;
            const std::string condition = lower_expression(current_block(function), tokens_, loop_line + 2, loop_end, temporary_);
            current_block(function).instructions.push_back(
                post_until ? amir_branch(condition, block_name(function, end_block), block_name(function, body_block))
                           : amir_branch(condition, block_name(function, body_block), block_name(function, end_block)));
        } else if (block(function, body_final).instructions.empty() || !is_terminal_instruction(block(function, body_final).instructions.back())) {
            block(function, body_final).instructions.push_back(amir_jump(block_name(function, cond_block)));
        }

        current_block_ = end_block;
        index = after_line(loop_line, limit);
        return true;
    }

    bool lower_for(AmirFunction& function, std::size_t begin, std::size_t line_end_index, std::size_t& index, std::size_t limit) {
        if (begin + 1 >= line_end_index || !is_loop_variable_token(tokens_[begin + 1])) {
            return false;
        }
        const std::size_t body_begin = after_line(begin, limit);
        const std::size_t next_line = find_plain_terminator(body_begin, limit, TokenType::For, TokenType::Next);
        if (next_line >= limit) {
            return false;
        }

        const std::string loop_var = tokens_[begin + 1].lexeme;
        const std::size_t in_index = find_line_token(begin + 2, line_end_index, TokenType::In);
        if (in_index < line_end_index) {
            return lower_for_each(function, begin, line_end_index, index, limit, loop_var, in_index, body_begin, next_line);
        }

        const std::size_t equal_index = find_line_token(begin + 2, line_end_index, TokenType::Equal);
        const std::size_t to_index = find_line_token(begin + 2, line_end_index, TokenType::To);
        if (equal_index >= line_end_index || to_index >= line_end_index || to_index <= equal_index + 1) {
            return false;
        }
        const std::size_t step_index = find_line_token(to_index + 1, line_end_index, TokenType::Step);
        const std::string end_name = hidden_name("for_end");
        const std::string step_name = hidden_name("for_step");

        const std::string start = lower_expression(current_block(function), tokens_, equal_index + 1, to_index, temporary_);
        current_block(function).instructions.push_back(amir_store(loop_var, start));
        const std::string end_value = lower_expression(current_block(function), tokens_, to_index + 1, step_index < line_end_index ? step_index : line_end_index, temporary_);
        current_block(function).instructions.push_back(amir_store(end_name, end_value));
        if (step_index < line_end_index) {
            const std::string step_value = lower_expression(current_block(function), tokens_, step_index + 1, line_end_index, temporary_);
            current_block(function).instructions.push_back(amir_store(step_name, step_value));
        } else {
            const std::string one = temp();
            current_block(function).instructions.push_back(amir_const(one, "1"));
            current_block(function).instructions.push_back(amir_store(step_name, one));
        }

        const std::size_t cond_block = add_block(function, "ForCond");
        const std::size_t pos_cond_block = add_block(function, "ForCondPos");
        const std::size_t neg_cond_block = add_block(function, "ForCondNeg");
        const std::size_t body_block = add_block(function, "ForBody");
        const std::size_t inc_block = add_block(function, "ForInc");
        const std::size_t end_block = add_block(function, "ForEnd");
        current_block(function).instructions.push_back(amir_jump(block_name(function, cond_block)));

        current_block_ = cond_block;
        const std::string step_check_value = temp();
        const std::string zero_check_value = temp();
        const std::string positive_step = temp();
        current_block(function).instructions.push_back(amir_load(step_check_value, step_name));
        current_block(function).instructions.push_back(amir_const(zero_check_value, "0"));
        current_block(function).instructions.push_back(amir_binary(positive_step, ">=", step_check_value, zero_check_value));
        current_block(function).instructions.push_back(amir_branch(positive_step, block_name(function, pos_cond_block), block_name(function, neg_cond_block)));

        current_block_ = pos_cond_block;
        const std::string current = temp();
        const std::string limit_value = temp();
        const std::string condition = temp();
        current_block(function).instructions.push_back(amir_load(current, loop_var));
        current_block(function).instructions.push_back(amir_load(limit_value, end_name));
        current_block(function).instructions.push_back(amir_binary(condition, "<=", current, limit_value));
        current_block(function).instructions.push_back(amir_branch(condition, block_name(function, body_block), block_name(function, end_block)));

        current_block_ = neg_cond_block;
        const std::string neg_current = temp();
        const std::string neg_limit_value = temp();
        const std::string neg_condition = temp();
        current_block(function).instructions.push_back(amir_load(neg_current, loop_var));
        current_block(function).instructions.push_back(amir_load(neg_limit_value, end_name));
        current_block(function).instructions.push_back(amir_binary(neg_condition, ">=", neg_current, neg_limit_value));
        current_block(function).instructions.push_back(amir_branch(neg_condition, block_name(function, body_block), block_name(function, end_block)));

        std::size_t body_cursor = body_begin;
        loop_stack_.push_back(LoopTarget{TokenType::For, block_name(function, inc_block), block_name(function, end_block)});
        const std::size_t body_final = lower_range(function, body_block, body_cursor, next_line, {});
        loop_stack_.pop_back();
        if (block(function, body_final).instructions.empty() || !is_terminal_instruction(block(function, body_final).instructions.back())) {
            block(function, body_final).instructions.push_back(amir_jump(block_name(function, inc_block)));
        }

        current_block_ = inc_block;
        const std::string old_value = temp();
        const std::string step_value = temp();
        const std::string next_value = temp();
        current_block(function).instructions.push_back(amir_load(old_value, loop_var));
        current_block(function).instructions.push_back(amir_load(step_value, step_name));
        current_block(function).instructions.push_back(amir_binary(next_value, "+", old_value, step_value));
        current_block(function).instructions.push_back(amir_store(loop_var, next_value));
        current_block(function).instructions.push_back(amir_jump(block_name(function, cond_block)));

        current_block_ = end_block;
        index = after_line(next_line, limit);
        return true;
    }

    bool lower_for_each(AmirFunction& function, std::size_t, std::size_t line_end_index, std::size_t& index, std::size_t limit, const std::string& loop_var,
                        std::size_t in_index, std::size_t body_begin, std::size_t next_line) {
        const std::string items_name = hidden_name("each_items");
        const std::string index_name = hidden_name("each_index");
        const std::string iterable = lower_expression(current_block(function), tokens_, in_index + 1, line_end_index, temporary_);
        current_block(function).instructions.push_back(amir_store(items_name, iterable));
        const std::string zero = temp();
        current_block(function).instructions.push_back(amir_const(zero, "0"));
        current_block(function).instructions.push_back(amir_store(index_name, zero));

        const std::size_t cond_block = add_block(function, "ForEachCond");
        const std::size_t body_block = add_block(function, "ForEachBody");
        const std::size_t inc_block = add_block(function, "ForEachInc");
        const std::size_t end_block = add_block(function, "ForEachEnd");
        current_block(function).instructions.push_back(amir_jump(block_name(function, cond_block)));

        current_block_ = cond_block;
        const std::string i_value = temp();
        const std::string items = temp();
        const std::string length = temp();
        const std::string condition = temp();
        const std::string item = temp();
        current_block(function).instructions.push_back(amir_load(i_value, index_name));
        current_block(function).instructions.push_back(amir_load(items, items_name));
        current_block(function).instructions.push_back(amir_call_value(length, "LEN", {items}));
        current_block(function).instructions.push_back(amir_binary(condition, "<", i_value, length));
        current_block(function).instructions.push_back(amir_branch(condition, block_name(function, body_block), block_name(function, end_block)));

        current_block_ = body_block;
        current_block(function).instructions.push_back(amir_index(item, items, i_value));
        current_block(function).instructions.push_back(amir_store(loop_var, item));
        std::size_t body_cursor = body_begin;
        loop_stack_.push_back(LoopTarget{TokenType::For, block_name(function, inc_block), block_name(function, end_block)});
        const std::size_t body_final = lower_range(function, body_block, body_cursor, next_line, {});
        loop_stack_.pop_back();
        if (block(function, body_final).instructions.empty() || !is_terminal_instruction(block(function, body_final).instructions.back())) {
            block(function, body_final).instructions.push_back(amir_jump(block_name(function, inc_block)));
        }

        current_block_ = inc_block;
        const std::string old_index = temp();
        const std::string one = temp();
        const std::string next_index = temp();
        current_block(function).instructions.push_back(amir_load(old_index, index_name));
        current_block(function).instructions.push_back(amir_const(one, "1"));
        current_block(function).instructions.push_back(amir_binary(next_index, "+", old_index, one));
        current_block(function).instructions.push_back(amir_store(index_name, next_index));
        current_block(function).instructions.push_back(amir_jump(block_name(function, cond_block)));

        current_block_ = end_block;
        index = after_line(next_line, limit);
        return true;
    }

    bool lower_select(AmirFunction& function, std::size_t begin, std::size_t line_end_index, std::size_t& index, std::size_t limit) {
        if (begin + 1 >= line_end_index || tokens_[begin + 1].type != TokenType::Case) {
            return false;
        }
        const std::size_t body_begin = after_line(begin, limit);
        const std::size_t end_select = find_select_end(body_begin, limit);
        if (end_select >= limit) {
            return false;
        }
        const auto cases = find_select_cases(body_begin, end_select, limit);
        if (cases.empty()) {
            index = after_line(end_select, limit);
            return true;
        }

        const std::string target_name = hidden_name("select_value");
        const std::string select_value = lower_expression(current_block(function), tokens_, begin + 2, line_end_index, temporary_);
        current_block(function).instructions.push_back(amir_store(target_name, select_value));

        const std::size_t end_block = add_block(function, "SelectEnd");
        std::vector<std::size_t> test_blocks;
        std::vector<std::size_t> body_blocks;
        test_blocks.reserve(cases.size());
        body_blocks.reserve(cases.size());
        for (std::size_t i = 0; i < cases.size(); ++i) {
            test_blocks.push_back(add_block(function, "SelectCase"));
            body_blocks.push_back(add_block(function, "SelectBody"));
        }
        current_block(function).instructions.push_back(amir_jump(block_name(function, test_blocks.front())));

        for (std::size_t i = 0; i < cases.size(); ++i) {
            const std::size_t case_line = cases[i];
            const std::size_t case_line_end = line_end(tokens_, case_line);
            const std::size_t body_begin_for_case = after_line(case_line, limit);
            const std::size_t body_end = i + 1 < cases.size() ? cases[i + 1] : end_select;
            const std::string next_target = i + 1 < cases.size() ? block_name(function, test_blocks[i + 1]) : block_name(function, end_block);

            current_block_ = test_blocks[i];
            if (case_line + 1 < case_line_end && tokens_[case_line + 1].type == TokenType::Else) {
                current_block(function).instructions.push_back(amir_jump(block_name(function, body_blocks[i])));
            } else {
                const std::string condition = lower_case_match(function, target_name, case_line + 1, case_line_end);
                current_block(function).instructions.push_back(amir_branch(condition, block_name(function, body_blocks[i]), next_target));
            }

            std::size_t body_cursor = body_begin_for_case;
            const std::size_t body_final = lower_range(function, body_blocks[i], body_cursor, body_end, {});
            if (block(function, body_final).instructions.empty() || !is_terminal_instruction(block(function, body_final).instructions.back())) {
                block(function, body_final).instructions.push_back(amir_jump(block_name(function, end_block)));
            }
        }

        current_block_ = end_block;
        index = after_line(end_select, limit);
        return true;
    }

    std::string lower_case_match(AmirFunction& function, const std::string& target_name, std::size_t begin, std::size_t end) {
        std::vector<std::string> matches;
        std::size_t part_begin = begin;
        for (std::size_t cursor = begin; cursor <= end; ++cursor) {
            if (cursor == end || tokens_[cursor].type == TokenType::Comma) {
                const std::size_t to_index = find_line_token(part_begin, cursor, TokenType::To);
                const std::string target = temp();
                current_block(function).instructions.push_back(amir_load(target, target_name));
                if (to_index < cursor) {
                    const std::string start = lower_expression(current_block(function), tokens_, part_begin, to_index, temporary_);
                    const std::string stop = lower_expression(current_block(function), tokens_, to_index + 1, cursor, temporary_);
                    const std::string ge = temp();
                    const std::string le = temp();
                    const std::string both = temp();
                    current_block(function).instructions.push_back(amir_binary(ge, ">=", target, start));
                    current_block(function).instructions.push_back(amir_binary(le, "<=", target, stop));
                    current_block(function).instructions.push_back(amir_binary(both, "&&", ge, le));
                    matches.push_back(both);
                } else {
                    const std::string value = lower_expression(current_block(function), tokens_, part_begin, cursor, temporary_);
                    const std::string equal = temp();
                    current_block(function).instructions.push_back(amir_binary(equal, "==", target, value));
                    matches.push_back(equal);
                }
                part_begin = cursor + 1;
            }
        }
        if (matches.empty()) {
            const std::string false_value = temp();
            current_block(function).instructions.push_back(amir_const(false_value, "false"));
            return false_value;
        }
        std::string result = matches.front();
        for (std::size_t i = 1; i < matches.size(); ++i) {
            const std::string next = temp();
            current_block(function).instructions.push_back(amir_binary(next, "||", result, matches[i]));
            result = next;
        }
        return result;
    }

    bool lower_try(AmirFunction& function, std::size_t begin, std::size_t, std::size_t& index, std::size_t limit) {
        const std::size_t body_begin = after_line(begin, limit);
        const auto [catch_index, end_try] = find_try_parts(body_begin, limit);
        if (end_try >= limit) {
            return false;
        }

        const std::size_t catch_block = add_block(function, "Catch");
        const std::size_t end_block = add_block(function, "TryEnd");
        std::string error_name;
        if (catch_index < end_try) {
            const std::size_t catch_line_end = line_end(tokens_, catch_index);
            if (catch_index + 1 < catch_line_end && tokens_[catch_index + 1].type == TokenType::Identifier) {
                error_name = tokens_[catch_index + 1].lexeme;
            }
        }
        current_block(function).instructions.push_back(amir_try_begin(block_name(function, catch_block), error_name));

        std::size_t try_cursor = body_begin;
        const std::size_t try_limit = catch_index < end_try ? catch_index : end_try;
        const std::size_t try_final = lower_range(function, current_block_, try_cursor, try_limit, {});
        current_block_ = try_final;
        current_block(function).instructions.push_back(amir_try_end());
        current_block(function).instructions.push_back(amir_jump(block_name(function, end_block)));

        std::size_t catch_final = catch_block;
        if (catch_index < end_try) {
            std::size_t catch_cursor = after_line(catch_index, limit);
            catch_final = lower_range(function, catch_block, catch_cursor, end_try, {});
        }
        if (block(function, catch_final).instructions.empty() || !is_terminal_instruction(block(function, catch_final).instructions.back())) {
            block(function, catch_final).instructions.push_back(amir_jump(block_name(function, end_block)));
        }

        current_block_ = end_block;
        index = after_line(end_try, limit);
        return true;
    }

    std::pair<std::size_t, std::size_t> find_try_parts(std::size_t body_begin, std::size_t limit) const {
        int depth = 0;
        std::size_t catch_index = limit;
        for (std::size_t index = body_begin; index < limit;) {
            skip_newlines(index, limit);
            const std::size_t begin = is_line_number(tokens_, index) ? index + 1 : index;
            if (begin >= limit) {
                break;
            }
            if (tokens_[begin].type == TokenType::Try) {
                ++depth;
            } else if (tokens_[begin].type == TokenType::Catch && depth == 0) {
                catch_index = begin;
            } else if (tokens_[begin].type == TokenType::EndKeyword && begin + 1 < limit && tokens_[begin + 1].type == TokenType::Try) {
                if (depth == 0) {
                    return {catch_index, begin};
                }
                --depth;
            }
            index = after_line(begin, limit);
        }
        return {catch_index, limit};
    }

    bool lower_function(AmirFunction& owner, std::size_t begin, std::size_t line_end_index, std::size_t& index, std::size_t limit) {
        if (begin + 1 >= line_end_index || tokens_[begin + 1].type != TokenType::Identifier) {
            return false;
        }
        const std::size_t body_begin = after_line(begin, limit);
        const std::size_t end_function = find_end_pair(body_begin, limit, TokenType::Function, TokenType::Function);
        if (end_function >= limit) {
            return false;
        }

        AmirFunction function;
        function.name = tokens_[begin + 1].lexeme;
        function.return_type = "VALUE";
        parse_function_signature(function, begin + 2, line_end_index);
        std::vector<std::string> metadata;
        if (!function.params.empty()) {
            std::ostringstream params;
            for (std::size_t i = 0; i < function.params.size(); ++i) {
                if (i > 0) {
                    params << ',';
                }
                params << function.params[i];
            }
            metadata.push_back("params=" + params.str());
        }
        metadata.push_back("returns=" + function.return_type);
        current_block(owner).instructions.push_back(amir_declare_function(function.name, metadata));
        function.blocks.push_back(AmirBlock{"Entry"});
        const std::size_t saved_block = current_block_;
        current_block_ = 0;
        std::size_t body_cursor = body_begin;
        const std::size_t function_final = lower_range(function, 0, body_cursor, end_function, {});
        if (function.blocks[function_final].instructions.empty() || !is_terminal_instruction(function.blocks[function_final].instructions.back())) {
            function.blocks[function_final].instructions.push_back(amir_return("VALUE", "nothing"));
        }
        current_block_ = saved_block;
        module_.functions.push_back(std::move(function));
        index = after_line(end_function, limit);
        return true;
    }

    void parse_function_signature(AmirFunction& function, std::size_t begin, std::size_t end) const {
        std::size_t index = begin;
        if (index < end && tokens_[index].type == TokenType::LeftParen) {
            ++index;
            while (index < end && tokens_[index].type != TokenType::RightParen) {
                const std::size_t param_begin = index;
                while (index < end && tokens_[index].type != TokenType::Comma && tokens_[index].type != TokenType::RightParen) {
                    ++index;
                }
                function.params.push_back(join_expression(tokens_, param_begin, index));
                if (index < end && tokens_[index].type == TokenType::Comma) {
                    ++index;
                }
            }
        }
        for (; index < end; ++index) {
            if (tokens_[index].type == TokenType::As && index + 1 < end) {
                function.return_type = join_expression(tokens_, index + 1, end);
                break;
            }
        }
    }

    bool lower_class_or_interface(AmirFunction& function, bool is_class, std::size_t begin, std::size_t& index, std::size_t limit) {
        if (begin + 1 >= limit || tokens_[begin + 1].type != TokenType::Identifier) {
            return false;
        }
        const TokenType closer = is_class ? TokenType::Class : TokenType::Interface;
        const std::size_t body_begin = after_line(begin, limit);
        const std::size_t end_decl = find_end_pair(body_begin, limit, closer, closer);
        if (end_decl >= limit) {
            return false;
        }
        std::vector<std::string> metadata;
        const std::size_t header_end = line_end(tokens_, begin);
        if (begin + 2 < header_end) {
            metadata.push_back(join_expression(tokens_, begin + 2, header_end));
        }
        if (is_class) {
            current_block(function).instructions.push_back(amir_declare_class(tokens_[begin + 1].lexeme, std::move(metadata)));
            lower_class_methods(tokens_[begin + 1].lexeme, body_begin, end_decl);
        } else {
            current_block(function).instructions.push_back(amir_declare_interface(tokens_[begin + 1].lexeme, std::move(metadata)));
        }
        index = after_line(end_decl, limit);
        return true;
    }

    void lower_class_methods(const std::string& class_name, std::size_t body_begin, std::size_t end_decl) {
        for (std::size_t scan = body_begin; scan < end_decl;) {
            skip_newlines(scan, end_decl);
            std::size_t begin = is_line_number(tokens_, scan) ? scan + 1 : scan;
            if (begin >= end_decl) {
                break;
            }
            while (begin < end_decl && (tokens_[begin].type == TokenType::Shared || tokens_[begin].type == TokenType::Public ||
                                        tokens_[begin].type == TokenType::Private || tokens_[begin].type == TokenType::Protected)) {
                ++begin;
            }
            if (begin >= end_decl) {
                break;
            }
            const bool constructor = tokens_[begin].type == TokenType::Constructor;
            if (tokens_[begin].type != TokenType::Function && !constructor) {
                scan = after_line(begin, end_decl);
                continue;
            }
            const std::size_t line_end_index = line_end(tokens_, begin);
            if (!constructor && (begin + 1 >= line_end_index || tokens_[begin + 1].type != TokenType::Identifier)) {
                scan = after_line(begin, end_decl);
                continue;
            }
            const std::size_t method_body = after_line(begin, end_decl);
            const std::size_t method_end = constructor ? find_end_pair(method_body, end_decl, TokenType::Constructor, TokenType::Constructor)
                                                       : find_end_pair(method_body, end_decl, TokenType::Function, TokenType::Function);
            if (method_end >= end_decl) {
                scan = after_line(begin, end_decl);
                continue;
            }

            AmirFunction method;
            method.name = constructor ? class_name + ".__new" : class_name + "." + tokens_[begin + 1].lexeme;
            method.return_type = constructor ? "VALUE" : "VALUE";
            parse_function_signature(method, constructor ? begin + 1 : begin + 2, line_end_index);
            method.blocks.push_back(AmirBlock{"Entry"});

            const std::size_t saved_block = current_block_;
            auto saved_loop_stack = loop_stack_;
            loop_stack_.clear();
            current_block_ = 0;
            std::size_t body_cursor = method_body;
            const std::size_t method_final = lower_range(method, 0, body_cursor, method_end, {});
            if (method.blocks[method_final].instructions.empty() || !is_terminal_instruction(method.blocks[method_final].instructions.back())) {
                method.blocks[method_final].instructions.push_back(amir_return("VALUE", "nothing"));
            }
            current_block_ = saved_block;
            loop_stack_ = std::move(saved_loop_stack);
            module_.functions.push_back(std::move(method));
            scan = after_line(method_end, end_decl);
        }
    }

    bool contains_name(const std::vector<std::string>& names, const std::string& name) const {
        for (const auto& candidate : names) {
            if (candidate == name) {
                return true;
            }
        }
        return false;
    }

    void validate_module() {
        for (const auto& function : module_.functions) {
            std::vector<std::string> targets;
            for (const auto& block : function.blocks) {
                targets.push_back(block.name);
                for (const auto& instruction : block.instructions) {
                    if (instruction.kind == AmirInstruction::Kind::Label) {
                        targets.push_back(instruction.target);
                    }
                }
            }

            for (const auto& block : function.blocks) {
                if (block.instructions.empty()) {
                    module_.diagnostics.push_back("empty block " + function.name + "." + block.name);
                    continue;
                }
                if (!is_terminal_instruction(block.instructions.back())) {
                    module_.diagnostics.push_back("unterminated block " + function.name + "." + block.name);
                }

                for (const auto& instruction : block.instructions) {
                    if (instruction.kind == AmirInstruction::Kind::Unsupported && !instruction.operands.empty()) {
                        module_.diagnostics.push_back("unsupported lowering in " + function.name + "." + block.name + ": " + instruction.operands.front());
                    } else if (instruction.kind == AmirInstruction::Kind::Jump) {
                        validate_target(function.name, block.name, instruction.target, targets);
                    } else if (instruction.kind == AmirInstruction::Kind::Branch && instruction.operands.size() >= 3) {
                        validate_target(function.name, block.name, instruction.operands[1], targets);
                        validate_target(function.name, block.name, instruction.operands[2], targets);
                    } else if (instruction.kind == AmirInstruction::Kind::TryBegin) {
                        validate_target(function.name, block.name, instruction.target, targets);
                    }
                }
            }
        }
    }

    void validate_target(const std::string& function, const std::string& block, const std::string& target, const std::vector<std::string>& targets) {
        if (!contains_name(targets, target)) {
            module_.diagnostics.push_back("unresolved A-MIR target " + target + " from " + function + "." + block);
        }
    }

    AmirModule module_;
    const std::vector<Token>& tokens_;
    std::vector<LoopTarget> loop_stack_;
    int temporary_ = 0;
    int hidden_counter_ = 0;
    std::size_t block_counter_ = 0;
    std::size_t current_block_ = 0;
};

AmirModule build_amir(const std::vector<Token>& tokens, const std::string& source_name) {
    return AmirBuilder(tokens, source_name).build();
}

void render_instruction(std::ostream& out, const AmirInstruction& instruction, const std::string& source_name) {
    switch (instruction.kind) {
        case AmirInstruction::Kind::Label:
            out << instruction.target << ":\n";
            break;
        case AmirInstruction::Kind::Source:
            out << "    ; source " << source_name << ':' << instruction.source_line << "\n";
            break;
        case AmirInstruction::Kind::Eval:
            out << "    " << instruction.result << " := EVAL " << instruction.operands.front() << "\n";
            break;
        case AmirInstruction::Kind::Const:
            out << "    " << instruction.result << " := CONST " << instruction.operands.front() << "\n";
            break;
        case AmirInstruction::Kind::Load:
            out << "    " << instruction.result << " := LOAD " << instruction.target << "\n";
            break;
        case AmirInstruction::Kind::Unary:
            out << "    " << instruction.result << " := " << instruction.target << ' ' << instruction.operands.front() << "\n";
            break;
        case AmirInstruction::Kind::Binary:
            out << "    " << instruction.result << " := " << instruction.target << ' ' << instruction.operands[0] << ", " << instruction.operands[1]
                << "\n";
            break;
        case AmirInstruction::Kind::CallValue:
            out << "    " << instruction.result << " := CALL " << instruction.target;
            for (const auto& operand : instruction.operands) {
                out << ' ' << operand;
            }
            out << "\n";
            break;
        case AmirInstruction::Kind::CallExternal:
            out << "    " << instruction.result << " := CALL_EXTERNAL " << instruction.target;
            for (const auto& operand : instruction.operands) {
                out << ' ' << operand;
            }
            out << "\n";
            break;
        case AmirInstruction::Kind::Array:
            out << "    " << instruction.result << " := ARRAY";
            for (const auto& operand : instruction.operands) {
                out << ' ' << operand;
            }
            out << "\n";
            break;
        case AmirInstruction::Kind::Object:
            out << "    " << instruction.result << " := OBJECT";
            for (const auto& operand : instruction.operands) {
                out << ' ' << operand;
            }
            out << "\n";
            break;
        case AmirInstruction::Kind::Index:
            out << "    " << instruction.result << " := INDEX " << instruction.target << ", " << instruction.operands.front() << "\n";
            break;
        case AmirInstruction::Kind::Store:
            out << "    STORE " << instruction.target << ", " << instruction.operands.front() << "\n";
            break;
        case AmirInstruction::Kind::StoreIndex:
            out << "    STORE_INDEX " << instruction.target;
            for (const auto& operand : instruction.operands) {
                out << ' ' << operand;
            }
            out << "\n";
            break;
        case AmirInstruction::Kind::Call:
            out << "    CALL " << instruction.target;
            for (const auto& operand : instruction.operands) {
                out << ' ' << operand;
            }
            out << "\n";
            break;
        case AmirInstruction::Kind::Jump:
            out << "    JUMP " << instruction.target << "\n";
            break;
        case AmirInstruction::Kind::Branch:
            out << "    BRANCH " << instruction.operands[0] << ", " << instruction.operands[1] << ", " << instruction.operands[2] << "\n";
            break;
        case AmirInstruction::Kind::TryBegin:
            out << "    TRY_BEGIN " << instruction.target;
            if (!instruction.operands.empty() && !instruction.operands.front().empty()) {
                out << " AS " << instruction.operands.front();
            }
            out << "\n";
            break;
        case AmirInstruction::Kind::TryEnd:
            out << "    TRY_END\n";
            break;
        case AmirInstruction::Kind::DeclareFunction:
            out << "    DECLARE_FUNCTION " << instruction.target;
            for (const auto& operand : instruction.operands) {
                out << ' ' << operand;
            }
            out << "\n";
            break;
        case AmirInstruction::Kind::DeclareClass:
            out << "    DECLARE_CLASS " << instruction.target;
            for (const auto& operand : instruction.operands) {
                out << ' ' << operand;
            }
            out << "\n";
            break;
        case AmirInstruction::Kind::DeclareInterface:
            out << "    DECLARE_INTERFACE " << instruction.target;
            for (const auto& operand : instruction.operands) {
                out << ' ' << operand;
            }
            out << "\n";
            break;
        case AmirInstruction::Kind::Return:
            out << "    RETURN " << instruction.target << ' ' << instruction.operands.front() << "\n";
            break;
        case AmirInstruction::Kind::Unsupported:
            out << "    ; unsupported-lowering: " << instruction.operands.front() << "\n";
            break;
    }
}

std::string render_amir(const AmirModule& module) {
    std::ostringstream out;
    out << "A-MIR MODULE \"" << escaped(module.source_name) << "\"\n";
    out << "VERSION " << module.version << "\n\n";
    if (!module.diagnostics.empty()) {
        out << "DIAGNOSTICS " << module.diagnostics.size() << "\n";
        for (const auto& diagnostic : module.diagnostics) {
            out << "    " << diagnostic << "\n";
        }
        out << "\n";
    }
    for (const auto& function : module.functions) {
        out << "FUNCTION " << function.name;
        if (!function.params.empty()) {
            out << '(';
            for (std::size_t i = 0; i < function.params.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << function.params[i];
            }
            out << ')';
        }
        out << " RETURNS " << function.return_type << "\n\n";
        for (const auto& block : function.blocks) {
            out << "BLOCK " << block.name << "\n";
            for (const auto& instruction : block.instructions) {
                render_instruction(out, instruction, module.source_name);
            }
            out << "END BLOCK\n\n";
        }
        out << "END FUNCTION\n";
    }
    return out.str();
}

std::string bare_parameter_name(const std::string& declared_parameter) {
    const auto space = declared_parameter.find(' ');
    return space == std::string::npos ? declared_parameter : declared_parameter.substr(0, space);
}

void render_argument_location(std::ostream& out, const systems::ArgumentLocation& location) {
    if (location.in_register) {
        out << location.register_name;
    } else {
        out << "STACK+" << location.stack_offset_bytes;
    }
}

// Renders the Microsoft x64 calling convention (docs/systems/calling-conventions.md) computed
// for each declared function's parameters and for each external/ABI-bound call site within its
// body. This is a deterministic textual stand-in for real generated assembly (Packet WP-005
// verification: "golden tests for generated assembly or machine-code disassembly") -- actual
// instruction encoding is WP-008's job; this stage only answers "where does each argument live."
std::string render_calling_convention(const AmirModule& module) {
    std::ostringstream out;
    out << "CALLING CONVENTION MICROSOFT_X64\n";
    out << "SHADOW_SPACE " << systems::kShadowSpaceBytes << " bytes\n";
    out << "STACK_ALIGNMENT " << systems::kStackAlignmentAtCallBytes << " bytes at CALL\n\n";

    for (const auto& function : module.functions) {
        out << "FUNCTION " << function.name << "\n";
        if (!function.params.empty()) {
            out << "    PARAMETERS\n";
            const auto locations = systems::assign_argument_locations(static_cast<int>(function.params.size()));
            for (std::size_t i = 0; i < function.params.size(); ++i) {
                out << "        " << bare_parameter_name(function.params[i]) << " : ";
                render_argument_location(out, locations[i]);
                out << "\n";
            }
        }
        out << "    RETURNS " << systems::integer_return_register() << " (" << function.return_type << ")\n";

        bool printed_call_sites_header = false;
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if (instruction.kind != AmirInstruction::Kind::CallExternal) {
                    continue;
                }
                if (!printed_call_sites_header) {
                    out << "    CALL SITES\n";
                    printed_call_sites_header = true;
                }
                out << "        " << instruction.target << " (external)\n";
                const auto call_locations = systems::assign_argument_locations(static_cast<int>(instruction.operands.size()));
                for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                    out << "            ARG" << i << " : ";
                    render_argument_location(out, call_locations[i]);
                    out << "\n";
                }
            }
        }
        out << "END FUNCTION\n\n";
    }
    return out.str();
}

enum class BytecodeOp {
    Label = 0,
    Source = 1,
    Const = 2,
    Load = 3,
    Store = 4,
    StoreIndex = 5,
    Unary = 6,
    Binary = 7,
    CallValue = 8,
    CallRuntime = 9,
    Array = 10,
    Object = 11,
    Index = 12,
    Jump = 13,
    Branch = 14,
    TryBegin = 15,
    TryEnd = 16,
    DeclareFunction = 17,
    DeclareClass = 18,
    DeclareInterface = 19,
    Return = 20,
    CallExternal = 21,
    Unsupported = 22,
};

struct BytecodeInstruction {
    BytecodeOp op = BytecodeOp::Unsupported;
    std::vector<std::string> operands;
};

struct BytecodeBlock {
    std::string name;
    std::vector<BytecodeInstruction> instructions;
};

struct BytecodeFunction {
    std::string name;
    std::string return_type;
    std::vector<std::string> params;
    std::vector<std::string> locals;
    std::vector<BytecodeBlock> blocks;
};

struct BytecodeModule {
    std::string source_name;
    int version = 0;
    std::vector<std::string> constants;
    std::vector<BytecodeFunction> functions;
    std::vector<std::string> diagnostics;
};

std::string bytecode_op_name(BytecodeOp op) {
    switch (op) {
        case BytecodeOp::Label:
            return "LABEL";
        case BytecodeOp::Source:
            return "SOURCE";
        case BytecodeOp::Const:
            return "CONST";
        case BytecodeOp::Load:
            return "LOAD";
        case BytecodeOp::Store:
            return "STORE";
        case BytecodeOp::StoreIndex:
            return "STORE_INDEX";
        case BytecodeOp::Unary:
            return "UNARY";
        case BytecodeOp::Binary:
            return "BINARY";
        case BytecodeOp::CallValue:
            return "CALL_VALUE";
        case BytecodeOp::CallRuntime:
            return "CALL_RUNTIME";
        case BytecodeOp::Array:
            return "ARRAY";
        case BytecodeOp::Object:
            return "OBJECT";
        case BytecodeOp::Index:
            return "INDEX";
        case BytecodeOp::Jump:
            return "JUMP";
        case BytecodeOp::Branch:
            return "BRANCH";
        case BytecodeOp::TryBegin:
            return "TRY_BEGIN";
        case BytecodeOp::TryEnd:
            return "TRY_END";
        case BytecodeOp::DeclareFunction:
            return "DECLARE_FUNCTION";
        case BytecodeOp::DeclareClass:
            return "DECLARE_CLASS";
        case BytecodeOp::DeclareInterface:
            return "DECLARE_INTERFACE";
        case BytecodeOp::Return:
            return "RETURN";
        case BytecodeOp::CallExternal:
            return "CALL_EXTERNAL";
        case BytecodeOp::Unsupported:
            return "UNSUPPORTED";
    }
    return "UNSUPPORTED";
}

bool is_symbol_name(const std::string& text) {
    if (text.empty() || text[0] == '%' || text[0] == '"' || (text[0] >= '0' && text[0] <= '9')) {
        return false;
    }
    if (text == "true" || text == "false" || text == "nothing") {
        return false;
    }
    for (char c : text) {
        const bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!valid) {
            return false;
        }
    }
    return true;
}

std::size_t intern(std::vector<std::string>& values, const std::string& value) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (values[index] == value) {
            return index;
        }
    }
    values.push_back(value);
    return values.size() - 1;
}

std::string constant_ref(BytecodeModule& module, const std::string& value) {
    return "K" + std::to_string(intern(module.constants, value));
}

std::string local_ref(BytecodeFunction& function, const std::string& value) {
    return "L" + std::to_string(intern(function.locals, value));
}

BytecodeInstruction bytecode_instruction(BytecodeOp op, std::vector<std::string> operands) {
    BytecodeInstruction instruction;
    instruction.op = op;
    instruction.operands = std::move(operands);
    return instruction;
}

BytecodeModule build_bytecode(const AmirModule& amir) {
    BytecodeModule module;
    module.source_name = amir.source_name;
    module.version = 0;
    module.diagnostics = amir.diagnostics;

    for (const auto& amir_function : amir.functions) {
        BytecodeFunction function;
        function.name = amir_function.name;
        function.return_type = amir_function.return_type;
        function.params = amir_function.params;
        for (const auto& param : function.params) {
            (void)local_ref(function, param);
        }

        for (const auto& amir_block : amir_function.blocks) {
            BytecodeBlock block;
            block.name = amir_block.name;
            for (const auto& instruction : amir_block.instructions) {
                switch (instruction.kind) {
                    case AmirInstruction::Kind::Label:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Label, {instruction.target}));
                        break;
                    case AmirInstruction::Kind::Source:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Source, {std::to_string(instruction.source_line)}));
                        break;
                    case AmirInstruction::Kind::Eval:
                        module.diagnostics.push_back("bytecode fallback EVAL retained: " + instruction.operands.front());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Unsupported, {instruction.result, instruction.operands.front()}));
                        break;
                    case AmirInstruction::Kind::Const:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Const, {instruction.result, constant_ref(module, instruction.operands.front())}));
                        break;
                    case AmirInstruction::Kind::Load:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Load, {instruction.result, local_ref(function, instruction.target)}));
                        break;
                    case AmirInstruction::Kind::Unary:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Unary, {instruction.result, instruction.target, instruction.operands.front()}));
                        break;
                    case AmirInstruction::Kind::Binary:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Binary, {instruction.result, instruction.target, instruction.operands[0], instruction.operands[1]}));
                        break;
                    case AmirInstruction::Kind::CallValue: {
                        std::vector<std::string> operands{instruction.result, instruction.target};
                        operands.insert(operands.end(), instruction.operands.begin(), instruction.operands.end());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::CallValue, std::move(operands)));
                        break;
                    }
                    case AmirInstruction::Kind::CallExternal: {
                        // No hosted-runtime binding exists yet for external/ABI-bound calls
                        // (that is WP-005/006/008's job); retained as bytecode so it surfaces
                        // execute_bytecode's "not implemented yet" diagnostic instead of being
                        // silently misrepresented as an ordinary host call.
                        std::vector<std::string> operands{instruction.result, instruction.target};
                        operands.insert(operands.end(), instruction.operands.begin(), instruction.operands.end());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::CallExternal, std::move(operands)));
                        break;
                    }
                    case AmirInstruction::Kind::Array: {
                        std::vector<std::string> operands{instruction.result};
                        operands.insert(operands.end(), instruction.operands.begin(), instruction.operands.end());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Array, std::move(operands)));
                        break;
                    }
                    case AmirInstruction::Kind::Object: {
                        std::vector<std::string> operands{instruction.result};
                        operands.insert(operands.end(), instruction.operands.begin(), instruction.operands.end());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Object, std::move(operands)));
                        break;
                    }
                    case AmirInstruction::Kind::Index:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Index, {instruction.result, instruction.target, instruction.operands.front()}));
                        break;
                    case AmirInstruction::Kind::Store:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Store, {local_ref(function, instruction.target), instruction.operands.front()}));
                        break;
                    case AmirInstruction::Kind::StoreIndex: {
                        std::vector<std::string> operands{local_ref(function, instruction.target)};
                        operands.insert(operands.end(), instruction.operands.begin(), instruction.operands.end());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::StoreIndex, std::move(operands)));
                        break;
                    }
                    case AmirInstruction::Kind::Call: {
                        std::vector<std::string> operands{instruction.target};
                        operands.insert(operands.end(), instruction.operands.begin(), instruction.operands.end());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::CallRuntime, std::move(operands)));
                        break;
                    }
                    case AmirInstruction::Kind::Jump:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Jump, {instruction.target}));
                        break;
                    case AmirInstruction::Kind::Branch:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Branch, instruction.operands));
                        break;
                    case AmirInstruction::Kind::TryBegin:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::TryBegin, {instruction.target, instruction.operands.empty() ? "" : instruction.operands.front()}));
                        break;
                    case AmirInstruction::Kind::TryEnd:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::TryEnd, {}));
                        break;
                    case AmirInstruction::Kind::DeclareFunction: {
                        std::vector<std::string> operands{instruction.target};
                        operands.insert(operands.end(), instruction.operands.begin(), instruction.operands.end());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::DeclareFunction, std::move(operands)));
                        break;
                    }
                    case AmirInstruction::Kind::DeclareClass: {
                        std::vector<std::string> operands{instruction.target};
                        operands.insert(operands.end(), instruction.operands.begin(), instruction.operands.end());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::DeclareClass, std::move(operands)));
                        break;
                    }
                    case AmirInstruction::Kind::DeclareInterface: {
                        std::vector<std::string> operands{instruction.target};
                        operands.insert(operands.end(), instruction.operands.begin(), instruction.operands.end());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::DeclareInterface, std::move(operands)));
                        break;
                    }
                    case AmirInstruction::Kind::Return:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Return, {instruction.target, instruction.operands.front()}));
                        break;
                    case AmirInstruction::Kind::Unsupported:
                        module.diagnostics.push_back("bytecode unsupported lowering retained: " + instruction.operands.front());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Unsupported, instruction.operands));
                        break;
                }
            }
            function.blocks.push_back(std::move(block));
        }
        module.functions.push_back(std::move(function));
    }
    return module;
}

std::string render_bytecode(const BytecodeModule& module) {
    std::ostringstream out;
    out << "ARCOFISSION BYTECODE\n";
    out << "FORMAT .arcof-text\n";
    out << "VERSION " << module.version << "\n";
    out << "SOURCE \"" << escaped(module.source_name) << "\"\n\n";

    out << "OPCODES\n";
    for (int id = static_cast<int>(BytecodeOp::Label); id <= static_cast<int>(BytecodeOp::Unsupported); ++id) {
        const auto op = static_cast<BytecodeOp>(id);
        out << "    " << id << " " << bytecode_op_name(op) << "\n";
    }
    out << "END OPCODES\n\n";

    if (!module.diagnostics.empty()) {
        out << "DIAGNOSTICS " << module.diagnostics.size() << "\n";
        for (const auto& diagnostic : module.diagnostics) {
            out << "    " << diagnostic << "\n";
        }
        out << "END DIAGNOSTICS\n\n";
    }

    out << "CONSTANTS " << module.constants.size() << "\n";
    for (std::size_t i = 0; i < module.constants.size(); ++i) {
        out << "    K" << i << " " << module.constants[i] << "\n";
    }
    out << "END CONSTANTS\n\n";

    for (const auto& function : module.functions) {
        out << "FUNCTION " << function.name << " RETURNS " << function.return_type << "\n";
        out << "PARAMS " << function.params.size() << "\n";
        for (std::size_t i = 0; i < function.params.size(); ++i) {
            out << "    P" << i << " " << function.params[i] << "\n";
        }
        out << "LOCALS " << function.locals.size() << "\n";
        for (std::size_t i = 0; i < function.locals.size(); ++i) {
            out << "    L" << i << " " << function.locals[i] << "\n";
        }
        for (const auto& block : function.blocks) {
            out << "BLOCK " << block.name << "\n";
            for (const auto& instruction : block.instructions) {
                out << "    " << static_cast<int>(instruction.op) << " " << bytecode_op_name(instruction.op);
                for (const auto& operand : instruction.operands) {
                    out << ' ' << operand;
                }
                out << "\n";
            }
            out << "END BLOCK\n";
        }
        out << "END FUNCTION\n\n";
    }

    return out.str();
}

std::string trim(std::string text) {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

std::vector<std::string> split_words(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> words;
    std::string word;
    while (input >> word) {
        words.push_back(word);
    }
    return words;
}

BytecodeOp bytecode_op_from_name(const std::string& name) {
    for (int id = static_cast<int>(BytecodeOp::Label); id <= static_cast<int>(BytecodeOp::Unsupported); ++id) {
        const auto op = static_cast<BytecodeOp>(id);
        if (bytecode_op_name(op) == name) {
            return op;
        }
    }
    throw std::runtime_error("unknown bytecode opcode: " + name);
}

BytecodeModule parse_bytecode(const std::string& text) {
    std::istringstream input(text);
    std::string line;
    BytecodeModule module;
    BytecodeFunction* function = nullptr;
    BytecodeBlock* block = nullptr;

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line == "ARCOFISSION BYTECODE" || line == "FORMAT .arcof-text" || line == "OPCODES" ||
            line == "END OPCODES" || line == "END CONSTANTS" || line == "END DIAGNOSTICS") {
            continue;
        }

        if (line.rfind("VERSION ", 0) == 0) {
            module.version = std::stoi(line.substr(8));
        } else if (line.rfind("SOURCE ", 0) == 0) {
            module.source_name = line.substr(7);
        } else if (line.rfind("DIAGNOSTICS ", 0) == 0) {
            continue;
        } else if (line.rfind("CONSTANTS ", 0) == 0) {
            continue;
        } else if (line.rfind("FUNCTION ", 0) == 0) {
            const auto words = split_words(line);
            if (words.size() < 4 || words[2] != "RETURNS") {
                throw std::runtime_error("invalid bytecode FUNCTION line: " + line);
            }
            module.functions.push_back(BytecodeFunction{words[1], words[3]});
            function = &module.functions.back();
            block = nullptr;
        } else if (line == "END FUNCTION") {
            function = nullptr;
            block = nullptr;
        } else if (line.rfind("PARAMS ", 0) == 0) {
            continue;
        } else if (line.rfind("P", 0) == 0 && function) {
            const auto split = line.find(' ');
            if (split != std::string::npos) {
                function->params.push_back(line.substr(split + 1));
            }
        } else if (line.rfind("LOCALS ", 0) == 0) {
            continue;
        } else if (line.rfind("L", 0) == 0 && function && !block) {
            const auto split = line.find(' ');
            if (split != std::string::npos) {
                function->locals.push_back(line.substr(split + 1));
            }
        } else if (line.rfind("K", 0) == 0 && !function) {
            const auto split = line.find(' ');
            if (split != std::string::npos) {
                module.constants.push_back(line.substr(split + 1));
            }
        } else if (line.rfind("BLOCK ", 0) == 0 && function) {
            function->blocks.push_back(BytecodeBlock{line.substr(6)});
            block = &function->blocks.back();
        } else if (line == "END BLOCK") {
            block = nullptr;
        } else if (block) {
            const auto words = split_words(line);
            if (words.size() < 2) {
                throw std::runtime_error("invalid bytecode instruction: " + line);
            }
            std::vector<std::string> operands;
            for (std::size_t i = 2; i < words.size(); ++i) {
                operands.push_back(words[i]);
            }
            block->instructions.push_back(bytecode_instruction(bytecode_op_from_name(words[1]), std::move(operands)));
        }
    }
    return module;
}

std::string unquote_constant(const std::string& text) {
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') {
        return text;
    }
    std::string out;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size() - 1) {
            const char next = text[++i];
            switch (next) {
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    out.push_back(next);
                    break;
            }
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

std::string declared_parameter_type(const std::string& declared_parameter) {
    const auto as_pos = declared_parameter.find(" AS ");
    return as_pos == std::string::npos ? "" : declared_parameter.substr(as_pos + 4);
}

struct X86_64CodegenResult {
    bool ok = true;
    std::string error;
    systems::x86_64::Assembler text;
    std::vector<std::uint8_t> rdata;
    struct DataRelocation {
        std::size_t disp_field_offset;
        std::size_t instruction_end_offset;
        std::size_t rdata_offset;
    };
    std::vector<DataRelocation> relocations;
    std::string entry_symbol;
};

// Generates x86-64 machine code for a single named function within `module` (Packet WP-008,
// docs/systems/x86-64-codegen.md). Deliberately narrow: supports exactly the A-MIR instruction
// kinds the milestone's hello-world program uses (CONST, LOAD, CALL_EXTERNAL, RETURN, plus the
// non-semantic SOURCE/LABEL markers, skipped) with a uniform spill-everything strategy (Packet
// non-goal: "register allocator sophistication beyond correctness" -- every named value gets its
// own stack slot, always reloaded before use, never kept live in a register across instructions).
// Any other instruction kind, or any construct this milestone's UEFI bindings/calling convention
// do not cover, produces a clear error rather than an incorrect or silently wrong encoding.
X86_64CodegenResult generate_x86_64_function(const AmirModule& module, const std::string& function_name) {
    X86_64CodegenResult result;
    result.entry_symbol = function_name;

    const AmirFunction* target = nullptr;
    for (const auto& function : module.functions) {
        if (function.name == function_name) {
            // Last match wins: the synthetic top-level wrapper is always named "Main" and is
            // always emitted first, so a real user-declared function with the same name (as in
            // the hello-world example) is always found after it.
            target = &function;
        }
    }
    if (!target) {
        result.ok = false;
        result.error = "no function named \"" + function_name + "\" was found";
        return result;
    }
    if (target->blocks.size() != 1) {
        result.ok = false;
        result.error = "function \"" + function_name + "\" has control flow beyond a single "
            "straight-line block, which this milestone's code generator does not support";
        return result;
    }

    std::vector<std::string> slot_names;
    std::unordered_map<std::string, int> slot_offsets;
    const auto add_slot = [&](const std::string& name) {
        if (name.empty() || slot_offsets.count(name) != 0) {
            return;
        }
        slot_offsets[name] = 0;
        slot_names.push_back(name);
    };
    for (const auto& declared_parameter : target->params) {
        add_slot(bare_parameter_name(declared_parameter));
    }
    for (const auto& instruction : target->blocks.front().instructions) {
        add_slot(instruction.result);
    }

    const int shadow = systems::kShadowSpaceBytes;
    int frame_size = shadow + 8 * static_cast<int>(slot_names.size());
    // RSP is kEntryRspMod16 (8) mod 16 at function entry; after `sub rsp, frame_size`, RSP must
    // be 0 mod 16 immediately before any CALL this function makes, which requires
    // frame_size % 16 == kEntryRspMod16.
    while (frame_size % 16 != systems::kEntryRspMod16) {
        ++frame_size;
    }
    if (frame_size > 255) {
        result.ok = false;
        result.error = "function \"" + function_name + "\" needs a stack frame larger than this "
            "milestone's 8-bit immediate prologue/epilogue encoding supports";
        return result;
    }
    for (std::size_t i = 0; i < slot_names.size(); ++i) {
        slot_offsets[slot_names[i]] = shadow + 8 * static_cast<int>(i);
    }
    const auto slot_of = [&](const std::string& name) -> int {
        const auto found = slot_offsets.find(name);
        return found == slot_offsets.end() ? -1 : found->second;
    };

    static const std::unordered_map<std::string, systems::x86_64::Reg> kRegisterByName = {
        {"RCX", systems::x86_64::Reg::RCX}, {"RDX", systems::x86_64::Reg::RDX},
        {"R8", systems::x86_64::Reg::R8}, {"R9", systems::x86_64::Reg::R9},
    };

    using Reg = systems::x86_64::Reg;
    result.text.sub_rsp_imm8(static_cast<std::uint8_t>(frame_size));

    // Spill incoming register arguments (Packet WP-008 non-goal: general-purpose instruction
    // selection -- only register-passed parameters are handled; a 5th+ stack-passed parameter is
    // outside this milestone's hello-world shape and produces a clear error rather than silently
    // mishandled code).
    {
        const auto locations = systems::assign_argument_locations(static_cast<int>(target->params.size()));
        for (std::size_t i = 0; i < target->params.size(); ++i) {
            const std::string name = bare_parameter_name(target->params[i]);
            if (!locations[i].in_register) {
                result.ok = false;
                result.error = "function \"" + function_name + "\" parameter \"" + name +
                    "\" is passed on the stack, which this milestone's code generator does not support";
                return result;
            }
            result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(slot_of(name)),
                                         kRegisterByName.at(locations[i].register_name));
        }
    }

    for (const auto& instruction : target->blocks.front().instructions) {
        switch (instruction.kind) {
            case AmirInstruction::Kind::Source:
            case AmirInstruction::Kind::Label:
                break;

            case AmirInstruction::Kind::Const: {
                const std::string& text_operand = instruction.operands.front();
                if (!text_operand.empty() && text_operand.front() == '"') {
                    std::vector<char16_t> encoded;
                    try {
                        encoded = systems::encode_utf16_null_terminated(unquote_constant(text_operand));
                    } catch (const std::exception& error) {
                        result.ok = false;
                        result.error = std::string("string constant cannot be encoded as UTF-16: ") + error.what();
                        return result;
                    }
                    const std::size_t data_offset = result.rdata.size();
                    for (char16_t unit : encoded) {
                        result.rdata.push_back(static_cast<std::uint8_t>(unit & 0xFF));
                        result.rdata.push_back(static_cast<std::uint8_t>((unit >> 8) & 0xFF));
                    }
                    const std::size_t disp_offset = result.text.lea_rip_relative(Reg::RAX);
                    result.relocations.push_back({disp_offset, result.text.size(), data_offset});
                    result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(slot_of(instruction.result)), Reg::RAX);
                } else {
                    std::uint64_t value = 0;
                    try {
                        std::size_t consumed = 0;
                        const long long parsed = std::stoll(text_operand, &consumed, 10);
                        if (consumed != text_operand.size()) {
                            throw std::invalid_argument("trailing characters");
                        }
                        value = static_cast<std::uint64_t>(parsed);
                    } catch (const std::exception&) {
                        result.ok = false;
                        result.error = "numeric constant \"" + text_operand + "\" is not an exact "
                            "integer literal, which is all this milestone's code generator supports";
                        return result;
                    }
                    result.text.mov_reg_imm64(Reg::RAX, value);
                    result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(slot_of(instruction.result)), Reg::RAX);
                }
                break;
            }

            case AmirInstruction::Kind::Load: {
                const int source_slot = slot_of(instruction.target);
                if (source_slot < 0) {
                    result.ok = false;
                    result.error = "LOAD of \"" + instruction.target + "\" has no assigned stack slot";
                    return result;
                }
                result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(source_slot));
                result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(slot_of(instruction.result)), Reg::RAX);
                break;
            }

            case AmirInstruction::Kind::CallExternal: {
                const auto dot = instruction.target.find('.');
                if (dot == std::string::npos) {
                    result.ok = false;
                    result.error = "external call target \"" + instruction.target + "\" is not a dotted field chain";
                    return result;
                }
                const std::string receiver = instruction.target.substr(0, dot);
                std::string receiver_type;
                for (const auto& declared_parameter : target->params) {
                    if (bare_parameter_name(declared_parameter) == receiver) {
                        receiver_type = declared_parameter_type(declared_parameter);
                        break;
                    }
                }
                auto current_type = systems::lookup_uefi_type(receiver_type);
                if (!current_type) {
                    result.ok = false;
                    result.error = "external call receiver \"" + receiver + "\" does not have a known UEFI binding type";
                    return result;
                }

                const int receiver_slot = slot_of(receiver);
                if (receiver_slot < 0) {
                    result.ok = false;
                    result.error = "external call receiver \"" + receiver + "\" has no assigned stack slot";
                    return result;
                }
                result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(receiver_slot));

                std::string remaining = instruction.target.substr(dot + 1);
                const systems::UefiField* final_field = nullptr;
                while (true) {
                    const auto next_dot = remaining.find('.');
                    const std::string segment = next_dot == std::string::npos ? remaining : remaining.substr(0, next_dot);
                    const systems::UefiField* field = current_type->find_field(segment);
                    if (!field) {
                        result.ok = false;
                        result.error = "external call field \"" + segment + "\" is not bound on " + current_type->name;
                        return result;
                    }
                    if (next_dot == std::string::npos) {
                        final_field = field;
                        break;
                    }
                    result.text.mov_load_disp8(Reg::RAX, Reg::RAX, static_cast<std::uint8_t>(field->offset_bytes));
                    const std::string next_type_name = field->result_type;
                    current_type = systems::lookup_uefi_type(next_type_name);
                    if (!current_type) {
                        result.ok = false;
                        result.error = "external call field \"" + segment + "\" does not resolve to a chainable systems type";
                        return result;
                    }
                    remaining = remaining.substr(next_dot + 1);
                }
                if (!final_field || !final_field->is_method) {
                    result.ok = false;
                    result.error = "external call target \"" + instruction.target + "\" does not resolve to a bound method";
                    return result;
                }

                // RAX now holds the resolved "This" pointer (docs/systems/uefi-bindings.md: the
                // implicit first argument real UEFI protocol methods take in the underlying C ABI).
                const int explicit_arg_count = static_cast<int>(instruction.operands.size());
                const auto locations = systems::assign_argument_locations(
                    explicit_arg_count + (final_field->implicit_this_argument ? 1 : 0));
                std::size_t location_index = 0;
                if (final_field->implicit_this_argument) {
                    result.text.mov_reg_reg(kRegisterByName.at(locations[location_index].register_name), Reg::RAX);
                    ++location_index;
                }
                for (int i = 0; i < explicit_arg_count; ++i, ++location_index) {
                    if (!locations[location_index].in_register) {
                        result.ok = false;
                        result.error = "external call \"" + instruction.target + "\" has more arguments than "
                            "this milestone's code generator supports in registers";
                        return result;
                    }
                    const int argument_slot = slot_of(instruction.operands[static_cast<std::size_t>(i)]);
                    if (argument_slot < 0) {
                        result.ok = false;
                        result.error = "external call argument \"" + instruction.operands[static_cast<std::size_t>(i)] +
                            "\" has no assigned stack slot";
                        return result;
                    }
                    result.text.mov_load_disp8(kRegisterByName.at(locations[location_index].register_name), Reg::RSP,
                                                static_cast<std::uint8_t>(argument_slot));
                }

                result.text.call_indirect_disp8(Reg::RAX, static_cast<std::uint8_t>(final_field->offset_bytes));
                result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(slot_of(instruction.result)), Reg::RAX);
                break;
            }

            case AmirInstruction::Kind::Return: {
                const std::string& value_ref = instruction.operands.front();
                if (value_ref != "nothing") {
                    const int value_slot = slot_of(value_ref);
                    if (value_slot < 0) {
                        result.ok = false;
                        result.error = "RETURN of \"" + value_ref + "\" has no assigned stack slot";
                        return result;
                    }
                    result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(value_slot));
                }
                result.text.add_rsp_imm8(static_cast<std::uint8_t>(frame_size));
                result.text.ret();
                break;
            }

            default:
                result.ok = false;
                result.error = "this milestone's code generator does not support this A-MIR instruction kind";
                return result;
        }
    }

    return result;
}

std::string render_x86_64(const X86_64CodegenResult& codegen) {
    std::ostringstream out;
    out << "X86_64 MICROSOFT_X64\n";
    out << "ENTRY " << codegen.entry_symbol << "\n\n";
    out << "TEXT " << codegen.text.size() << " bytes\n";
    const auto& bytes = codegen.text.bytes();
    for (std::size_t i = 0; i < bytes.size(); i += 8) {
        std::ostringstream line;
        line << "    " << std::hex << std::setw(4) << std::setfill('0') << i << ":";
        for (std::size_t j = i; j < bytes.size() && j < i + 8; ++j) {
            line << ' ' << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[j]);
        }
        out << line.str() << std::dec << "\n";
    }
    out << "\nRDATA " << codegen.rdata.size() << " bytes\n";
    for (std::size_t i = 0; i < codegen.rdata.size(); i += 8) {
        std::ostringstream line;
        line << "    " << std::hex << std::setw(4) << std::setfill('0') << i << ":";
        for (std::size_t j = i; j < codegen.rdata.size() && j < i + 8; ++j) {
            line << ' ' << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(codegen.rdata[j]);
        }
        out << line.str() << std::dec << "\n";
    }
    out << "\nRELOCATIONS " << codegen.relocations.size() << "\n";
    for (const auto& relocation : codegen.relocations) {
        out << "    TEXT+" << std::hex << relocation.disp_field_offset << " RIP_REL32_TO RDATA+"
            << relocation.rdata_offset << " (instruction ends at TEXT+" << relocation.instruction_end_offset
            << ")" << std::dec << "\n";
    }
    return out.str();
}

Value parse_constant_value(const std::string& text) {
    if (text == "nothing") {
        return Value();
    }
    if (text == "true") {
        return true;
    }
    if (text == "false") {
        return false;
    }
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        return unquote_constant(text);
    }
    return std::stod(text);
}

long long value_to_int(const Value& value) {
    return static_cast<long long>(value.as_number());
}

Value eval_unary(const std::string& op, const Value& value) {
    if (op == "-") {
        return -value.as_number();
    }
    if (op == "!") {
        return !value.truthy();
    }
    if (op == "~" || op == "NOT") {
        return static_cast<double>(~value_to_int(value));
    }
    throw std::runtime_error("unsupported bytecode unary operator: " + op);
}

Value eval_binary(const std::string& op, const Value& left, const Value& right) {
    if (op == "+") {
        if (left.is_string() || right.is_string()) {
            return left.to_string() + right.to_string();
        }
        return left.as_number() + right.as_number();
    }
    if (op == "-") {
        return left.as_number() - right.as_number();
    }
    if (op == "*") {
        return left.as_number() * right.as_number();
    }
    if (op == "/") {
        return left.as_number() / right.as_number();
    }
    if (op == "MOD") {
        const double divisor = right.as_number();
        if (divisor == 0.0) {
            throw std::runtime_error("MOD divisor cannot be zero");
        }
        return std::fmod(left.as_number(), divisor);
    }
    if (op == "==" || op == "=") {
        return values_equal(left, right);
    }
    if (op == "!=") {
        return !values_equal(left, right);
    }
    if (op == "<") {
        return left.as_number() < right.as_number();
    }
    if (op == "<=") {
        return left.as_number() <= right.as_number();
    }
    if (op == ">") {
        return left.as_number() > right.as_number();
    }
    if (op == ">=") {
        return left.as_number() >= right.as_number();
    }
    if (op == "&") {
        return static_cast<double>(value_to_int(left) & value_to_int(right));
    }
    if (op == "|") {
        return static_cast<double>(value_to_int(left) | value_to_int(right));
    }
    if (op == "^") {
        return static_cast<double>(value_to_int(left) ^ value_to_int(right));
    }
    if (op == "<<") {
        return static_cast<double>(value_to_int(left) << value_to_int(right));
    }
    if (op == ">>") {
        return static_cast<double>(value_to_int(left) >> value_to_int(right));
    }
    if (op == "&&" || op == "ANDALSO") {
        return left.truthy() && right.truthy();
    }
    if (op == "||" || op == "ORELSE") {
        return left.truthy() || right.truthy();
    }
    if (op == "CONTAINS") {
        if (left.is_array()) {
            for (const auto& item : left.as_array()) {
                if (values_equal(item, right)) {
                    return true;
                }
            }
            return false;
        }
        return left.to_string().find(right.to_string()) != std::string::npos;
    }
    throw std::runtime_error("unsupported bytecode binary operator: " + op);
}

struct BytecodeFrame {
    std::unordered_map<std::string, Value> temps;
    std::unordered_map<std::string, Value> locals;
};

Value operand_value(const BytecodeModule& module, const BytecodeFunction& function, const BytecodeFrame& frame, const std::string& operand) {
    if (operand.empty()) {
        return Value();
    }
    if (operand[0] == '%') {
        const auto found = frame.temps.find(operand);
        if (found == frame.temps.end()) {
            throw std::runtime_error("undefined bytecode temporary: " + operand);
        }
        return found->second;
    }
    if (operand[0] == 'K') {
        const std::size_t index = static_cast<std::size_t>(std::stoul(operand.substr(1)));
        if (index >= module.constants.size()) {
            throw std::runtime_error("constant index out of range: " + operand);
        }
        return parse_constant_value(module.constants[index]);
    }
    if (operand[0] == 'L') {
        const auto found = frame.locals.find(operand);
        if (found != frame.locals.end()) {
            return found->second;
        }
        const std::size_t index = static_cast<std::size_t>(std::stoul(operand.substr(1)));
        if (index >= function.locals.size()) {
            throw std::runtime_error("local index out of range: " + operand);
        }
        throw std::runtime_error("undefined bytecode local: " + function.locals[index]);
    }
    return parse_constant_value(operand);
}

std::string local_name(const BytecodeFunction& function, const std::string& ref) {
    if (ref.empty() || ref[0] != 'L') {
        return ref;
    }
    const std::size_t index = static_cast<std::size_t>(std::stoul(ref.substr(1)));
    if (index >= function.locals.size()) {
        throw std::runtime_error("local index out of range: " + ref);
    }
    return function.locals[index];
}

const BytecodeFunction* find_function(const BytecodeModule& module, const std::string& name) {
    for (const auto& function : module.functions) {
        if (function.name == name) {
            return &function;
        }
    }
    return nullptr;
}

Value index_value(const Value& target, const Value& index_value) {
    if (target.is_array()) {
        const int index = static_cast<int>(index_value.as_number());
        const auto& array = target.as_array();
        if (index < 0 || static_cast<std::size_t>(index) >= array.size()) {
            throw std::runtime_error("array index out of range");
        }
        return array[static_cast<std::size_t>(index)];
    }
    if (target.is_string()) {
        const int index = static_cast<int>(index_value.as_number());
        const std::string text = target.to_string();
        if (index < 0 || static_cast<std::size_t>(index) >= text.size()) {
            throw std::runtime_error("string index out of range");
        }
        return std::string(1, text[static_cast<std::size_t>(index)]);
    }
    if (target.is_object()) {
        return target.get_property(index_value.to_string());
    }
    throw std::runtime_error("value is not indexable");
}

void assign_indexed(Value& target, const std::vector<Value>& indexes, std::size_t index_position, Value value) {
    if (index_position >= indexes.size()) {
        target = std::move(value);
        return;
    }
    if (target.is_array()) {
        auto& array = target.as_array();
        const int index = static_cast<int>(indexes[index_position].as_number());
        if (index < 0 || static_cast<std::size_t>(index) >= array.size()) {
            throw std::runtime_error("array index out of range");
        }
        assign_indexed(array[static_cast<std::size_t>(index)], indexes, index_position + 1, std::move(value));
        return;
    }
    if (target.is_object()) {
        auto& object = target.as_object();
        assign_indexed(object[indexes[index_position].to_string()], indexes, index_position + 1, std::move(value));
        return;
    }
    throw std::runtime_error("value is not index-assignable");
}

Value execute_function(const BytecodeModule& module, const BytecodeFunction& function, Runtime& runtime, const std::vector<Value>& args) {
    if (function.blocks.empty()) {
        throw std::runtime_error(function.name + " has no bytecode blocks");
    }

    struct Cursor {
        std::size_t block = 0;
        std::size_t instruction = 0;
    };
    struct TryHandler {
        Cursor catch_cursor;
        std::string error_name;
    };

    std::unordered_map<std::string, Cursor> targets;
    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        targets[function.blocks[block_index].name] = Cursor{block_index, 0};
        const auto& block = function.blocks[block_index];
        for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
            const auto& instruction = block.instructions[instruction_index];
            if (instruction.op == BytecodeOp::Label && !instruction.operands.empty()) {
                targets[instruction.operands.front()] = Cursor{block_index, instruction_index + 1};
            }
        }
    }

    const auto jump_to = [&](const std::string& target) {
        const auto found = targets.find(target);
        if (found == targets.end()) {
            throw std::runtime_error("unresolved bytecode target: " + target);
        }
        return found->second;
    };

    BytecodeFrame frame;
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        if (i >= args.size()) {
            break;
        }
        for (std::size_t local_index = 0; local_index < function.locals.size(); ++local_index) {
            if (function.locals[local_index] == function.params[i]) {
                frame.locals["L" + std::to_string(local_index)] = args[i];
                break;
            }
        }
    }

    Cursor cursor{0, 0};
    std::vector<TryHandler> try_stack;
    while (cursor.block < function.blocks.size()) {
        const BytecodeBlock& block = function.blocks[cursor.block];
        if (cursor.instruction >= block.instructions.size()) {
            return Value();
        }
        const BytecodeInstruction& instruction = block.instructions[cursor.instruction++];
        runtime.tick();
        try {
            switch (instruction.op) {
            case BytecodeOp::Label:
            case BytecodeOp::Source:
                break;
            case BytecodeOp::Const:
                frame.temps[instruction.operands[0]] = operand_value(module, function, frame, instruction.operands[1]);
                break;
            case BytecodeOp::Load:
                frame.temps[instruction.operands[0]] = operand_value(module, function, frame, instruction.operands[1]);
                break;
            case BytecodeOp::Store: {
                const Value value = operand_value(module, function, frame, instruction.operands[1]);
                frame.locals[instruction.operands[0]] = value;
                runtime.set_global(local_name(function, instruction.operands[0]), value);
                break;
            }
            case BytecodeOp::StoreIndex: {
                if (instruction.operands.size() < 3) {
                    throw std::runtime_error("STORE_INDEX expects a target, at least one index, and a value");
                }
                Value target = operand_value(module, function, frame, instruction.operands[0]);
                std::vector<Value> indexes;
                for (std::size_t i = 1; i + 1 < instruction.operands.size(); ++i) {
                    indexes.push_back(operand_value(module, function, frame, instruction.operands[i]));
                }
                Value value = operand_value(module, function, frame, instruction.operands.back());
                assign_indexed(target, indexes, 0, value);
                frame.locals[instruction.operands[0]] = target;
                runtime.set_global(local_name(function, instruction.operands[0]), target);
                break;
            }
            case BytecodeOp::Unary:
                frame.temps[instruction.operands[0]] = eval_unary(instruction.operands[1], operand_value(module, function, frame, instruction.operands[2]));
                break;
            case BytecodeOp::Binary:
                frame.temps[instruction.operands[0]] = eval_binary(instruction.operands[1], operand_value(module, function, frame, instruction.operands[2]),
                                                                    operand_value(module, function, frame, instruction.operands[3]));
                break;
            case BytecodeOp::CallValue: {
                std::vector<Value> args;
                for (std::size_t i = 2; i < instruction.operands.size(); ++i) {
                    args.push_back(operand_value(module, function, frame, instruction.operands[i]));
                }
                if (const BytecodeFunction* user_function = find_function(module, instruction.operands[1])) {
                    frame.temps[instruction.operands[0]] = execute_function(module, *user_function, runtime, args);
                } else {
                    frame.temps[instruction.operands[0]] = runtime.call_host_function(instruction.operands[1], args);
                }
                break;
            }
            case BytecodeOp::CallRuntime: {
                std::vector<Value> args;
                for (std::size_t i = 1; i < instruction.operands.size(); ++i) {
                    args.push_back(operand_value(module, function, frame, instruction.operands[i]));
                }
                if (instruction.operands[0] == "Runtime.Print") {
                    if (!args.empty()) {
                        runtime.output() << args[0].to_string();
                    }
                    runtime.output() << '\n';
                } else {
                    (void)runtime.call_host_function(instruction.operands[0], args);
                }
                break;
            }
            case BytecodeOp::Array: {
                Value::Array values;
                for (std::size_t i = 1; i < instruction.operands.size(); ++i) {
                    values.push_back(operand_value(module, function, frame, instruction.operands[i]));
                }
                frame.temps[instruction.operands[0]] = Value(std::move(values));
                break;
            }
            case BytecodeOp::Object: {
                Value::Object values;
                for (std::size_t i = 1; i < instruction.operands.size(); ++i) {
                    const auto split = instruction.operands[i].find(':');
                    if (split == std::string::npos) {
                        throw std::runtime_error("invalid OBJECT field operand: " + instruction.operands[i]);
                    }
                    std::string key = instruction.operands[i].substr(0, split);
                    if (key.size() >= 2 && key.front() == '"' && key.back() == '"') {
                        key = unquote_constant(key);
                    }
                    values[key] = operand_value(module, function, frame, instruction.operands[i].substr(split + 1));
                }
                frame.temps[instruction.operands[0]] = Value(std::move(values));
                break;
            }
            case BytecodeOp::Index:
                frame.temps[instruction.operands[0]] =
                    index_value(operand_value(module, function, frame, instruction.operands[1]), operand_value(module, function, frame, instruction.operands[2]));
                break;
            case BytecodeOp::Jump:
                cursor = jump_to(instruction.operands.front());
                break;
            case BytecodeOp::Branch:
                cursor = operand_value(module, function, frame, instruction.operands[0]).truthy() ? jump_to(instruction.operands[1])
                                                                                                   : jump_to(instruction.operands[2]);
                break;
            case BytecodeOp::TryBegin:
                try_stack.push_back(TryHandler{jump_to(instruction.operands.front()), instruction.operands.size() > 1 ? instruction.operands[1] : ""});
                break;
            case BytecodeOp::TryEnd:
                if (!try_stack.empty()) {
                    try_stack.pop_back();
                }
                break;
            case BytecodeOp::DeclareFunction:
            case BytecodeOp::DeclareClass:
            case BytecodeOp::DeclareInterface:
                break;
            case BytecodeOp::Return:
                return instruction.operands.size() > 1 ? operand_value(module, function, frame, instruction.operands[1]) : Value();
            case BytecodeOp::Unsupported:
                throw std::runtime_error("cannot execute unsupported bytecode instruction");
            default:
                throw std::runtime_error("bytecode VM does not implement opcode yet: " + bytecode_op_name(instruction.op));
            }
        } catch (const std::exception& error) {
            if (try_stack.empty()) {
                throw;
            }
            const TryHandler handler = try_stack.back();
            try_stack.pop_back();
            if (!handler.error_name.empty()) {
                Value::Object object;
                object["Message"] = error.what();
                object["Type"] = "RuntimeError";
                const Value error_value(std::move(object));
                bool stored_local = false;
                for (std::size_t local_index = 0; local_index < function.locals.size(); ++local_index) {
                    if (function.locals[local_index] == handler.error_name) {
                        frame.locals["L" + std::to_string(local_index)] = error_value;
                        stored_local = true;
                        break;
                    }
                }
                if (!stored_local) {
                    runtime.set_global(handler.error_name, error_value);
                }
            }
            cursor = handler.catch_cursor;
        }
    }
    return Value();
}

Value execute_bytecode(const BytecodeModule& module, Runtime& runtime) {
    if (module.functions.empty()) {
        throw std::runtime_error("bytecode module has no functions");
    }
    const BytecodeFunction* main = find_function(module, "Main");
    if (!main) {
        throw std::runtime_error("bytecode module has no Main function");
    }
    return execute_function(module, *main, runtime, {});
}

std::filesystem::path source_root_path() {
    return std::filesystem::path(__FILE__).parent_path().parent_path();
}

std::string cache_value(const std::filesystem::path& cache_path, const std::string& key) {
    std::ifstream input(cache_path);
    std::string line;
    while (std::getline(input, line)) {
        const std::string prefix = key + ":";
        if (line.rfind(prefix, 0) != 0) {
            continue;
        }
        const auto equals = line.find('=');
        if (equals != std::string::npos) {
            return line.substr(equals + 1);
        }
    }
    return "";
}

std::vector<std::string> split_shell_like(const std::string& line) {
    std::vector<std::string> words;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    bool escaping = false;
    for (char c : line) {
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
        if ((c == ' ' || c == '\t') && !in_single && !in_double) {
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

std::vector<std::string> native_link_dependencies(const std::filesystem::path& build_dir) {
    std::ifstream input(build_dir / "CMakeFiles" / "ArcoFission.dir" / "link.txt");
    std::string line;
    if (!std::getline(input, line)) {
        return {};
    }

    const auto words = split_shell_like(line);
    std::vector<std::string> deps;
    bool after_arco = false;
    for (const auto& word : words) {
        if (!after_arco) {
            if (word == "libarco.a" || word.size() >= 10 && word.substr(word.size() - 10) == "/libarco.a") {
                after_arco = true;
            }
            continue;
        }
        deps.push_back(word);
    }
    return deps;
}

#if defined(__linux__)
std::filesystem::path current_executable_dir() {
    std::vector<char> buffer(4096);
    while (true) {
        const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (size < 0) {
            throw std::runtime_error("could not resolve /proc/self/exe");
        }
        if (static_cast<std::size_t>(size) < buffer.size() - 1) {
            buffer[static_cast<std::size_t>(size)] = '\0';
            return std::filesystem::path(buffer.data()).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool is_elf64_file(const std::filesystem::path& path) {
    unsigned char header[5] = {};
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(header), sizeof(header));
    return input.gcount() == static_cast<std::streamsize>(sizeof(header)) && header[0] == 0x7f && header[1] == 'E' &&
           header[2] == 'L' && header[3] == 'F' && header[4] == 2;
}
#endif

Result build_native_bytecode(const std::string& bytecode, const std::string& output_path) {
#if defined(__linux__)
    try {
        const std::filesystem::path source_root = source_root_path();
        const std::filesystem::path build_dir = current_executable_dir();
        const std::filesystem::path libarco = build_dir / "libarco.a";
        if (!std::filesystem::exists(libarco)) {
            return {false, "", "native build needs libarco.a beside ArcoFission; run from a CMake build tree"};
        }

        const std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() /
                                              ("arcofission-native-" + std::to_string(static_cast<long long>(::getpid())));
        std::filesystem::create_directories(tmp_dir);
        const std::filesystem::path launcher = tmp_dir / "launcher.cpp";
        {
            std::ofstream out(launcher);
            if (!out) {
                return {false, "", "could not write native launcher source"};
            }
            out << "#include \"arco/fission.hpp\"\n"
                << "#include <iostream>\n"
                << "\n"
                << "int main() {\n"
                << "    const std::string bytecode = " << cpp_string_literal(bytecode) << ";\n"
                << "    const auto result = arco::fission::run_bytecode(bytecode);\n"
                << "    if (!result.ok) {\n"
                << "        std::cerr << result.error << '\\n';\n"
                << "        return 1;\n"
                << "    }\n"
                << "    std::cout << result.output;\n"
                << "    return 0;\n"
                << "}\n";
        }

        const std::filesystem::path cache = build_dir / "CMakeCache.txt";
        std::string compiler = cache_value(cache, "CMAKE_CXX_COMPILER");
        if (compiler.empty()) {
            const char* env_cxx = std::getenv("CXX");
            compiler = env_cxx && *env_cxx ? env_cxx : "c++";
        }

        std::vector<std::string> args{
            compiler,
            "-std=c++17",
            "-O2",
            launcher.string(),
            "-o",
            output_path,
            "-I" + (source_root / "include").string(),
            "-I" + (source_root / "bindings" / "c").string(),
            "-I" + (source_root / "bindings" / "cpp").string(),
            libarco.string(),
        };
        const auto deps = native_link_dependencies(build_dir);
        args.insert(args.end(), deps.begin(), deps.end());

        std::ostringstream command;
        bool first = true;
        for (const auto& arg : args) {
            if (!first) {
                command << ' ';
            }
            first = false;
            command << shell_quote(arg);
        }

        const int status = std::system(command.str().c_str());
        std::filesystem::remove_all(tmp_dir);
        if (status == -1) {
            return {false, "", "could not launch C++ compiler"};
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            return {false, "", "C++ compiler failed while building native ELF64"};
        }
        if (!is_elf64_file(output_path)) {
            return {false, "", "native compiler did not produce an ELF64 executable"};
        }
        ::chmod(output_path.c_str(), 0755);

        std::ostringstream message;
        message << "SOURCE ACCEPTED\n";
        message << "STRUCTURE ASSEMBLED\n";
        message << "BYTECODE EMBEDDED\n";
        message << "ELF64 WRITTEN " << output_path << "\n";
        return {true, message.str(), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
#else
    (void)bytecode;
    (void)output_path;
    return {false, "", "native ELF64 builds are only supported on Linux"};
#endif
}

std::string emit_ast(const std::vector<std::unique_ptr<Stmt>>& statements, const std::string& source_name) {
    std::ostringstream out;
    out << "AST MODULE \"" << escaped(source_name) << "\"\n";
    out << "VERSION 0\n\n";
    out << "Program\n";
    for (const auto& statement : statements) {
        std::ostringstream node;
        statement->dump_ast(node, 1);
        std::istringstream lines(node.str());
        std::string line;
        while (std::getline(lines, line)) {
            out << line;
            if (statement->line_label >= 0 && line.rfind("  ", 0) == 0 && line.find('@') != std::string::npos) {
                out << " label=" << statement->line_label;
            }
            out << '\n';
        }
    }
    return out.str();
}

} // namespace

Result reveal_amir(const std::string& source, const std::string& source_name) {
    try {
        Runtime runtime;
        const std::string processed = runtime.preprocess_source(source);
        Lexer lexer(processed);
        auto tokens = lexer.scan_tokens();

        Parser parser(tokens, runtime.compile_metadata().runtime_mode == "NONE");
        (void)parser.parse();

        return {true, render_amir(build_amir(tokens, source_name)), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result reveal_amir_file(const std::string& path) {
    try {
        return reveal_amir(read_file(path), path);
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result reveal_callconv(const std::string& source, const std::string& source_name) {
    try {
        Runtime runtime;
        const std::string processed = runtime.preprocess_source(source);
        Lexer lexer(processed);
        auto tokens = lexer.scan_tokens();

        Parser parser(tokens, runtime.compile_metadata().runtime_mode == "NONE");
        (void)parser.parse();

        return {true, render_calling_convention(build_amir(tokens, source_name)), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result reveal_callconv_file(const std::string& path) {
    try {
        return reveal_callconv(read_file(path), path);
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result reveal_x86_64(const std::string& source, const std::string& source_name, const std::string& entry_function) {
    try {
        Runtime runtime;
        const std::string processed = runtime.preprocess_source(source);
        Lexer lexer(processed);
        auto tokens = lexer.scan_tokens();

        Parser parser(tokens, runtime.compile_metadata().runtime_mode == "NONE");
        (void)parser.parse();

        const auto codegen = generate_x86_64_function(build_amir(tokens, source_name), entry_function);
        if (!codegen.ok) {
            return {false, "", codegen.error};
        }
        return {true, render_x86_64(codegen), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result reveal_x86_64_file(const std::string& path, const std::string& entry_function) {
    try {
        return reveal_x86_64(read_file(path), path, entry_function);
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result build_efi_image(const std::string& source, const std::string& source_name, const std::string& entry_function,
                        const std::string& output_path) {
    try {
        Runtime runtime;
        const std::string processed = runtime.preprocess_source(source);
        Lexer lexer(processed);
        auto tokens = lexer.scan_tokens();

        Parser parser(tokens, runtime.compile_metadata().runtime_mode == "NONE");
        (void)parser.parse();

        const auto codegen = generate_x86_64_function(build_amir(tokens, source_name), entry_function);
        if (!codegen.ok) {
            return {false, "", codegen.error};
        }

        systems::MachineCodeImage image;
        image.text = codegen.text.bytes();
        image.rdata = codegen.rdata;
        image.entry_symbol = codegen.entry_symbol;
        for (const auto& relocation : codegen.relocations) {
            image.relocations.push_back({relocation.disp_field_offset, relocation.instruction_end_offset, relocation.rdata_offset});
        }

        const auto pe_bytes = systems::write_pe32plus_efi_image(image);
        std::ofstream output(output_path, std::ios::binary);
        if (!output) {
            return {false, "", "could not open output file " + output_path};
        }
        output.write(reinterpret_cast<const char*>(pe_bytes.data()), static_cast<std::streamsize>(pe_bytes.size()));
        if (!output) {
            return {false, "", "failed while writing " + output_path};
        }

        std::ostringstream message;
        message << "SOURCE ACCEPTED\n";
        message << "STRUCTURE ASSEMBLED\n";
        message << "X86_64 GENERATED\n";
        message << "PE32+ WRITTEN " << output_path << " (" << pe_bytes.size() << " bytes)\n";
        return {true, message.str(), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result build_efi_image_file(const std::string& path, const std::string& entry_function, const std::string& output_path) {
    try {
        return build_efi_image(read_file(path), path, entry_function, output_path);
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result reveal_bytecode(const std::string& source, const std::string& source_name) {
    try {
        Runtime runtime;
        const std::string processed = runtime.preprocess_source(source);
        Lexer lexer(processed);
        auto tokens = lexer.scan_tokens();

        Parser parser(tokens, runtime.compile_metadata().runtime_mode == "NONE");
        (void)parser.parse();

        const AmirModule amir = build_amir(tokens, source_name);
        return {true, render_bytecode(build_bytecode(amir)), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result reveal_bytecode_file(const std::string& path) {
    try {
        return reveal_bytecode(read_file(path), path);
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result run_bytecode(const std::string& bytecode) {
    try {
        Runtime runtime;
        std::ostringstream output;
        runtime.set_output(output);
        (void)execute_bytecode(parse_bytecode(bytecode), runtime);
        return {true, output.str(), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result run_bytecode_file(const std::string& path) {
    try {
        return run_bytecode(read_file(path));
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result compile_run(const std::string& source, const std::string& source_name) {
    try {
        Runtime preprocess_runtime;
        const std::string processed = preprocess_runtime.preprocess_source(source);
        Lexer lexer(processed);
        auto tokens = lexer.scan_tokens();

        Parser parser(tokens, preprocess_runtime.compile_metadata().runtime_mode == "NONE");
        (void)parser.parse();

        Runtime runtime;
        std::ostringstream output;
        runtime.set_output(output);
        (void)execute_bytecode(build_bytecode(build_amir(tokens, source_name)), runtime);
        return {true, output.str(), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result compile_run_file(const std::string& path) {
    try {
        return compile_run(read_file(path), path);
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result build_native_file(const std::string& path, const std::string& output_path) {
    try {
        Runtime runtime;
        const std::string processed = runtime.preprocess_source(read_file(path));
        Lexer lexer(processed);
        auto tokens = lexer.scan_tokens();

        Parser parser(tokens, runtime.compile_metadata().runtime_mode == "NONE");
        (void)parser.parse();

        const AmirModule amir = build_amir(tokens, path);
        const std::string bytecode = render_bytecode(build_bytecode(amir));
        return build_native_bytecode(bytecode, output_path);
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result reveal_ast(const std::string& source, const std::string& source_name) {
    try {
        Runtime runtime;
        const std::string processed = runtime.preprocess_source(source);
        Lexer lexer(processed);
        auto tokens = lexer.scan_tokens();

        Parser parser(tokens, runtime.compile_metadata().runtime_mode == "NONE");
        auto statements = parser.parse();

        return {true, emit_ast(statements, source_name), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result reveal_ast_file(const std::string& path) {
    try {
        return reveal_ast(read_file(path), path);
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

} // namespace arco::fission
