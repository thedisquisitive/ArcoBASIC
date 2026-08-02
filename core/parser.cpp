#include "parser.hpp"
#include "lexer.hpp"

#include "arco/fixed_width_types.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <memory>
#include <utility>

namespace arco {

namespace {

long long as_int(const Value& value) {
    return static_cast<long long>(value.as_number());
}

std::string next_wider_type_name(const std::string& type_name) {
    if (type_name == "U8") return "U16";
    if (type_name == "U16") return "U32";
    if (type_name == "U32") return "U64";
    if (type_name == "I8") return "I16";
    if (type_name == "I16") return "I32";
    if (type_name == "I32") return "I64";
    return type_name;
}

Value apply_binary(TokenType op, const Value& a, const Value& b) {
    switch (op) {
        case TokenType::Plus:
            if (a.is_string() || b.is_string()) {
                return a.to_string() + b.to_string();
            }
            return a.as_number() + b.as_number();
        case TokenType::Minus:
            return a.as_number() - b.as_number();
        case TokenType::Star:
            return a.as_number() * b.as_number();
        case TokenType::Slash:
            return a.as_number() / b.as_number();
        case TokenType::Mod: {
            const double divisor = b.as_number();
            if (divisor == 0.0) {
                throw std::runtime_error("MOD divisor cannot be zero");
            }
            return std::fmod(a.as_number(), divisor);
        }
        case TokenType::Ampersand:
        case TokenType::BitAnd:
            return static_cast<double>(as_int(a) & as_int(b));
        case TokenType::Pipe:
        case TokenType::BitOr:
            return static_cast<double>(as_int(a) | as_int(b));
        case TokenType::Caret:
        case TokenType::BitXor:
            return static_cast<double>(as_int(a) ^ as_int(b));
        case TokenType::ShiftLeft:
        case TokenType::ShiftLeftWord:
            return static_cast<double>(as_int(a) << as_int(b));
        case TokenType::ShiftRight:
        case TokenType::ShiftRightWord:
            return static_cast<double>(as_int(a) >> as_int(b));
        case TokenType::Equal:
            return values_equal(a, b);
        case TokenType::NotEqual:
            return !values_equal(a, b);
        case TokenType::Less:
            return a.as_number() < b.as_number();
        case TokenType::LessEqual:
            return a.as_number() <= b.as_number();
        case TokenType::Greater:
            return a.as_number() > b.as_number();
        case TokenType::GreaterEqual:
            return a.as_number() >= b.as_number();
        case TokenType::Contains:
            if (a.is_array()) {
                for (const auto& item : a.as_array()) {
                    if (values_equal(item, b)) {
                        return true;
                    }
                }
                return false;
            }
            return a.to_string().find(b.to_string()) != std::string::npos;
        case TokenType::In:
            if (b.is_array()) {
                for (const auto& item : b.as_array()) {
                    if (values_equal(a, item)) {
                        return true;
                    }
                }
                return false;
            }
            return b.to_string().find(a.to_string()) != std::string::npos;
        case TokenType::Has:
            return (as_int(a) & as_int(b)) != 0;
        default:
            throw std::runtime_error("unsupported binary operator");
    }
}

std::string token_error(const Token& token, const std::string& message) {
    std::ostringstream out;
    out << "line " << token.line << ", column " << token.column << ": " << message;
    return out.str();
}

std::string uppercase(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

// Namespace prefixes that unambiguously require the hosted runtime (docs/systems/uefi-target.md
// section 7). Deliberately short and explicit: only names with no plausible freestanding meaning.
// Matched as a whole leading dot-segment, not a substring, so "SystemTable" does not match "System".
bool is_hosted_runtime_namespace(const std::string& first_segment) {
    static const std::initializer_list<const char*> forbidden = {
        "FILE", "NETWORK", "NET", "SYSTEM", "WEB", "PRINTER", "PROCESS", "HOST", "DOCUMENT",
    };
    const std::string upper = uppercase(first_segment);
    for (const char* name : forbidden) {
        if (upper == name) {
            return true;
        }
    }
    return false;
}

std::string freestanding_diagnostic(const std::string& construct, const std::string& alternative) {
    return construct + " is not available under #RUNTIME NONE.\n" + alternative;
}

void ast_indent(std::ostream& output, int indent) {
    for (int i = 0; i < indent; ++i) {
        output << "  ";
    }
}

void ast_line(std::ostream& output, int indent, const std::string& text) {
    ast_indent(output, indent);
    output << text << '\n';
}

std::string ast_quote(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (char c : text) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << c; break;
        }
    }
    out << '"';
    return out.str();
}

std::string ast_value(const Value& value) {
    if (value.is_string()) {
        return ast_quote(value.to_string());
    }
    return value.to_string();
}

std::string ast_token_name(TokenType type) {
    switch (type) {
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Star: return "*";
        case TokenType::Slash: return "/";
        case TokenType::Mod: return "%";
        case TokenType::Ampersand:
        case TokenType::BitAnd: return "AND";
        case TokenType::Pipe:
        case TokenType::BitOr: return "OR";
        case TokenType::LogicalAnd:
        case TokenType::AndAlso: return "ANDALSO";
        case TokenType::LogicalOr:
        case TokenType::OrElse: return "ORELSE";
        case TokenType::Caret:
        case TokenType::BitXor: return "XOR";
        case TokenType::Tilde:
        case TokenType::BitNot: return "NOT";
        case TokenType::Bang: return "!";
        case TokenType::Equal: return "==";
        case TokenType::NotEqual: return "<>";
        case TokenType::Less: return "<";
        case TokenType::LessEqual: return "<=";
        case TokenType::Greater: return ">";
        case TokenType::GreaterEqual: return ">=";
        case TokenType::ShiftLeft:
        case TokenType::ShiftLeftWord: return "SHL";
        case TokenType::ShiftRight:
        case TokenType::ShiftRightWord: return "SHR";
        case TokenType::Contains: return "CONTAINS";
        case TokenType::In: return "IN";
        case TokenType::Has: return "HAS";
        case TokenType::Add: return "ADD";
        case TokenType::Remove: return "REMOVE";
        case TokenType::Toggle: return "TOGGLE";
        default: return "operator";
    }
}

enum class LoopControlAction {
    Exit,
    Continue,
};

enum class LoopControlTarget {
    For,
    While,
    Do,
};

class LoopControlSignal final : public std::exception {
public:
    LoopControlSignal(LoopControlAction action, LoopControlTarget target) : action_(action), target_(target) {}
    const char* what() const noexcept override {
        if (action_ == LoopControlAction::Exit) {
            if (target_ == LoopControlTarget::For) {
                return "EXIT FOR outside FOR loop";
            }
            if (target_ == LoopControlTarget::While) {
                return "EXIT WHILE outside WHILE loop";
            }
            return "EXIT DO outside DO loop";
        }
        if (target_ == LoopControlTarget::For) {
            return "CONTINUE FOR outside FOR loop";
        }
        if (target_ == LoopControlTarget::While) {
            return "CONTINUE WHILE outside WHILE loop";
        }
        return "CONTINUE DO outside DO loop";
    }
    LoopControlAction action() const noexcept { return action_; }
    LoopControlTarget target() const noexcept { return target_; }

private:
    LoopControlAction action_;
    LoopControlTarget target_;
};

struct LiteralExpr final : Expr {
    explicit LiteralExpr(Value value) : value(std::move(value)) {}
    Value eval(Runtime&) const override { return value; }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Literal " + ast_value(value));
    }
    Value value;
};

struct InterpolatedStringExpr final : Expr {
    explicit InterpolatedStringExpr(std::string text) : text(std::move(text)) {}
    Value eval(Runtime& runtime) const override {
        std::ostringstream output;
        for (std::size_t i = 0; i < text.size();) {
            if (text[i] == '{') {
                if (i + 1 < text.size() && text[i + 1] == '{') {
                    output << '{';
                    i += 2;
                    continue;
                }
                std::size_t depth = 1;
                std::size_t end = i + 1;
                while (end < text.size() && depth > 0) {
                    if (text[end] == '{') {
                        depth++;
                    } else if (text[end] == '}') {
                        depth--;
                    }
                    if (depth > 0) {
                        end++;
                    }
                }
                if (depth != 0) {
                    throw std::runtime_error("unterminated interpolation expression");
                }
                const std::string source = "PRINT " + text.substr(i + 1, end - i - 1) + "\n";
                Lexer lexer(source);
                Parser parser(lexer.scan_tokens());
                auto statements = parser.parse();
                std::ostringstream captured;
                auto& previous_output = runtime.output();
                runtime.set_output(captured);
                try {
                    for (const auto& statement : statements) {
                        statement->exec(runtime);
                    }
                } catch (...) {
                    runtime.set_output(previous_output);
                    throw;
                }
                runtime.set_output(previous_output);
                std::string value = captured.str();
                if (!value.empty() && value.back() == '\n') {
                    value.pop_back();
                }
                output << value;
                i = end + 1;
                continue;
            }
            if (text[i] == '}' && i + 1 < text.size() && text[i + 1] == '}') {
                output << '}';
                i += 2;
                continue;
            }
            output << text[i++];
        }
        return output.str();
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "InterpolatedString " + ast_quote(text));
    }
    std::string text;
};

struct VariableExpr final : Expr {
    explicit VariableExpr(std::string name) : name(std::move(name)) {}
    Value eval(Runtime& runtime) const override { return runtime.get_global(name); }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Variable " + name);
    }
    std::string name;
};

struct UnaryExpr final : Expr {
    UnaryExpr(TokenType op, std::unique_ptr<Expr> right) : op(op), right(std::move(right)) {}
    Value eval(Runtime& runtime) const override {
        const Value value = right->eval(runtime);
        if (op == TokenType::Minus) {
            return -value.as_number();
        }
        if (op == TokenType::Tilde || op == TokenType::BitNot) {
            return static_cast<double>(~as_int(value));
        }
        if (op == TokenType::Bang) {
            return !value.truthy();
        }
        return value;
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Unary " + ast_token_name(op));
        right->dump_ast(output, indent + 1);
    }
    TokenType op;
    std::unique_ptr<Expr> right;
};

