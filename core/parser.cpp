#include "parser.hpp"
#include "lexer.hpp"

#include <sstream>
#include <stdexcept>
#include <memory>
#include <utility>

namespace arco {

namespace {

long long as_int(const Value& value) {
    return static_cast<long long>(value.as_number());
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

struct LiteralExpr final : Expr {
    explicit LiteralExpr(Value value) : value(std::move(value)) {}
    Value eval(Runtime&) const override { return value; }
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
    std::string text;
};

struct VariableExpr final : Expr {
    explicit VariableExpr(std::string name) : name(std::move(name)) {}
    Value eval(Runtime& runtime) const override { return runtime.get_global(name); }
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
        return value;
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

    std::unique_ptr<Expr> left;
    TokenType op;
    std::unique_ptr<Expr> right;
};

struct CallExpr final : Expr {
    CallExpr(std::string name, std::vector<std::unique_ptr<Expr>> args)
        : name(std::move(name)), args(std::move(args)) {}

    Value eval(Runtime& runtime) const override {
        std::vector<Value> values;
        values.reserve(args.size());
        for (const auto& arg : args) {
            values.push_back(arg->eval(runtime));
        }
        return runtime.call_host_function(name, values);
    }

    std::string name;
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
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
};

struct PrintStmt final : Stmt {
    explicit PrintStmt(std::unique_ptr<Expr> expr) : expr(std::move(expr)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        runtime.output() << expr->eval(runtime).to_string() << '\n';
    }
    std::unique_ptr<Expr> expr;
};

struct AssignStmt final : Stmt {
    AssignStmt(std::string name, std::vector<std::unique_ptr<Expr>> indexes, std::unique_ptr<Expr> expr)
        : name(std::move(name)), indexes(std::move(indexes)), expr(std::move(expr)) {}
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
    std::string name;
    std::vector<std::unique_ptr<Expr>> indexes;
    std::unique_ptr<Expr> expr;
};

struct CompoundAssignStmt final : Stmt {
    CompoundAssignStmt(std::string name, TokenType op, std::unique_ptr<Expr> expr) : name(std::move(name)), op(op), expr(std::move(expr)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        const Value current = runtime.get_global(name);
        const Value next = apply_binary(op, current, expr->eval(runtime));
        runtime.set_global(name, next);
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
    std::string name;
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
};

struct ExprStmt final : Stmt {
    explicit ExprStmt(std::unique_ptr<Expr> expr) : expr(std::move(expr)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        expr->eval(runtime);
    }
    std::unique_ptr<Expr> expr;
};

struct NoOpStmt final : Stmt {
    void exec(Runtime& runtime) const override {
        runtime.tick();
    }
};

struct ReturnStmt final : Stmt {
    explicit ReturnStmt(std::unique_ptr<Expr> value) : value(std::move(value)) {}
    void exec(Runtime& runtime) const override {
        runtime.tick();
        throw ReturnSignal(value ? value->eval(runtime) : Value());
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

struct FunctionParam {
    std::string name;
    std::shared_ptr<Expr> default_value;
};

struct FunctionStmt final : Stmt {
    using Body = std::vector<std::unique_ptr<Stmt>>;

    FunctionStmt(std::string name, std::vector<FunctionParam> params, std::shared_ptr<Body> body)
        : name(std::move(name)), params(std::move(params)), body(std::move(body)) {}

    void exec(Runtime& runtime) const override {
        runtime.tick();
        const auto fn_name = name;
        const auto fn_params = params;
        const auto fn_body = body;
        runtime.register_function(fn_name, [fn_name, fn_params, fn_body, &runtime](const std::vector<Value>& args) -> Value {
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
                if (i < args.size()) {
                    runtime.set_global(fn_params[i].name, args[i]);
                } else if (fn_params[i].default_value) {
                    runtime.set_global(fn_params[i].name, fn_params[i].default_value->eval(runtime));
                } else {
                    throw std::runtime_error(fn_name + " missing argument: " + fn_params[i].name);
                }
            }
            try {
                for (const auto& statement : *fn_body) {
                    statement->exec(runtime);
                }
            } catch (const ReturnSignal& ret) {
                return ret.value();
            }
            return {};
        });
    }

    std::string name;
    std::vector<FunctionParam> params;
    std::shared_ptr<Body> body;
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
        } catch (const std::exception& error) {
            if (!error_name.empty()) {
                runtime.set_global(error_name, Value::Object{{"Message", error.what()}});
            }
            for (const auto& statement : catch_body) {
                statement->exec(runtime);
            }
        }
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
    int line;
};

struct StopStmt final : Stmt {
    void exec(Runtime& runtime) const override {
        runtime.tick();
        throw StopSignal();
    }
};

struct BlockStmt final : Stmt {
    explicit BlockStmt(std::vector<std::unique_ptr<Stmt>> statements) : statements(std::move(statements)) {}
    void exec(Runtime& runtime) const override {
        for (const auto& statement : statements) {
            statement->exec(runtime);
        }
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
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> then_branch;
    std::vector<std::unique_ptr<Stmt>> else_branch;
};

struct WhileStmt final : Stmt {
    WhileStmt(std::unique_ptr<Expr> condition, std::vector<std::unique_ptr<Stmt>> body)
        : condition(std::move(condition)), body(std::move(body)) {}
    void exec(Runtime& runtime) const override {
        while (condition->eval(runtime).truthy()) {
            runtime.tick();
            for (const auto& statement : body) {
                statement->exec(runtime);
            }
        }
    }
    std::unique_ptr<Expr> condition;
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
            for (const auto& statement : body) {
                statement->exec(runtime);
            }
        }
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
            for (const auto& statement : body) {
                statement->exec(runtime);
            }
        }
    }
    std::string name;
    std::unique_ptr<Expr> iterable;
    std::vector<std::unique_ptr<Stmt>> body;
};

} // namespace

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

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
    } else if (match(TokenType::While)) {
        parsed = while_statement();
    } else if (match(TokenType::For)) {
        parsed = for_statement();
    } else if (match(TokenType::Function)) {
        parsed = function_statement();
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
        if (current_ + 1 < tokens_.size() && (tokens_[current_ + 1].type == TokenType::PlusEqual ||
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
    auto value = expression();
    return std::make_unique<PrintStmt>(std::move(value));
}

Parser::StmtPtr Parser::assignment_statement(bool) {
    const Token name = consume(TokenType::Identifier, "expected variable name");
    std::vector<ExprPtr> indexes;
    while (match(TokenType::LeftBracket)) {
        indexes.push_back(expression());
        consume(TokenType::RightBracket, "expected ']' after index");
    }
    consume(TokenType::Equal, "expected '=' after variable name");
    return std::make_unique<AssignStmt>(name.lexeme, std::move(indexes), expression());
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
        do {
            then_branch.push_back(statement());
        } while (match(TokenType::Colon) && !check(TokenType::Newline) && !check(TokenType::End));
        return std::make_unique<IfStmt>(std::move(condition), std::move(then_branch), std::vector<StmtPtr>{});
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

Parser::StmtPtr Parser::while_statement() {
    auto condition = expression();
    skip_newlines();
    auto body = block_until({TokenType::Wend});
    consume(TokenType::Wend, "expected WEND");
    return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
}

Parser::StmtPtr Parser::for_statement() {
    const Token name = consume(TokenType::Identifier, "expected loop variable");
    if (match(TokenType::In)) {
        auto iterable = expression();
        skip_newlines();
        auto body = block_until({TokenType::Next});
        consume(TokenType::Next, "expected NEXT");
        if (check(TokenType::Identifier)) {
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
    consume(TokenType::LeftParen, "expected '(' after function name");
    std::vector<FunctionParam> params;
    bool saw_default = false;
    if (!check(TokenType::RightParen)) {
        do {
            const Token param = consume(TokenType::Identifier, "expected parameter name");
            std::shared_ptr<Expr> default_value;
            if (match(TokenType::Equal)) {
                saw_default = true;
                default_value = std::shared_ptr<Expr>(expression().release());
            } else if (saw_default) {
                throw std::runtime_error(token_error(param, "parameters after a default value must also have defaults"));
            }
            params.push_back(FunctionParam{param.lexeme, std::move(default_value)});
        } while (match(TokenType::Comma));
    }
    consume(TokenType::RightParen, "expected ')' after parameters");
    skip_newlines();
    auto body = std::make_shared<FunctionStmt::Body>(block_until({TokenType::EndKeyword}));
    consume(TokenType::EndKeyword, "expected END FUNCTION");
    consume(TokenType::Function, "expected FUNCTION after END");
    return std::make_unique<FunctionStmt>(name.lexeme, std::move(params), std::move(body));
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
    return bit_or();
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
    while (match_any({TokenType::Star, TokenType::Slash})) {
        const TokenType op = previous().type;
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
            if (!check(TokenType::RightParen)) {
                do {
                    args.push_back(expression());
                } while (match(TokenType::Comma));
            }
            consume(TokenType::RightParen, "expected ')' after arguments");
            expr = std::make_unique<CallExpr>(variable->name, std::move(args));
        } else if (match(TokenType::LeftBracket)) {
            auto index = expression();
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
    if (match(TokenType::LeftParen)) {
        auto expr = expression();
        consume(TokenType::RightParen, "expected ')' after expression");
        return expr;
    }
    if (match(TokenType::LeftBracket)) {
        std::vector<ExprPtr> items;
        if (!check(TokenType::RightBracket)) {
            do {
                items.push_back(expression());
            } while (match(TokenType::Comma));
        }
        consume(TokenType::RightBracket, "expected ']' after array literal");
        return std::make_unique<ArrayExpr>(std::move(items));
    }
    if (match(TokenType::LeftBrace)) {
        std::vector<std::pair<std::string, ExprPtr>> fields;
        if (!check(TokenType::RightBrace)) {
            do {
                std::string key;
                if (match(TokenType::String) || match(TokenType::Identifier)) {
                    key = previous().lexeme;
                } else {
                    throw std::runtime_error(token_error(peek(), "expected object field name"));
                }
                consume(TokenType::Colon, "expected ':' after object field name");
                fields.emplace_back(std::move(key), expression());
            } while (match(TokenType::Comma));
        }
        consume(TokenType::RightBrace, "expected '}' after object literal");
        return std::make_unique<ObjectExpr>(std::move(fields));
    }
    throw std::runtime_error(token_error(peek(), "expected expression"));
}

} // namespace arco
