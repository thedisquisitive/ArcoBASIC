#pragma once

#include "arco/runtime.hpp"
#include "token.hpp"

#include <memory>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

namespace arco {

// Canonical, compiler-facing AST representation (RFC-0012). The interpreter's executable AST
// nodes expose this immutable-by-convention snapshot after parsing; later compiler stages consume
// it instead of reconstructing program structure from the token stream.
enum class AstKind {
    Unsupported,
    Literal,
    InterpolatedString,
    Variable,
    Unary,
    Binary,
    Logical,
    Call,
    MethodCall,
    SuperCall,
    Index,
    Array,
    Object,
    Print,
    Assign,
    CompoundAssign,
    FlagOperation,
    Flags,
    HardwareSemantic,
    ExpressionStatement,
    NoOp,
    Return,
    Function,
    Class,
    ClassField,
    ClassMethod,
    Interface,
    InterfaceMethod,
    Try,
    Goto,
    Stop,
    LoopControl,
    Block,
    If,
    Select,
    SelectBranch,
    SelectMatch,
    While,
    Do,
    For,
    ForEach,
};

struct CanonicalAstNode;
using CanonicalAstNodePtr = std::shared_ptr<CanonicalAstNode>;

struct CanonicalAstParameter {
    std::string name;
    std::string type_name;
    CanonicalAstNodePtr default_value;
};

struct CanonicalAstGroup {
    std::string role;
    std::vector<CanonicalAstNodePtr> nodes;
};

struct CanonicalAstNode {
    AstKind kind = AstKind::Unsupported;
    int source_line = 1;
    int source_column = 1;
    int line_label = -1;

    // Kind-specific scalar attributes. Their meaning is defined by the AstKind and kept explicit
    // in parser.cpp's construction sites and the compiler's AST visitor.
    std::string name;
    std::string secondary_name;
    std::string type_name;
    std::string text;
    TokenType op = TokenType::End;
    Value literal;
    int integer = 0;
    bool flag = false;
    bool flag2 = false;

    std::vector<std::string> names;
    std::vector<CanonicalAstParameter> parameters;
    std::vector<CanonicalAstNodePtr> children;
    std::vector<std::pair<std::string, CanonicalAstNodePtr>> named_children;
    std::vector<CanonicalAstGroup> groups;
};

struct Expr {
    virtual ~Expr() = default;
    virtual Value eval(Runtime& runtime) const = 0;
    virtual void dump_ast(std::ostream& output, int indent) const;
    virtual CanonicalAstNodePtr canonical_ast() const;
};

struct Stmt {
    virtual ~Stmt() = default;
    virtual void exec(Runtime& runtime) const = 0;
    virtual void dump_ast(std::ostream& output, int indent) const;
    virtual CanonicalAstNodePtr canonical_ast() const;
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
    explicit Parser(std::vector<Token> tokens, bool freestanding_runtime_none = false);
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
    void validate_uefi_field_chain(const Token& location, const std::string& root_type, const std::string& dotted_path);
    void validate_utf16_arguments(const Token& location, const std::vector<ExprPtr>& args);

    StmtPtr statement();
    StmtPtr print_statement();
    StmtPtr assignment_statement(bool had_let);
    StmtPtr compound_assignment_statement();
    StmtPtr flag_operation_statement();
    StmtPtr flags_statement();
    StmtPtr hardware_semantic_statement();
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
    bool freestanding_runtime_none_ = false;
    std::unordered_map<std::string, std::string> current_function_parameter_types_;
};

} // namespace arco