struct BinaryExpr final : Expr {
    BinaryExpr(std::unique_ptr<Expr> left, TokenType op, std::unique_ptr<Expr> right)
        : left(std::move(left)), op(op), right(std::move(right)) {}

    Value eval(Runtime& runtime) const override {
        const Value a = left->eval(runtime);
        const Value b = right->eval(runtime);
        return apply_binary(op, a, b);
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Binary " + ast_token_name(op));
        left->dump_ast(output, indent + 1);
        right->dump_ast(output, indent + 1);
    }

    std::unique_ptr<Expr> left;
    TokenType op;
    std::unique_ptr<Expr> right;
};

struct LogicalExpr final : Expr {
    LogicalExpr(std::unique_ptr<Expr> left, TokenType op, std::unique_ptr<Expr> right)
        : left(std::move(left)), op(op), right(std::move(right)) {}

    Value eval(Runtime& runtime) const override {
        const bool left_value = left->eval(runtime).truthy();
        if (op == TokenType::LogicalAnd || op == TokenType::AndAlso) {
            if (!left_value) {
                return false;
            }
            return right->eval(runtime).truthy();
        }
        if (left_value) {
            return true;
        }
        return right->eval(runtime).truthy();
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Logical " + ast_token_name(op));
        left->dump_ast(output, indent + 1);
        right->dump_ast(output, indent + 1);
    }

    std::unique_ptr<Expr> left;
    TokenType op;
    std::unique_ptr<Expr> right;
};

struct CallExpr final : Expr {
    CallExpr(std::string name, std::vector<std::unique_ptr<Expr>> args)
        : name(std::move(name)), args(std::move(args)) {}

    Value eval(Runtime& runtime) const override {
        if (uppercase(name) == "REF" && (args.size() == 1 || args.size() == 2)) {
            if (const auto* variable = dynamic_cast<const VariableExpr*>(args[0].get())) {
                std::string type_name;
                if (args.size() == 2) {
                    type_name = args[1]->eval(runtime).to_string();
                }
                return runtime.make_reference_to(variable->name, type_name);
            }
        }
        std::vector<Value> values;
        values.reserve(args.size());
        for (const auto& arg : args) {
            values.push_back(arg->eval(runtime));
        }
        return runtime.call_host_function(name, values);
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Call " + name);
        for (const auto& arg : args) {
            arg->dump_ast(output, indent + 1);
        }
    }

    std::string name;
    std::vector<std::unique_ptr<Expr>> args;
};

struct MethodCallExpr final : Expr {
    MethodCallExpr(std::string full_name, std::string receiver, std::string method, std::vector<std::unique_ptr<Expr>> args)
        : full_name(std::move(full_name)), receiver(std::move(receiver)), method(std::move(method)), args(std::move(args)) {}

    Value eval(Runtime& runtime) const override {
        std::vector<Value> values;
        values.reserve(args.size());
        for (const auto& arg : args) {
            values.push_back(arg->eval(runtime));
        }
        if (runtime.has_global(receiver)) {
            const Value target = runtime.get_global(receiver);
            if (target.is_object()) {
                bool has_class = false;
                try {
                    (void)target.get_property("__class");
                    has_class = true;
                } catch (const std::exception&) {
                }
                if (has_class) {
                    return runtime.call_method(target, method, values);
                }
            }
        }
        return runtime.call_host_function(full_name, values);
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "MethodCall " + receiver + "." + method);
        for (const auto& arg : args) {
            arg->dump_ast(output, indent + 1);
        }
    }

    std::string full_name;
    std::string receiver;
    std::string method;
    std::vector<std::unique_ptr<Expr>> args;
};

struct SuperCallExpr final : Expr {
    SuperCallExpr(std::string class_name, std::string method, std::vector<std::unique_ptr<Expr>> args)
        : class_name(std::move(class_name)), method(std::move(method)), args(std::move(args)) {}

    Value eval(Runtime& runtime) const override {
        std::vector<Value> values;
        values.reserve(args.size() + 1);
        values.push_back(runtime.get_global("SELF"));
        for (const auto& arg : args) {
            values.push_back(arg->eval(runtime));
        }
        return runtime.call_host_function(class_name + "." + method, values);
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "SuperCall " + class_name + "." + method);
        for (const auto& arg : args) {
            arg->dump_ast(output, indent + 1);
        }
    }

    std::string class_name;
    std::string method;
    std::vector<std::unique_ptr<Expr>> args;
};

struct IndexExpr final : Expr {
    IndexExpr(std::unique_ptr<Expr> target, std::unique_ptr<Expr> index) : target(std::move(target)), index(std::move(index)) {}
    Value eval(Runtime& runtime) const override {
        const Value value = target->eval(runtime);
        const int i = static_cast<int>(index->eval(runtime).as_number());
        if (value.is_array()) {
            const auto& array = value.as_array();
            if (i < 0 || static_cast<std::size_t>(i) >= array.size()) {
                throw std::runtime_error("array index out of range");
            }
            return array[static_cast<std::size_t>(i)];
        }
        if (value.is_string()) {
            const std::string text = value.to_string();
            if (i < 0 || static_cast<std::size_t>(i) >= text.size()) {
                throw std::runtime_error("string index out of range");
            }
            return std::string(1, text[static_cast<std::size_t>(i)]);
        }
        throw std::runtime_error("value is not indexable");
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Index");
        ast_line(output, indent + 1, "Target");
        target->dump_ast(output, indent + 2);
        ast_line(output, indent + 1, "IndexValue");
        index->dump_ast(output, indent + 2);
    }
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> index;
};

struct ArrayExpr final : Expr {
    explicit ArrayExpr(std::vector<std::unique_ptr<Expr>> items) : items(std::move(items)) {}
    Value eval(Runtime& runtime) const override {
        Value::Array values;
        values.reserve(items.size());
        for (const auto& item : items) {
            values.push_back(item->eval(runtime));
        }
        return values;
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Array");
        for (const auto& item : items) {
            item->dump_ast(output, indent + 1);
        }
    }
    std::vector<std::unique_ptr<Expr>> items;
};

struct ObjectExpr final : Expr {
    explicit ObjectExpr(std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields) : fields(std::move(fields)) {}
    Value eval(Runtime& runtime) const override {
        Value::Object values;
        for (const auto& [key, expr] : fields) {
            values[key] = expr->eval(runtime);
        }
        return values;
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Object");
        for (const auto& [key, expr] : fields) {
            ast_line(output, indent + 1, "Field " + key);
            expr->dump_ast(output, indent + 2);
        }
    }
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
};

struct PrintStmt final : Stmt {
    explicit PrintStmt(std::unique_ptr<Expr> expr) : expr(std::move(expr)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        runtime.output() << expr->eval(runtime).to_string() << '\n';
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Print");
        expr->dump_ast(output, indent + 1);
    }
    std::unique_ptr<Expr> expr;
};

struct AssignStmt final : Stmt {
    AssignStmt(std::string name, std::vector<std::unique_ptr<Expr>> indexes, std::unique_ptr<Expr> expr,
               std::string type_name = "")
        : name(std::move(name)), indexes(std::move(indexes)), expr(std::move(expr)), type_name(std::move(type_name)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        if (indexes.empty()) {
            runtime.set_global(name, expr->eval(runtime));
            return;
        }
        std::vector<int> evaluated;
        evaluated.reserve(indexes.size());
        for (const auto& index : indexes) {
            evaluated.push_back(static_cast<int>(index->eval(runtime).as_number()));
        }
        runtime.set_indexed(name, evaluated, expr->eval(runtime));
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Assign " + name + (type_name.empty() ? "" : " AS " + type_name));
        for (const auto& index : indexes) {
            ast_line(output, indent + 1, "Index");
            index->dump_ast(output, indent + 2);
        }
        ast_line(output, indent + 1, "Value");
        expr->dump_ast(output, indent + 2);
    }
    std::string name;
    std::vector<std::unique_ptr<Expr>> indexes;
    std::unique_ptr<Expr> expr;
    std::string type_name;
};

struct CompoundAssignStmt final : Stmt {
    CompoundAssignStmt(std::string name, TokenType op, std::unique_ptr<Expr> expr) : name(std::move(name)), op(op), expr(std::move(expr)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        const Value current = runtime.get_global(name);
        const Value next = apply_binary(op, current, expr->eval(runtime));
        runtime.set_global(name, next);
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "CompoundAssign " + name + " " + ast_token_name(op));
        expr->dump_ast(output, indent + 1);
    }
    std::string name;
    TokenType op;
    std::unique_ptr<Expr> expr;
};

struct FlagOperationStmt final : Stmt {
    FlagOperationStmt(std::string name, TokenType op, std::unique_ptr<Expr> mask) : name(std::move(name)), op(op), mask(std::move(mask)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        const Value current = runtime.get_global(name);
        const Value value = mask->eval(runtime);
        switch (op) {
            case TokenType::Add:
                runtime.set_global(name, static_cast<double>(as_int(current) | as_int(value)));
                break;
            case TokenType::Remove:
                runtime.set_global(name, static_cast<double>(as_int(current) & ~as_int(value)));
                break;
            case TokenType::Toggle:
                runtime.set_global(name, static_cast<double>(as_int(current) ^ as_int(value)));
                break;
            default:
                throw std::runtime_error("unsupported flag operation");
        }
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "FlagOperation " + name + " " + ast_token_name(op));
        mask->dump_ast(output, indent + 1);
    }
    std::string name;
    TokenType op;
    std::unique_ptr<Expr> mask;
};

struct FlagsStmt final : Stmt {
    FlagsStmt(std::string name, std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields) : name(std::move(name)), fields(std::move(fields)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        Value::Object object;
        for (const auto& [field, expr] : fields) {
            object[field] = expr->eval(runtime);
        }
        runtime.set_global(name, object);
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Flags " + name);
        for (const auto& [field, expr] : fields) {
            ast_line(output, indent + 1, "Flag " + field);
            expr->dump_ast(output, indent + 2);
        }
    }
    std::string name;
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
};

