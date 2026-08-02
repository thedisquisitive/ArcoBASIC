#pragma once

#include "arco/runtime.hpp"
#include "token.hpp"

#include <memory>
#include <iosfwd>
#include <string>
#include <vector>

namespace arco {

struct Expr {
    virtual ~Expr() = default;
    virtual Value eval(Runtime& runtime) const = 0;
    virtual void dump_ast(std::ostream& output, int indent) const;
};

struct Stmt {
    virtual ~Stmt() = default;
    virtual void exec(Runtime& runtime) const = 0;
    virtual void dump_ast(std::ostream& output, int indent) const;
    int line_label = -1;
    int source_line = 1;
    int source_column = 1;
};

struct FunctionParam {
    std::string name;
    std::string type_name;
    std::shared_ptr<Expr> default_value;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    std::vector<std::unique_ptr<Stmt>> parse();

private:
    using ExprPtr = std::unique_ptr<Expr>;
    using StmtPtr = std::unique_ptr<Stmt>;

    bool at_end() const;
    const Token& peek() const;
    const Token& previous() const;
    const Token& advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool match_any(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, const std::string& message);
    void skip_newlines();
    void skip_line_number();
    std::string parse_type_name(const std::string& message);
    void validate_fixed_width_initializer(const Token& variable, const std::string& type_name);

    StmtPtr statement();
    StmtPtr print_statement();
    StmtPtr assignment_statement(bool had_let);
    StmtPtr compound_assignment_statement();
    StmtPtr flag_operation_statement();
    StmtPtr flags_statement();
    StmtPtr expression_statement();
    StmtPtr if_statement();
    StmtPtr select_statement();
    StmtPtr while_statement();
    StmtPtr do_statement();
    StmtPtr for_statement();
    StmtPtr function_statement();
    StmtPtr class_statement();
    StmtPtr interface_statement();
    StmtPtr return_statement();
    StmtPtr try_statement();
    StmtPtr goto_statement();
    StmtPtr stop_statement();
    StmtPtr loop_control_statement();
    std::vector<StmtPtr> block_until(std::initializer_list<TokenType> terminators);
    std::vector<struct FunctionParam> parameter_list();

    ExprPtr expression();
    ExprPtr logical_or();
    ExprPtr logical_and();
    ExprPtr bit_or();
    ExprPtr bit_xor();
    ExprPtr bit_and();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr shift();
    ExprPtr term();
    ExprPtr factor();
    ExprPtr unary();
    ExprPtr call();
    ExprPtr primary();

    std::vector<Token> tokens_;
    std::size_t current_ = 0;
    std::string current_super_class_;
};

} // namespace arco