struct ExprStmt final : Stmt {
    explicit ExprStmt(std::unique_ptr<Expr> expr) : expr(std::move(expr)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        expr->eval(runtime);
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "ExpressionStatement");
        expr->dump_ast(output, indent + 1);
    }
    std::unique_ptr<Expr> expr;
};

struct NoOpStmt final : Stmt {
    void exec(Runtime& runtime) const override {
        runtime.tick();
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "NoOp");
    }
};

struct ReturnStmt final : Stmt {
    explicit ReturnStmt(std::unique_ptr<Expr> value) : value(std::move(value)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        throw ReturnSignal(value ? value->eval(runtime) : Value());
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Return");
        if (value) {
            value->dump_ast(output, indent + 1);
        }
    }
    std::unique_ptr<Expr> value;
};

class ScopeGuard {
public:
    explicit ScopeGuard(Runtime& runtime) : runtime_(runtime) {
        runtime_.push_scope();
    }
    ~ScopeGuard() {
        runtime_.pop_scope();
    }
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    Runtime& runtime_;
};

class ClassContextGuard {
public:
    ClassContextGuard(Runtime& runtime, std::string class_name) : runtime_(runtime) {
        runtime_.push_class_context(std::move(class_name));
    }
    ~ClassContextGuard() {
        runtime_.pop_class_context();
    }
    ClassContextGuard(const ClassContextGuard&) = delete;
    ClassContextGuard& operator=(const ClassContextGuard&) = delete;

private:
    Runtime& runtime_;
};

using FunctionBody = std::vector<std::unique_ptr<Stmt>>;

void dump_stmt_list(std::ostream& output, int indent, const std::vector<std::unique_ptr<Stmt>>& statements) {
    for (const auto& statement : statements) {
        statement->dump_ast(output, indent);
    }
}

std::string param_summary(const FunctionParam& param) {
    std::string text = param.name;
    if (!param.type_name.empty()) {
        text += " AS " + param.type_name;
    }
    if (param.default_value) {
        text += " =";
    }
    return text;
}

Value enforce_return_type(Runtime& runtime, const std::string& callable, const std::string& type_name, const Value& value) {
    if (!type_name.empty() && !runtime.value_matches_type(value, type_name)) {
        throw std::runtime_error(callable + " should return " + type_name);
    }
    return value;
}

struct FunctionStmt final : Stmt {
    using Body = FunctionBody;

    FunctionStmt(std::string name, std::vector<FunctionParam> params, std::string return_type, std::shared_ptr<Body> body)
        : name(std::move(name)), params(std::move(params)), return_type(std::move(return_type)), body(std::move(body)) {}

    void exec(Runtime& runtime) const override {
        runtime.tick();
        const auto fn_name = name;
        const auto fn_params = params;
        const auto fn_return_type = return_type;
        const auto fn_body = body;
        runtime.register_function(fn_name, [fn_name, fn_params, fn_return_type, fn_body, &runtime](const std::vector<Value>& args) -> Value {
            std::size_t required = 0;
            for (const auto& param : fn_params) {
                if (!param.default_value) {
                    required++;
                }
            }
            if (args.size() < required || args.size() > fn_params.size()) {
                throw std::runtime_error(fn_name + " expects " + std::to_string(required) + " to " + std::to_string(fn_params.size()) + " arguments");
            }
            ScopeGuard scope(runtime);
            for (std::size_t i = 0; i < fn_params.size(); ++i) {
                Value value;
                if (i < args.size()) {
                    value = args[i];
                } else if (fn_params[i].default_value) {
                    value = fn_params[i].default_value->eval(runtime);
                } else {
                    throw std::runtime_error(fn_name + " missing argument: " + fn_params[i].name);
                }
                if (!fn_params[i].type_name.empty() && !runtime.value_matches_type(value, fn_params[i].type_name)) {
                    throw std::runtime_error(fn_name + " parameter " + fn_params[i].name + " expects " + fn_params[i].type_name);
                }
                runtime.set_global(fn_params[i].name, value);
            }
            try {
                for (const auto& statement : *fn_body) {
                    statement->exec(runtime);
                }
            } catch (const ReturnSignal& ret) {
                return enforce_return_type(runtime, fn_name, fn_return_type, ret.value());
            }
            return enforce_return_type(runtime, fn_name, fn_return_type, {});
        });
    }
    void dump_ast(std::ostream& output, int indent) const override {
        std::string header = "Function " + name;
        if (!return_type.empty()) {
            header += " AS " + return_type;
        }
        ast_line(output, indent, header);
        if (!params.empty()) {
            ast_line(output, indent + 1, "Params");
            for (const auto& param : params) {
                ast_line(output, indent + 2, param_summary(param));
                if (param.default_value) {
                    param.default_value->dump_ast(output, indent + 3);
                }
            }
        }
        ast_line(output, indent + 1, "Body");
        dump_stmt_list(output, indent + 2, *body);
    }

    std::string name;
    std::vector<FunctionParam> params;
    std::string return_type;
    std::shared_ptr<Body> body;
};

struct ClassField {
    std::string name;
    std::string type_name;
    std::shared_ptr<Expr> default_value;
    bool shared = false;
    int access = 0;
};

struct ClassMethod {
    std::string name;
    std::string super_class;
    std::vector<FunctionParam> params;
    std::string return_type;
    std::shared_ptr<FunctionBody> body;
    bool shared = false;
    int access = 0;
    bool abstract = false;
};

struct ClassStmt final : Stmt {
    ClassStmt(std::string name, std::string parent, std::vector<std::string> interfaces, std::vector<ClassField> fields, std::vector<ClassMethod> methods)
        : name(std::move(name)), parent(std::move(parent)), interfaces(std::move(interfaces)), fields(std::move(fields)), methods(std::move(methods)) {}

    void exec(Runtime& runtime) const override {
        runtime.tick();
        const auto class_name = name;
        const auto parent_name = parent;
        const auto class_interfaces = interfaces;
        const auto class_fields = fields;
        runtime.register_class(class_name, parent_name, class_interfaces);
        for (const auto& field : class_fields) {
            runtime.register_class_field(class_name, field.name, field.access, field.type_name);
        }
        for (const auto& method : methods) {
            runtime.register_class_method(class_name, method.name, method.access);
        }
        std::vector<std::string> unresolved_abstracts = parent_name.empty() ? std::vector<std::string>{} : runtime.class_abstract_methods(parent_name);
        auto remove_abstract = [&](const std::string& implemented) {
            unresolved_abstracts.erase(std::remove_if(unresolved_abstracts.begin(), unresolved_abstracts.end(), [&](const std::string& required) {
                return uppercase(required) == uppercase(implemented);
            }), unresolved_abstracts.end());
        };
        for (const auto& method : methods) {
            if (method.abstract) {
                unresolved_abstracts.push_back(method.name);
            } else {
                remove_abstract(method.name);
            }
        }
        runtime.set_class_abstract_methods(class_name, unresolved_abstracts);
        for (const auto& interface_name : class_interfaces) {
            for (const auto& required : runtime.interface_methods(interface_name)) {
                const ClassMethod* found = nullptr;
                for (const auto& method : methods) {
                    if (uppercase(method.name) == uppercase(required.name)) {
                        found = &method;
                        break;
                    }
                }
                if (!found) {
                    throw std::runtime_error(class_name + " does not implement " + interface_name + "." + required.name);
                }
                if (found->params.size() != required.param_types.size()) {
                    throw std::runtime_error(class_name + "." + found->name + " does not match " + interface_name + "." + required.name + " parameter count");
                }
                for (std::size_t i = 0; i < required.param_types.size(); ++i) {
                    if (!required.param_types[i].empty() && uppercase(found->params[i].type_name) != uppercase(required.param_types[i])) {
                        throw std::runtime_error(class_name + "." + found->name + " parameter " + found->params[i].name + " should be " + required.param_types[i]);
                    }
                }
                if (!required.return_type.empty() && uppercase(found->return_type) != uppercase(required.return_type)) {
                    throw std::runtime_error(class_name + "." + found->name + " should return " + required.return_type);
                }
            }
        }
        Value::Object class_object{{"__name", class_name}, {"__parent", parent_name}};
        if (!parent_name.empty() && runtime.has_global(parent_name)) {
            class_object = runtime.get_global(parent_name).as_object();
            class_object["__name"] = class_name;
            class_object["__parent"] = parent_name;
        }
        for (const auto& field : class_fields) {
            if (field.shared) {
                Value value = field.default_value ? field.default_value->eval(runtime) : Value();
                if (!value.is_null() && !field.type_name.empty() && !runtime.value_matches_type(value, field.type_name)) {
                    throw std::runtime_error(class_name + "." + field.name + " expects " + field.type_name);
                }
                class_object[field.name] = std::move(value);
            }
        }
        runtime.set_global(class_name, class_object);
        runtime.register_function(class_name + ".__new", [class_name, parent_name, class_fields, &runtime](const std::vector<Value>& args) -> Value {
            if (!args.empty()) {
                throw std::runtime_error(class_name + ".__new expects 0 arguments");
            }
            ScopeGuard scope(runtime);
            Value::Object object;
            if (!parent_name.empty()) {
                const Value parent_instance = runtime.call_host_function(parent_name + ".__new", {});
                object = parent_instance.as_object();
            }
            object["__class"] = class_name;
            for (const auto& field : class_fields) {
                if (!field.shared) {
                    Value value = field.default_value ? field.default_value->eval(runtime) : Value();
                    if (!value.is_null() && !field.type_name.empty() && !runtime.value_matches_type(value, field.type_name)) {
                        throw std::runtime_error(class_name + "." + field.name + " expects " + field.type_name);
                    }
                    object[field.name] = std::move(value);
                }
            }
            return object;
        });
        runtime.register_function(class_name, [class_name, &runtime](const std::vector<Value>& args) -> Value {
            const auto abstract_methods = runtime.class_abstract_methods(class_name);
            if (!abstract_methods.empty()) {
                throw std::runtime_error("cannot instantiate abstract class: " + class_name);
            }
            Value instance = runtime.call_host_function(class_name + ".__new", {});
            if (!args.empty() || runtime.has_function(class_name + ".Init")) {
                runtime.call_method(instance, "Init", args);
            }
            return instance;
        });

        for (const auto& method : methods) {
            if (method.abstract) {
                continue;
            }
            const auto method_name = class_name + "." + method.name;
            std::vector<FunctionParam> method_params;
            if (!method.shared) {
                method_params.push_back(FunctionParam{"SELF", class_name, nullptr});
            }
            method_params.insert(method_params.end(), method.params.begin(), method.params.end());
            const auto method_return_type = method.return_type;
            const auto method_body = method.body;
            runtime.register_function(method_name, [class_name, method_name, method_params, method_return_type, method_body, &runtime](const std::vector<Value>& args) -> Value {
                std::size_t required = 0;
                for (const auto& param : method_params) {
                    if (!param.default_value) {
                        required++;
                    }
                }
                if (args.size() < required || args.size() > method_params.size()) {
                    throw std::runtime_error(method_name + " expects " + std::to_string(required) + " to " + std::to_string(method_params.size()) + " arguments");
                }
                ScopeGuard scope(runtime);
                ClassContextGuard class_context(runtime, class_name);
                for (std::size_t i = 0; i < method_params.size(); ++i) {
                    Value value;
                    if (i < args.size()) {
                        value = args[i];
                    } else if (method_params[i].default_value) {
                        value = method_params[i].default_value->eval(runtime);
                    } else {
                        throw std::runtime_error(method_name + " missing argument: " + method_params[i].name);
                    }
                    if (!method_params[i].type_name.empty() && !runtime.value_matches_type(value, method_params[i].type_name)) {
                        throw std::runtime_error(method_name + " parameter " + method_params[i].name + " expects " + method_params[i].type_name);
                    }
                    runtime.set_global(method_params[i].name, value);
                }
                try {
                    for (const auto& statement : *method_body) {
                        statement->exec(runtime);
                    }
                } catch (const ReturnSignal& ret) {
                    return enforce_return_type(runtime, method_name, method_return_type, ret.value());
                }
                return enforce_return_type(runtime, method_name, method_return_type, {});
            });
        }
    }
    void dump_ast(std::ostream& output, int indent) const override {
        std::string header = "Class " + name;
        if (!parent.empty()) {
            header += " EXTENDS " + parent;
        }
        if (!interfaces.empty()) {
            header += " IMPLEMENTS";
            for (const auto& item : interfaces) {
                header += " " + item;
            }
        }
        ast_line(output, indent, header);
        if (!fields.empty()) {
            ast_line(output, indent + 1, "Fields");
            for (const auto& field : fields) {
                std::string line;
                if (field.access == 1) line += "PROTECTED ";
                if (field.access == 2) line += "PRIVATE ";
                if (field.shared) line += "SHARED ";
                line += field.name;
                if (!field.type_name.empty()) {
                    line += " AS " + field.type_name;
                }
                if (field.default_value) {
                    line += " =";
                }
                ast_line(output, indent + 2, line);
                if (field.default_value) {
                    field.default_value->dump_ast(output, indent + 3);
                }
            }
        }
        if (!methods.empty()) {
            ast_line(output, indent + 1, "Methods");
            for (const auto& method : methods) {
                std::string line;
                if (method.access == 1) line += "PROTECTED ";
                if (method.access == 2) line += "PRIVATE ";
                if (method.shared) line += "SHARED ";
                if (method.abstract) line += "ABSTRACT ";
                line += method.name;
                if (!method.return_type.empty()) {
                    line += " AS " + method.return_type;
                }
                ast_line(output, indent + 2, line);
                if (!method.params.empty()) {
                    ast_line(output, indent + 3, "Params");
                    for (const auto& param : method.params) {
                        ast_line(output, indent + 4, param_summary(param));
                        if (param.default_value) {
                            param.default_value->dump_ast(output, indent + 5);
                        }
                    }
                }
                if (!method.abstract) {
                    ast_line(output, indent + 3, "Body");
                    dump_stmt_list(output, indent + 4, *method.body);
                }
            }
        }
    }

    std::string name;
    std::string parent;
    std::vector<std::string> interfaces;
    std::vector<ClassField> fields;
    std::vector<ClassMethod> methods;
};

struct InterfaceStmt final : Stmt {
    InterfaceStmt(std::string name, std::vector<MethodSignature> methods) : name(std::move(name)), methods(std::move(methods)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        runtime.register_interface(name, methods);
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Interface " + name);
        for (const auto& method : methods) {
            std::string line = "Function " + method.name;
            if (!method.return_type.empty()) {
                line += " AS " + method.return_type;
            }
            ast_line(output, indent + 1, line);
            if (!method.param_types.empty()) {
                ast_line(output, indent + 2, "ParamTypes");
                for (const auto& type : method.param_types) {
                    ast_line(output, indent + 3, type.empty() ? "Any" : type);
                }
            }
        }
    }
    std::string name;
    std::vector<MethodSignature> methods;
};

struct TryStmt final : Stmt {
    TryStmt(std::vector<std::unique_ptr<Stmt>> try_body, std::string error_name, std::vector<std::unique_ptr<Stmt>> catch_body)
        : try_body(std::move(try_body)), error_name(std::move(error_name)), catch_body(std::move(catch_body)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        try {
            for (const auto& statement : try_body) {
                statement->exec(runtime);
            }
        } catch (const ExitSignal&) {
            throw;
        } catch (const ReturnSignal&) {
            throw;
        } catch (const LoopControlSignal&) {
            throw;
        } catch (const std::exception& error) {
            if (!error_name.empty()) {
                runtime.set_global(error_name, Value::Object{{"Message", error.what()}});
            }
            for (const auto& statement : catch_body) {
                statement->exec(runtime);
            }
        }
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Try");
        ast_line(output, indent + 1, "Body");
        dump_stmt_list(output, indent + 2, try_body);
        ast_line(output, indent + 1, error_name.empty() ? "Catch" : "Catch " + error_name);
        dump_stmt_list(output, indent + 2, catch_body);
    }
    std::vector<std::unique_ptr<Stmt>> try_body;
    std::string error_name;
    std::vector<std::unique_ptr<Stmt>> catch_body;
};

struct GotoStmt final : Stmt {
    explicit GotoStmt(int line) : line(line) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        throw GotoSignal(line);
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Goto " + std::to_string(line));
    }
    int line;
};

struct StopStmt final : Stmt {
    void exec(Runtime& runtime) const override {
        runtime.tick();
        throw StopSignal();
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Stop");
    }
};

struct LoopControlStmt final : Stmt {
    LoopControlStmt(LoopControlAction action, LoopControlTarget target) : action(action), target(target) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        throw LoopControlSignal(action, target);
    }
    void dump_ast(std::ostream& output, int indent) const override {
        std::string text = action == LoopControlAction::Exit ? "Exit " : "Continue ";
        text += target == LoopControlTarget::For ? "For" : "While";
        ast_line(output, indent, text);
    }
    LoopControlAction action;
    LoopControlTarget target;
};

struct BlockStmt final : Stmt {
    explicit BlockStmt(std::vector<std::unique_ptr<Stmt>> statements) : statements(std::move(statements)) {}
    void exec(Runtime& runtime) const override {
        for (const auto& statement : statements) {
            statement->exec(runtime);
        }
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Block");
        dump_stmt_list(output, indent + 1, statements);
    }
    std::vector<std::unique_ptr<Stmt>> statements;
};

struct IfStmt final : Stmt {
    IfStmt(std::unique_ptr<Expr> condition, std::vector<std::unique_ptr<Stmt>> then_branch, std::vector<std::unique_ptr<Stmt>> else_branch)
        : condition(std::move(condition)), then_branch(std::move(then_branch)), else_branch(std::move(else_branch)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        const auto& branch = condition->eval(runtime).truthy() ? then_branch : else_branch;
        for (const auto& statement : branch) {
            statement->exec(runtime);
        }
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "If");
        ast_line(output, indent + 1, "Condition");
        condition->dump_ast(output, indent + 2);
        ast_line(output, indent + 1, "Then");
        dump_stmt_list(output, indent + 2, then_branch);
        if (!else_branch.empty()) {
            ast_line(output, indent + 1, "Else");
            dump_stmt_list(output, indent + 2, else_branch);
        }
    }
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> then_branch;
    std::vector<std::unique_ptr<Stmt>> else_branch;
};

struct SelectCaseMatch {
    std::unique_ptr<Expr> start;
    std::unique_ptr<Expr> end;
};

struct SelectBranch {
    std::vector<SelectCaseMatch> matches;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct SelectStmt final : Stmt {
    SelectStmt(std::unique_ptr<Expr> target, std::vector<SelectBranch> branches, std::vector<std::unique_ptr<Stmt>> else_branch)
        : target(std::move(target)), branches(std::move(branches)), else_branch(std::move(else_branch)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        const Value value = target->eval(runtime);
        const std::vector<std::unique_ptr<Stmt>>* selected = nullptr;
        for (const auto& branch : branches) {
            for (const auto& match : branch.matches) {
                if (match.end) {
                    const double target_value = value.as_number();
                    const double start_value = match.start->eval(runtime).as_number();
                    const double end_value = match.end->eval(runtime).as_number();
                    const double lower = std::min(start_value, end_value);
                    const double upper = std::max(start_value, end_value);
                    if (target_value >= lower && target_value <= upper) {
                        selected = &branch.body;
                        break;
                    }
                } else if (values_equal(value, match.start->eval(runtime))) {
                    selected = &branch.body;
                    break;
                }
            }
            if (selected) {
                break;
            }
        }
        if (!selected && !else_branch.empty()) {
            selected = &else_branch;
        }
        if (!selected) {
            return;
        }
        for (const auto& statement : *selected) {
            statement->exec(runtime);
        }
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Select");
        ast_line(output, indent + 1, "Target");
        target->dump_ast(output, indent + 2);
        for (const auto& branch : branches) {
            ast_line(output, indent + 1, "Case");
            for (const auto& match : branch.matches) {
                if (match.end) {
                    ast_line(output, indent + 2, "Range");
                    match.start->dump_ast(output, indent + 3);
                    match.end->dump_ast(output, indent + 3);
                } else {
                    match.start->dump_ast(output, indent + 2);
                }
            }
            ast_line(output, indent + 2, "Body");
            dump_stmt_list(output, indent + 3, branch.body);
        }
        if (!else_branch.empty()) {
            ast_line(output, indent + 1, "Case Else");
            dump_stmt_list(output, indent + 2, else_branch);
        }
    }

    std::unique_ptr<Expr> target;
    std::vector<SelectBranch> branches;
    std::vector<std::unique_ptr<Stmt>> else_branch;
};

struct WhileStmt final : Stmt {
    WhileStmt(std::unique_ptr<Expr> condition, std::vector<std::unique_ptr<Stmt>> body)
        : condition(std::move(condition)), body(std::move(body)) {}
    void exec(Runtime& runtime) const override {
        while (condition->eval(runtime).truthy()) {
            runtime.tick();
            try {
                for (const auto& statement : body) {
                    statement->exec(runtime);
                }
            } catch (const LoopControlSignal& control) {
                if (control.target() != LoopControlTarget::While) {
                    throw;
                }
                if (control.action() == LoopControlAction::Exit) {
                    break;
                }
                continue;
            }
        }
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "While");
        ast_line(output, indent + 1, "Condition");
        condition->dump_ast(output, indent + 2);
        ast_line(output, indent + 1, "Body");
        dump_stmt_list(output, indent + 2, body);
    }
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct DoStmt final : Stmt {
    DoStmt(std::unique_ptr<Expr> pre_condition, bool pre_until, std::unique_ptr<Expr> post_condition, bool post_until, std::vector<std::unique_ptr<Stmt>> body)
        : pre_condition(std::move(pre_condition)), pre_until(pre_until), post_condition(std::move(post_condition)), post_until(post_until), body(std::move(body)) {}
    void exec(Runtime& runtime) const override {
        while (true) {
            if (pre_condition) {
                const bool condition = pre_condition->eval(runtime).truthy();
                if (pre_until ? condition : !condition) {
                    break;
                }
            }
            runtime.tick();
            try {
                for (const auto& statement : body) {
                    statement->exec(runtime);
                }
            } catch (const LoopControlSignal& control) {
                if (control.target() != LoopControlTarget::Do) {
                    throw;
                }
                if (control.action() == LoopControlAction::Exit) {
                    break;
                }
            }
            if (post_condition) {
                const bool condition = post_condition->eval(runtime).truthy();
                if (post_until ? condition : !condition) {
                    break;
                }
            }
        }
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "Do");
        if (pre_condition) {
            ast_line(output, indent + 1, pre_until ? "Until" : "While");
            pre_condition->dump_ast(output, indent + 2);
        }
        ast_line(output, indent + 1, "Body");
        dump_stmt_list(output, indent + 2, body);
        if (post_condition) {
            ast_line(output, indent + 1, post_until ? "Loop Until" : "Loop While");
            post_condition->dump_ast(output, indent + 2);
        }
    }

    std::unique_ptr<Expr> pre_condition;
    bool pre_until = false;
    std::unique_ptr<Expr> post_condition;
    bool post_until = false;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct ForStmt final : Stmt {
    ForStmt(std::string name, std::unique_ptr<Expr> start, std::unique_ptr<Expr> end, std::unique_ptr<Expr> step, std::vector<std::unique_ptr<Stmt>> body)
        : name(std::move(name)), start(std::move(start)), end(std::move(end)), step(std::move(step)), body(std::move(body)) {}
    void exec(Runtime& runtime) const override {
        const double limit = end->eval(runtime).as_number();
        const double increment = step ? step->eval(runtime).as_number() : 1.0;
        if (increment == 0.0) {
            throw std::runtime_error("FOR STEP cannot be zero");
        }
        for (double value = start->eval(runtime).as_number(); increment > 0 ? value <= limit : value >= limit; value += increment) {
            runtime.tick();
            runtime.set_global(name, value);
            try {
                for (const auto& statement : body) {
                    statement->exec(runtime);
                }
            } catch (const LoopControlSignal& control) {
                if (control.target() != LoopControlTarget::For) {
                    throw;
                }
                if (control.action() == LoopControlAction::Exit) {
                    break;
                }
                continue;
            }
        }
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "For " + name);
        ast_line(output, indent + 1, "Start");
        start->dump_ast(output, indent + 2);
        ast_line(output, indent + 1, "End");
        end->dump_ast(output, indent + 2);
        if (step) {
            ast_line(output, indent + 1, "Step");
            step->dump_ast(output, indent + 2);
        }
        ast_line(output, indent + 1, "Body");
        dump_stmt_list(output, indent + 2, body);
    }
    std::string name;
    std::unique_ptr<Expr> start;
    std::unique_ptr<Expr> end;
    std::unique_ptr<Expr> step;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct ForEachStmt final : Stmt {
    ForEachStmt(std::string name, std::unique_ptr<Expr> iterable, std::vector<std::unique_ptr<Stmt>> body)
        : name(std::move(name)), iterable(std::move(iterable)), body(std::move(body)) {}
    void exec(Runtime& runtime) const override {
        const Value values = iterable->eval(runtime);
        if (!values.is_array()) {
            throw std::runtime_error("FOR IN expects an array");
        }
        for (const auto& value : values.as_array()) {
            runtime.tick();
            runtime.set_global(name, value);
            try {
                for (const auto& statement : body) {
                    statement->exec(runtime);
                }
            } catch (const LoopControlSignal& control) {
                if (control.target() != LoopControlTarget::For) {
                    throw;
                }
                if (control.action() == LoopControlAction::Exit) {
                    break;
                }
                continue;
            }
        }
    }
    void dump_ast(std::ostream& output, int indent) const override {
        ast_line(output, indent, "ForEach " + name);
        ast_line(output, indent + 1, "Iterable");
        iterable->dump_ast(output, indent + 2);
        ast_line(output, indent + 1, "Body");
        dump_stmt_list(output, indent + 2, body);
    }
    std::string name;
    std::unique_ptr<Expr> iterable;
    std::vector<std::unique_ptr<Stmt>> body;
};

} // namespace

void Expr::dump_ast(std::ostream& output, int indent) const {
    ast_line(output, indent, "Expr");
}

void Stmt::dump_ast(std::ostream& output, int indent) const {
    std::string header = "Stmt";
    if (line_label >= 0) {
        header += " label=" + std::to_string(line_label);
    }
    header += " @" + std::to_string(source_line) + ":" + std::to_string(source_column);
    ast_line(output, indent, header);
}

Parser::Parser(std::vector<Token> tokens, bool freestanding_runtime_none)
    : tokens_(std::move(tokens)), freestanding_runtime_none_(freestanding_runtime_none) {}

std::vector<std::unique_ptr<Stmt>> Parser::parse() {
    std::vector<std::unique_ptr<Stmt>> statements;
    skip_newlines();
    while (!at_end()) {
        statements.push_back(statement());
        skip_newlines();
    }
    return statements;
}

bool Parser::at_end() const {
    return peek().type == TokenType::End;
}

const Token& Parser::peek() const {
    return tokens_[current_];
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

const Token& Parser::advance() {
    if (!at_end()) {
        current_++;
    }
    return previous();
}

bool Parser::check(TokenType type) const {
    return !at_end() && peek().type == type;
}

bool Parser::match(TokenType type) {
    if (!check(type)) {
        return false;
    }
    advance();
    return true;
}

bool Parser::match_any(std::initializer_list<TokenType> types) {
    for (TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) {
        return advance();
    }
    throw std::runtime_error(token_error(peek(), message));
}

void Parser::skip_newlines() {
    while (match(TokenType::Newline) || match(TokenType::Colon)) {}
}

void Parser::skip_line_number() {
    if (check(TokenType::Number) && current_ + 1 < tokens_.size() && tokens_[current_ + 1].type != TokenType::End) {
        advance();
    }
}

std::string Parser::parse_type_name(const std::string& message) {
    const Token& token = peek();
    if (match(TokenType::Identifier)) {
        return previous().lexeme;
    }
    throw std::runtime_error(token_error(token, message));
}

// Statically checks a fixed-width-typed LET declaration's initializer when it
// is a plain integer literal (optionally negated). Non-literal initializers
// (identifiers, calls, parenthesized expressions, ...) are not checked here;
// they are left for later semantic-analysis/A-MIR work. See
// docs/systems/uefi-target.md section 3 for the type contract this enforces.
void Parser::validate_fixed_width_initializer(const Token& variable, const std::string& type_name) {
    const auto fixed_type = systems::lookup_fixed_width_type(type_name);
    if (!fixed_type) {
        return;
    }

    std::size_t index = current_;
    bool negative = false;
    if (index < tokens_.size() && tokens_[index].type == TokenType::Minus) {
        negative = true;
        ++index;
    }
    if (index >= tokens_.size() || tokens_[index].type != TokenType::Number) {
        return;
    }

    const Token& literal = tokens_[index];
    if (literal.lexeme.find('.') != std::string::npos) {
        throw std::runtime_error(token_error(literal,
            type_name + " requires an integer literal; received " + literal.lexeme +
            ". Use a whole-number literal for a fixed-width type."));
    }
    if (literal.lexeme.size() > 1 && literal.lexeme[0] == '0' &&
        (literal.lexeme[1] == 'x' || literal.lexeme[1] == 'X' || literal.lexeme[1] == 'b' || literal.lexeme[1] == 'B')) {
        // Hex/binary literal range-checking is not implemented yet (documented
        // limitation); only decimal literals are statically range-checked in
        // this milestone.
        return;
    }

    const auto magnitude = systems::parse_u64_decimal_exact(literal.lexeme);
    if (!magnitude) {
        throw std::runtime_error(token_error(literal,
            type_name + " literal is too large to represent in any 64-bit systems type: received " +
            std::string(negative ? "-" : "") + literal.lexeme + "."));
    }

    if (fixed_type->is_bool) {
        if (negative || *magnitude > 1) {
            throw std::runtime_error(token_error(literal,
                "BOOL literal must be 0 or 1; received " + std::string(negative ? "-" : "") + literal.lexeme + "."));
        }
        return;
    }

    if (fixed_type->is_pointer) {
        throw std::runtime_error(token_error(literal,
            "PTR cannot be initialized from an integer literal; assign a system-provided handle or pointer value instead."));
    }

    if (negative) {
        if (!fixed_type->is_signed || *magnitude > fixed_type->negative_magnitude_max) {
            std::string message;
            if (!fixed_type->is_signed) {
                message = type_name + " cannot represent negative values: expected 0.." +
                    std::to_string(fixed_type->positive_max) + ", received -" + literal.lexeme +
                    ". Use a signed type such as I" + type_name.substr(1) + " for negative values.";
            } else {
                message = type_name + " literal out of range: expected -" +
                    std::to_string(fixed_type->negative_magnitude_max) + ".." +
                    std::to_string(fixed_type->positive_max) + ", received -" + literal.lexeme + ".";
                const std::string wider = next_wider_type_name(type_name);
                message += wider == type_name
                    ? " No wider fixed-width signed type is available; this literal cannot be represented."
                    : " Use a wider signed type such as " + wider + ".";
            }
            throw std::runtime_error(token_error(literal, message));
        }
        return;
    }

    if (*magnitude > fixed_type->positive_max) {
        std::string message = type_name + " literal out of range: expected 0.." +
            std::to_string(fixed_type->positive_max) + ", received " + literal.lexeme + ".";
        const std::string wider = next_wider_type_name(type_name);
        message += wider == type_name
            ? " No wider fixed-width type is available; this literal cannot be represented."
            : " Use a value within range or a wider type such as " + wider + ".";
        throw std::runtime_error(token_error(literal, message));
    }
}

std::vector<FunctionParam> Parser::parameter_list() {
    consume(TokenType::LeftParen, "expected '(' after function name");
    std::vector<FunctionParam> params;
    bool saw_default = false;
    if (!check(TokenType::RightParen)) {
        do {
            const Token param = consume(TokenType::Identifier, "expected parameter name");
            std::string type_name;
            if (match(TokenType::As)) {
                type_name = parse_type_name("expected type name after AS");
            }
            std::shared_ptr<Expr> default_value;
            if (match(TokenType::Equal)) {
                saw_default = true;
                default_value = std::shared_ptr<Expr>(expression().release());
            } else if (saw_default) {
                throw std::runtime_error(token_error(param, "parameters after a default value must also have defaults"));
            }
            params.push_back(FunctionParam{param.lexeme, std::move(type_name), std::move(default_value)});
        } while (match(TokenType::Comma));
    }
    consume(TokenType::RightParen, "expected ')' after parameters");
    return params;
}

Parser::StmtPtr Parser::statement() {
    int label = -1;
    if (check(TokenType::Number) && current_ + 1 < tokens_.size() && tokens_[current_ + 1].type != TokenType::End) {
        label = static_cast<int>(advance().number);
    }
    const Token statement_token = peek();
    if (check(TokenType::Newline) || check(TokenType::End)) {
        auto parsed = std::make_unique<NoOpStmt>();
        parsed->line_label = label;
        parsed->source_line = statement_token.line;
        parsed->source_column = statement_token.column;
        return parsed;
    }
    StmtPtr parsed;
    if (match(TokenType::Print)) {
        parsed = print_statement();
    } else if (match(TokenType::Run)) {
        std::vector<ExprPtr> args;
        args.push_back(expression());
        parsed = std::make_unique<PrintStmt>(std::make_unique<CallExpr>("RUN", std::move(args)));
    } else if (match(TokenType::Let)) {
        parsed = assignment_statement(true);
    } else if (match(TokenType::If)) {
        parsed = if_statement();
    } else if (match(TokenType::Select)) {
        parsed = select_statement();
    } else if (match(TokenType::While)) {
        parsed = while_statement();
    } else if (match(TokenType::Do)) {
        parsed = do_statement();
    } else if (match(TokenType::For)) {
        parsed = for_statement();
    } else if (match(TokenType::Function)) {
        parsed = function_statement();
    } else if (match(TokenType::Class)) {
        parsed = class_statement();
    } else if (match(TokenType::Interface)) {
        parsed = interface_statement();
    } else if (match(TokenType::Return)) {
        parsed = return_statement();
    } else if (match(TokenType::Try)) {
        parsed = try_statement();
    } else if (match(TokenType::Goto)) {
        parsed = goto_statement();
    } else if (match(TokenType::Stop)) {
        parsed = stop_statement();
    } else if (match(TokenType::Flags)) {
        parsed = flags_statement();
    } else if (check(TokenType::Identifier)) {
        const std::string statement_name = uppercase(peek().lexeme);
        if ((statement_name == "EXIT" || statement_name == "CONTINUE") && current_ + 1 < tokens_.size() &&
            (tokens_[current_ + 1].type == TokenType::For || tokens_[current_ + 1].type == TokenType::While || tokens_[current_ + 1].type == TokenType::Do)) {
            parsed = loop_control_statement();
        } else if (current_ + 1 < tokens_.size() && (tokens_[current_ + 1].type == TokenType::PlusEqual ||
            tokens_[current_ + 1].type == TokenType::MinusEqual || tokens_[current_ + 1].type == TokenType::StarEqual ||
            tokens_[current_ + 1].type == TokenType::SlashEqual || tokens_[current_ + 1].type == TokenType::AmpersandEqual ||
            tokens_[current_ + 1].type == TokenType::PipeEqual || tokens_[current_ + 1].type == TokenType::CaretEqual ||
            tokens_[current_ + 1].type == TokenType::ShiftLeftEqual || tokens_[current_ + 1].type == TokenType::ShiftRightEqual)) {
            parsed = compound_assignment_statement();
        } else if (current_ + 1 < tokens_.size() && (tokens_[current_ + 1].type == TokenType::Add ||
            tokens_[current_ + 1].type == TokenType::Remove || tokens_[current_ + 1].type == TokenType::Toggle)) {
            parsed = flag_operation_statement();
        } else if (current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == TokenType::LeftParen) {
            parsed = expression_statement();
        } else {
            parsed = assignment_statement(false);
        }
    } else {
        throw std::runtime_error(token_error(peek(), "expected statement"));
    }
    parsed->line_label = label;
    parsed->source_line = statement_token.line;
    parsed->source_column = statement_token.column;
    return parsed;
}

Parser::StmtPtr Parser::print_statement() {
    if (freestanding_runtime_none_) {
        throw std::runtime_error(token_error(previous(),
            "UEFI target does not provide the standard console runtime.\n"
            "Use UEFI.SystemTable.ConsoleOut or select a hosted runtime profile."));
    }
    auto value = expression();
    return std::make_unique<PrintStmt>(std::move(value));
}

Parser::StmtPtr Parser::assignment_statement(bool had_let) {
    const Token name = consume(TokenType::Identifier, "expected variable name");
    std::string type_name;
    if (had_let && match(TokenType::As)) {
        type_name = parse_type_name("expected type name after AS");
    }
    std::vector<ExprPtr> indexes;
    while (match(TokenType::LeftBracket)) {
        indexes.push_back(expression());
        consume(TokenType::RightBracket, "expected ']' after index");
    }
    consume(TokenType::Equal, "expected '=' after variable name");
    if (!type_name.empty()) {
        validate_fixed_width_initializer(name, type_name);
    }
    return std::make_unique<AssignStmt>(name.lexeme, std::move(indexes), expression(), type_name);
}

Parser::StmtPtr Parser::compound_assignment_statement() {
    const Token name = consume(TokenType::Identifier, "expected variable name");
    const Token op = advance();
    TokenType binary_op = TokenType::Plus;
    switch (op.type) {
        case TokenType::PlusEqual:
            binary_op = TokenType::Plus;
            break;
        case TokenType::MinusEqual:
            binary_op = TokenType::Minus;
            break;
        case TokenType::StarEqual:
            binary_op = TokenType::Star;
            break;
        case TokenType::SlashEqual:
            binary_op = TokenType::Slash;
            break;
        case TokenType::AmpersandEqual:
            binary_op = TokenType::Ampersand;
            break;
        case TokenType::PipeEqual:
            binary_op = TokenType::Pipe;
            break;
        case TokenType::CaretEqual:
            binary_op = TokenType::Caret;
            break;
        case TokenType::ShiftLeftEqual:
            binary_op = TokenType::ShiftLeft;
            break;
        case TokenType::ShiftRightEqual:
            binary_op = TokenType::ShiftRight;
            break;
        default:
            throw std::runtime_error(token_error(op, "expected compound assignment operator"));
    }
    return std::make_unique<CompoundAssignStmt>(name.lexeme, binary_op, expression());
}

Parser::StmtPtr Parser::flag_operation_statement() {
    const Token name = consume(TokenType::Identifier, "expected flag variable name");
    const Token op = advance();
    return std::make_unique<FlagOperationStmt>(name.lexeme, op.type, expression());
}

Parser::StmtPtr Parser::flags_statement() {
    const Token name = consume(TokenType::Identifier, "expected FLAGS block name");
    skip_newlines();
    std::vector<std::pair<std::string, ExprPtr>> fields;
    while (!at_end() && !check(TokenType::EndKeyword)) {
        skip_line_number();
        const Token field = consume(TokenType::Identifier, "expected flag name");
        consume(TokenType::Equal, "expected '=' after flag name");
        fields.emplace_back(field.lexeme, expression());
        skip_newlines();
    }
    consume(TokenType::EndKeyword, "expected END FLAGS");
    consume(TokenType::Flags, "expected FLAGS after END");
    return std::make_unique<FlagsStmt>(name.lexeme, std::move(fields));
}

Parser::StmtPtr Parser::expression_statement() {
    return std::make_unique<ExprStmt>(expression());
}

Parser::StmtPtr Parser::if_statement() {
    auto condition = expression();
    consume(TokenType::Then, "expected THEN after IF condition");
    if (!check(TokenType::Newline) && !check(TokenType::End)) {
        std::vector<StmtPtr> then_branch;
        std::vector<StmtPtr> else_branch;
        do {
            if (check(TokenType::Else) || check(TokenType::Newline) || check(TokenType::End)) {
                break;
            }
            then_branch.push_back(statement());
            if (check(TokenType::Else) || check(TokenType::Newline) || check(TokenType::End)) {
                break;
            }
        } while (match(TokenType::Colon));
        if (match(TokenType::Else)) {
            do {
                if (check(TokenType::Newline) || check(TokenType::End)) {
                    break;
                }
                else_branch.push_back(statement());
                if (check(TokenType::Newline) || check(TokenType::End)) {
                    break;
                }
            } while (match(TokenType::Colon));
        }
        return std::make_unique<IfStmt>(std::move(condition), std::move(then_branch), std::move(else_branch));
    }
    skip_newlines();
    auto then_branch = block_until({TokenType::Else, TokenType::EndKeyword});
    std::vector<StmtPtr> else_branch;
    if (match(TokenType::Else)) {
        skip_newlines();
        else_branch = block_until({TokenType::EndKeyword});
    }
    consume(TokenType::EndKeyword, "expected END IF");
    consume(TokenType::If, "expected IF after END");
    return std::make_unique<IfStmt>(std::move(condition), std::move(then_branch), std::move(else_branch));
}

Parser::StmtPtr Parser::select_statement() {
    consume(TokenType::Case, "expected CASE after SELECT");
    auto target = expression();
    skip_newlines();

    std::vector<SelectBranch> branches;
    std::vector<StmtPtr> else_branch;
    while (!at_end() && !check(TokenType::EndKeyword)) {
        skip_line_number();
        if (check(TokenType::EndKeyword)) {
            break;
        }
        consume(TokenType::Case, "expected CASE in SELECT block");
        if (match(TokenType::Else)) {
            skip_newlines();
            else_branch = block_until({TokenType::EndKeyword});
            break;
        }

        std::vector<SelectCaseMatch> matches;
        do {
            auto start = expression();
            ExprPtr end;
            if (match(TokenType::To)) {
                end = expression();
            }
            matches.push_back(SelectCaseMatch{std::move(start), std::move(end)});
        } while (match(TokenType::Comma));
        skip_newlines();
        branches.push_back(SelectBranch{std::move(matches), block_until({TokenType::Case, TokenType::EndKeyword})});
    }

    consume(TokenType::EndKeyword, "expected END SELECT");
    consume(TokenType::Select, "expected SELECT after END");
    return std::make_unique<SelectStmt>(std::move(target), std::move(branches), std::move(else_branch));
}

Parser::StmtPtr Parser::while_statement() {
    auto condition = expression();
    skip_newlines();
    auto body = block_until({TokenType::Wend});
    consume(TokenType::Wend, "expected WEND");
    return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
}

Parser::StmtPtr Parser::do_statement() {
    ExprPtr pre_condition;
    bool pre_until = false;
    if (match(TokenType::While)) {
        pre_condition = expression();
    } else if (match(TokenType::Until)) {
        pre_until = true;
        pre_condition = expression();
    }

    skip_newlines();
    auto body = block_until({TokenType::Loop});
    consume(TokenType::Loop, "expected LOOP");

    ExprPtr post_condition;
    bool post_until = false;
    if (match(TokenType::While)) {
        post_condition = expression();
    } else if (match(TokenType::Until)) {
        post_until = true;
        post_condition = expression();
    }

    return std::make_unique<DoStmt>(std::move(pre_condition), pre_until, std::move(post_condition), post_until, std::move(body));
}

Parser::StmtPtr Parser::for_statement() {
    Token name;
    if (check(TokenType::Identifier) || check(TokenType::Run)) {
        name = advance();
    } else {
        name = consume(TokenType::Identifier, "expected loop variable");
    }
    if (match(TokenType::In)) {
        auto iterable = expression();
        skip_newlines();
        auto body = block_until({TokenType::Next});
        consume(TokenType::Next, "expected NEXT");
        if (check(TokenType::Identifier) || check(TokenType::Run)) {
            advance();
        }
        return std::make_unique<ForEachStmt>(name.lexeme, std::move(iterable), std::move(body));
    }
    consume(TokenType::Equal, "expected '=' after loop variable");
    auto start = expression();
    consume(TokenType::To, "expected TO in FOR statement");
    auto end = expression();
    ExprPtr step;
    if (match(TokenType::Step)) {
        step = expression();
    }
    skip_newlines();
    auto body = block_until({TokenType::Next});
    consume(TokenType::Next, "expected NEXT");
    if (check(TokenType::Identifier)) {
        advance();
    }
    return std::make_unique<ForStmt>(name.lexeme, std::move(start), std::move(end), std::move(step), std::move(body));
}

Parser::StmtPtr Parser::function_statement() {
    const Token name = consume(TokenType::Identifier, "expected function name");
    auto params = parameter_list();
    std::string return_type;
    if (match(TokenType::As)) {
        return_type = parse_type_name("expected return type after AS");
    }
    skip_newlines();
    auto body = std::make_shared<FunctionStmt::Body>(block_until({TokenType::EndKeyword}));
    consume(TokenType::EndKeyword, "expected END FUNCTION");
    consume(TokenType::Function, "expected FUNCTION after END");
    return std::make_unique<FunctionStmt>(name.lexeme, std::move(params), std::move(return_type), std::move(body));
}

Parser::StmtPtr Parser::class_statement() {
    const Token name = consume(TokenType::Identifier, "expected class name");
    std::string parent;
    if (match(TokenType::Extends)) {
        parent = consume(TokenType::Identifier, "expected parent class name after EXTENDS").lexeme;
    }
    std::vector<std::string> interfaces;
    if (match(TokenType::Implements)) {
        do {
            interfaces.push_back(consume(TokenType::Identifier, "expected interface name after IMPLEMENTS").lexeme);
        } while (match(TokenType::Comma));
    }
    skip_newlines();
    std::vector<ClassField> fields;
    std::vector<ClassMethod> methods;
    while (!at_end() && !check(TokenType::EndKeyword)) {
        skip_line_number();
        if (check(TokenType::EndKeyword)) {
            break;
        }
        bool shared = false;
        int access = 0;
        bool abstract = false;
        bool modifiers = true;
        while (modifiers) {
            if (match(TokenType::Shared)) {
                shared = true;
            } else if (match(TokenType::Abstract)) {
                abstract = true;
            } else if (match(TokenType::Private)) {
                access = 2;
            } else if (match(TokenType::Protected)) {
                access = 1;
            } else if (match(TokenType::Public)) {
                access = 0;
            } else {
                modifiers = false;
            }
        }
        if (match(TokenType::Function) || match(TokenType::Constructor)) {
            const bool constructor = previous().type == TokenType::Constructor;
            Token method;
            method.type = TokenType::Identifier;
            method.lexeme = "Init";
            method.line = previous().line;
            method.column = previous().column;
            if (!constructor) {
                method = consume(TokenType::Identifier, "expected method name");
            }
            auto params = parameter_list();
            std::string return_type;
            if (match(TokenType::As)) {
                if (constructor) {
                    throw std::runtime_error(token_error(previous(), "CONSTRUCTOR cannot declare a return type"));
                }
                return_type = parse_type_name("expected return type after AS");
            }
            if (abstract) {
                methods.push_back(ClassMethod{method.lexeme, parent, std::move(params), std::move(return_type), std::make_shared<FunctionBody>(), shared, access, true});
                skip_newlines();
                continue;
            }
            skip_newlines();
            const std::string previous_super_class = current_super_class_;
            current_super_class_ = parent;
            auto body = std::make_shared<FunctionBody>(block_until({TokenType::EndKeyword}));
            current_super_class_ = previous_super_class;
            consume(TokenType::EndKeyword, constructor ? "expected END CONSTRUCTOR" : "expected END FUNCTION");
            consume(constructor ? TokenType::Constructor : TokenType::Function, constructor ? "expected CONSTRUCTOR after END" : "expected FUNCTION after END");
            methods.push_back(ClassMethod{method.lexeme, parent, std::move(params), std::move(return_type), std::move(body), shared, access, false});
            skip_newlines();
            continue;
        }

        const Token field = consume(TokenType::Identifier, "expected class field, FUNCTION, or CONSTRUCTOR");
        std::string type_name;
        if (match(TokenType::As)) {
            type_name = parse_type_name("expected field type after AS");
        }
        std::shared_ptr<Expr> default_value;
        if (match(TokenType::Equal)) {
            default_value = std::shared_ptr<Expr>(expression().release());
        }
        fields.push_back(ClassField{field.lexeme, std::move(type_name), std::move(default_value), shared, access});
        skip_newlines();
    }
    consume(TokenType::EndKeyword, "expected END CLASS");
    consume(TokenType::Class, "expected CLASS after END");
    return std::make_unique<ClassStmt>(name.lexeme, parent, std::move(interfaces), std::move(fields), std::move(methods));
}

Parser::StmtPtr Parser::interface_statement() {
    const Token name = consume(TokenType::Identifier, "expected interface name");
    skip_newlines();
    std::vector<MethodSignature> methods;
    while (!at_end() && !check(TokenType::EndKeyword)) {
        skip_line_number();
        if (check(TokenType::EndKeyword)) {
            break;
        }
        consume(TokenType::Function, "expected FUNCTION in interface");
        const Token method = consume(TokenType::Identifier, "expected interface method name");
        auto params = parameter_list();
        std::vector<std::string> param_types;
        param_types.reserve(params.size());
        for (const auto& param : params) {
            param_types.push_back(param.type_name);
        }
        std::string return_type;
        if (match(TokenType::As)) {
            return_type = parse_type_name("expected return type after AS");
        }
        methods.push_back(MethodSignature{method.lexeme, std::move(param_types), std::move(return_type)});
        skip_newlines();
    }
    consume(TokenType::EndKeyword, "expected END INTERFACE");
    consume(TokenType::Interface, "expected INTERFACE after END");
    return std::make_unique<InterfaceStmt>(name.lexeme, std::move(methods));
}

Parser::StmtPtr Parser::return_statement() {
    if (check(TokenType::Newline) || check(TokenType::End) || check(TokenType::EndKeyword)) {
        return std::make_unique<ReturnStmt>(nullptr);
    }
    return std::make_unique<ReturnStmt>(expression());
}

Parser::StmtPtr Parser::try_statement() {
    skip_newlines();
    auto try_body = block_until({TokenType::Catch, TokenType::EndKeyword});
    std::string error_name;
    std::vector<StmtPtr> catch_body;
    if (match(TokenType::Catch)) {
        if (check(TokenType::Identifier)) {
            error_name = advance().lexeme;
        }
        skip_newlines();
        catch_body = block_until({TokenType::EndKeyword});
    }
    consume(TokenType::EndKeyword, "expected END TRY");
    consume(TokenType::Try, "expected TRY after END");
    return std::make_unique<TryStmt>(std::move(try_body), std::move(error_name), std::move(catch_body));
}

Parser::StmtPtr Parser::goto_statement() {
    const Token target = consume(TokenType::Number, "expected line number after GOTO");
    return std::make_unique<GotoStmt>(static_cast<int>(target.number));
}

Parser::StmtPtr Parser::stop_statement() {
    return std::make_unique<StopStmt>();
}

Parser::StmtPtr Parser::loop_control_statement() {
    const Token action = consume(TokenType::Identifier, "expected EXIT or CONTINUE");
    const std::string action_name = uppercase(action.lexeme);
    LoopControlAction control_action;
    if (action_name == "EXIT") {
        control_action = LoopControlAction::Exit;
    } else if (action_name == "CONTINUE") {
        control_action = LoopControlAction::Continue;
    } else {
        throw std::runtime_error(token_error(action, "expected EXIT or CONTINUE"));
    }
    if (match(TokenType::For)) {
        return std::make_unique<LoopControlStmt>(control_action, LoopControlTarget::For);
    }
    if (match(TokenType::Do)) {
        return std::make_unique<LoopControlStmt>(control_action, LoopControlTarget::Do);
    }
    consume(TokenType::While, "expected FOR, WHILE, or DO after " + action.lexeme);
    return std::make_unique<LoopControlStmt>(control_action, LoopControlTarget::While);
}

std::vector<Parser::StmtPtr> Parser::block_until(std::initializer_list<TokenType> terminators) {
    std::vector<StmtPtr> statements;
    skip_newlines();
    while (!at_end()) {
        skip_line_number();
        for (TokenType terminator : terminators) {
            if (check(terminator)) {
                return statements;
            }
        }
        statements.push_back(statement());
        skip_newlines();
    }
    return statements;
}

Parser::ExprPtr Parser::expression() {
    return logical_or();
}

Parser::ExprPtr Parser::logical_or() {
    auto expr = logical_and();
    while (match_any({TokenType::LogicalOr, TokenType::OrElse})) {
        const TokenType op = previous().type;
        expr = std::make_unique<LogicalExpr>(std::move(expr), op, logical_and());
    }
    return expr;
}

Parser::ExprPtr Parser::logical_and() {
    auto expr = bit_or();
    while (match_any({TokenType::LogicalAnd, TokenType::AndAlso})) {
        const TokenType op = previous().type;
        expr = std::make_unique<LogicalExpr>(std::move(expr), op, bit_or());
    }
    return expr;
}

Parser::ExprPtr Parser::bit_or() {
    auto expr = bit_xor();
    while (match_any({TokenType::Pipe, TokenType::BitOr})) {
        const TokenType op = previous().type;
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, bit_xor());
    }
    return expr;
}

Parser::ExprPtr Parser::bit_xor() {
    auto expr = bit_and();
    while (match_any({TokenType::Caret, TokenType::BitXor})) {
        const TokenType op = previous().type;
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, bit_and());
    }
    return expr;
}

Parser::ExprPtr Parser::bit_and() {
    auto expr = equality();
    while (match_any({TokenType::Ampersand, TokenType::BitAnd})) {
        const TokenType op = previous().type;
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, equality());
    }
    return expr;
}

Parser::ExprPtr Parser::equality() {
    auto expr = comparison();
    while (match_any({TokenType::Equal, TokenType::NotEqual, TokenType::In, TokenType::Has})) {
        const TokenType op = previous().type;
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, comparison());
    }
    return expr;
}

Parser::ExprPtr Parser::comparison() {
    auto expr = shift();
    while (match_any({TokenType::Less, TokenType::LessEqual, TokenType::Greater, TokenType::GreaterEqual, TokenType::Contains})) {
        const TokenType op = previous().type;
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, shift());
    }
    return expr;
}

Parser::ExprPtr Parser::shift() {
    auto expr = term();
    while (match_any({TokenType::ShiftLeft, TokenType::ShiftRight, TokenType::ShiftLeftWord, TokenType::ShiftRightWord})) {
        const TokenType op = previous().type;
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, term());
    }
    return expr;
}

Parser::ExprPtr Parser::term() {
    auto expr = factor();
    while (match_any({TokenType::Plus, TokenType::Minus})) {
        const TokenType op = previous().type;
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, factor());
    }
    return expr;
}

Parser::ExprPtr Parser::factor() {
    auto expr = unary();
    while (match_any({TokenType::Star, TokenType::Slash, TokenType::Mod}) || (check(TokenType::Identifier) && uppercase(peek().lexeme) == "MOD" && (advance(), true))) {
        const TokenType op = uppercase(previous().lexeme) == "MOD" ? TokenType::Mod : previous().type;
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, unary());
    }
    return expr;
}

Parser::ExprPtr Parser::unary() {
    if (match(TokenType::Minus)) {
        return std::make_unique<UnaryExpr>(TokenType::Minus, unary());
    }
    if (match(TokenType::Tilde)) {
        return std::make_unique<UnaryExpr>(TokenType::Tilde, unary());
    }
    if (match(TokenType::Bang)) {
        return std::make_unique<UnaryExpr>(TokenType::Bang, unary());
    }
    if (match(TokenType::BitNot)) {
        return std::make_unique<UnaryExpr>(TokenType::BitNot, unary());
    }
    return call();
}

Parser::ExprPtr Parser::call() {
    auto expr = primary();
    while (true) {
        if (match(TokenType::LeftParen)) {
            auto* variable = dynamic_cast<VariableExpr*>(expr.get());
            if (!variable) {
                throw std::runtime_error(token_error(previous(), "only named host functions can be called"));
            }
            std::vector<ExprPtr> args;
            skip_newlines();
            if (!check(TokenType::RightParen)) {
                while (true) {
                    args.push_back(expression());
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
            consume(TokenType::RightParen, "expected ')' after arguments");
            const auto dot = variable->name.find('.');
            if (freestanding_runtime_none_) {
                const std::string first_segment = dot != std::string::npos ? variable->name.substr(0, dot) : variable->name;
                if (is_hosted_runtime_namespace(first_segment)) {
                    throw std::runtime_error(token_error(previous(),
                        freestanding_diagnostic(first_segment + ".* calls",
                            "Provide an explicit systems binding instead of the hosted " + first_segment + " runtime, "
                            "or select a hosted runtime profile.")));
                }
            }
            if (dot != std::string::npos) {
                if (uppercase(variable->name.substr(0, dot)) == "SUPER") {
                    if (current_super_class_.empty()) {
                        throw std::runtime_error(token_error(previous(), "SUPER can only be used inside a class method with EXTENDS"));
                    }
                    expr = std::make_unique<SuperCallExpr>(current_super_class_, variable->name.substr(dot + 1), std::move(args));
                } else {
                    expr = std::make_unique<MethodCallExpr>(variable->name, variable->name.substr(0, dot), variable->name.substr(dot + 1), std::move(args));
                }
            } else {
                expr = std::make_unique<CallExpr>(variable->name, std::move(args));
            }
        } else if (match(TokenType::LeftBracket)) {
            skip_newlines();
            auto index = expression();
            skip_newlines();
            consume(TokenType::RightBracket, "expected ']' after index");
            expr = std::make_unique<IndexExpr>(std::move(expr), std::move(index));
        } else {
            break;
        }
    }
    return expr;
}

Parser::ExprPtr Parser::primary() {
    if (match(TokenType::FalseKeyword)) {
        return std::make_unique<LiteralExpr>(false);
    }
    if (match(TokenType::TrueKeyword)) {
        return std::make_unique<LiteralExpr>(true);
    }
    if (match(TokenType::NullKeyword)) {
        return std::make_unique<LiteralExpr>(Value());
    }
    if (match(TokenType::Number)) {
        return std::make_unique<LiteralExpr>(previous().number);
    }
    if (match(TokenType::String)) {
        return std::make_unique<LiteralExpr>(previous().lexeme);
    }
    if (match(TokenType::InterpolatedString)) {
        return std::make_unique<InterpolatedStringExpr>(previous().lexeme);
    }
    if (match(TokenType::Identifier)) {
        return std::make_unique<VariableExpr>(previous().lexeme);
    }
    if (match(TokenType::Run)) {
        return std::make_unique<VariableExpr>("RUN");
    }
    if (match(TokenType::Implements)) {
        return std::make_unique<VariableExpr>("IMPLEMENTS");
    }
    if (match(TokenType::LeftParen)) {
        skip_newlines();
        auto expr = expression();
        skip_newlines();
        consume(TokenType::RightParen, "expected ')' after expression");
        return expr;
    }
    if (match(TokenType::LeftBracket)) {
        std::vector<ExprPtr> items;
        skip_newlines();
        if (!check(TokenType::RightBracket)) {
            while (true) {
                items.push_back(expression());
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
        consume(TokenType::RightBracket, "expected ']' after array literal");
        return std::make_unique<ArrayExpr>(std::move(items));
    }
    if (match(TokenType::LeftBrace)) {
        std::vector<std::pair<std::string, ExprPtr>> fields;
        skip_newlines();
        if (!check(TokenType::RightBrace)) {
            while (true) {
                std::string key;
                if (match(TokenType::String) || match(TokenType::Identifier)) {
                    key = previous().lexeme;
                } else {
                    throw std::runtime_error(token_error(peek(), "expected object field name"));
                }
                consume(TokenType::Colon, "expected ':' after object field name");
                skip_newlines();
                fields.emplace_back(std::move(key), expression());
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
        consume(TokenType::RightBrace, "expected '}' after object literal");
        return std::make_unique<ObjectExpr>(std::move(fields));
    }
    throw std::runtime_error(token_error(peek(), "expected expression"));
}

} // namespace arco
