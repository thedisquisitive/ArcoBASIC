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
        CpuHalt,
        CpuHaltForever,
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

AmirInstruction amir_cpu_halt() {
    return AmirInstruction{AmirInstruction::Kind::CpuHalt};
}

AmirInstruction amir_cpu_halt_forever() {
    return AmirInstruction{AmirInstruction::Kind::CpuHaltForever};
}

bool is_terminal_instruction(const AmirInstruction& instruction) {
    return instruction.kind == AmirInstruction::Kind::Return || instruction.kind == AmirInstruction::Kind::Jump ||
           instruction.kind == AmirInstruction::Kind::Branch || instruction.kind == AmirInstruction::Kind::CpuHaltForever;
}

std::string upper_ascii(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return text;
}

const std::vector<CanonicalAstNodePtr>& ast_group(const CanonicalAstNode& node, const std::string& role) {
    static const std::vector<CanonicalAstNodePtr> empty;
    for (const auto& group : node.groups) {
        if (group.role == role) {
            return group.nodes;
        }
    }
    return empty;
}

std::string ast_operator(TokenType op) {
    switch (op) {
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Star: return "*";
        case TokenType::Slash: return "/";
        case TokenType::Mod: return "MOD";
        case TokenType::Ampersand:
        case TokenType::BitAnd: return "&";
        case TokenType::Pipe:
        case TokenType::BitOr: return "|";
        case TokenType::Caret:
        case TokenType::BitXor: return "^";
        case TokenType::Bang: return "!";
        case TokenType::Tilde:
        case TokenType::BitNot: return "~";
        case TokenType::LogicalAnd: return "&&";
        case TokenType::AndAlso: return "ANDALSO";
        case TokenType::LogicalOr: return "||";
        case TokenType::OrElse: return "ORELSE";
        case TokenType::Equal: return "==";
        case TokenType::NotEqual: return "!=";
        case TokenType::Less: return "<";
        case TokenType::LessEqual: return "<=";
        case TokenType::Greater: return ">";
        case TokenType::GreaterEqual: return ">=";
        case TokenType::ShiftLeft:
        case TokenType::ShiftLeftWord: return "<<";
        case TokenType::ShiftRight:
        case TokenType::ShiftRightWord: return ">>";
        case TokenType::Contains: return "CONTAINS";
        case TokenType::In: return "IN";
        default: return "?";
    }
}

std::string render_ast_expression(const CanonicalAstNode& node) {
    switch (node.kind) {
        case AstKind::Literal:
            return node.text;
        case AstKind::InterpolatedString:
            return "$\"" + escaped(node.text) + "\"";
        case AstKind::Variable:
            return node.name;
        case AstKind::Unary:
            return ast_operator(node.op) + (node.children.empty() ? "nothing" : render_ast_expression(*node.children.front()));
        case AstKind::Binary:
        case AstKind::Logical:
            if (node.children.size() == 2) {
                return render_ast_expression(*node.children[0]) + " " + ast_operator(node.op) + " " +
                       render_ast_expression(*node.children[1]);
            }
            return "nothing";
        case AstKind::Call:
        case AstKind::MethodCall: {
            std::ostringstream out;
            out << node.name << '(';
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (i > 0) out << ", ";
                out << render_ast_expression(*node.children[i]);
            }
            out << ')';
            return out.str();
        }
        case AstKind::SuperCall: {
            std::ostringstream out;
            out << "SUPER." << node.secondary_name << '(';
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (i > 0) out << ", ";
                out << render_ast_expression(*node.children[i]);
            }
            out << ')';
            return out.str();
        }
        case AstKind::Index:
            if (node.children.size() == 2) {
                return render_ast_expression(*node.children[0]) + "[" + render_ast_expression(*node.children[1]) + "]";
            }
            return "nothing";
        default:
            return node.text.empty() ? "nothing" : node.text;
    }
}

// RFC-0012 canonical frontend -> A-MIR lowering. This builder consumes only the parser-produced
// canonical AST. It never sees or reinterprets lexer tokens.
class AstAmirBuilder {
public:
    AstAmirBuilder(const std::vector<std::unique_ptr<Stmt>>& statements, std::string source_name) {
        module_.source_name = std::move(source_name);
        roots_.reserve(statements.size());
        for (const auto& statement : statements) {
            roots_.push_back(statement->canonical_ast());
        }
    }

    AmirModule build() {
        AmirFunction main;
        main.name = "Main";
        main.return_type = "I32";
        main.blocks.push_back(AmirBlock{"Entry"});
        current_block_ = 0;
        lower_statements(main, roots_);
        ensure_terminated(main, current_block_, "I32", "0");
        module_.functions.insert(module_.functions.begin(), std::move(main));
        validate_module();
        return module_;
    }

private:
    struct LoopTarget {
        AstKind kind;
        std::string continue_target;
        std::string exit_target;
    };

    std::string temp() { return "%t" + std::to_string(temporary_++); }
    std::string hidden_name(const std::string& prefix) { return "__fission_" + prefix + std::to_string(hidden_counter_++); }

    std::size_t add_block(AmirFunction& function, const std::string& prefix) {
        function.blocks.push_back(AmirBlock{prefix + std::to_string(block_counter_++)});
        return function.blocks.size() - 1;
    }

    AmirBlock& current_block(AmirFunction& function) { return function.blocks[current_block_]; }
    AmirBlock& block(AmirFunction& function, std::size_t index) { return function.blocks[index]; }
    std::string block_name(const AmirFunction& function, std::size_t index) const { return function.blocks[index].name; }

    void ensure_terminated(AmirFunction& function, std::size_t index, const std::string& type, const std::string& value) {
        if (block(function, index).instructions.empty() || !is_terminal_instruction(block(function, index).instructions.back())) {
            block(function, index).instructions.push_back(amir_return(type, value));
        }
    }

    void jump_if_open(AmirFunction& function, std::size_t index, const std::string& target) {
        if (block(function, index).instructions.empty() || !is_terminal_instruction(block(function, index).instructions.back())) {
            block(function, index).instructions.push_back(amir_jump(target));
        }
    }

    bool has_parameter(const AmirFunction& function, const std::string& name) const {
        for (const auto& param : function.params) {
            const auto space = param.find(' ');
            if ((space == std::string::npos ? param : param.substr(0, space)) == name) return true;
        }
        return false;
    }

    std::string lower_expression(AmirFunction& function, const CanonicalAstNode& node) {
        AmirBlock& out = current_block(function);
        switch (node.kind) {
            case AstKind::Literal: {
                const std::string result = temp();
                out.instructions.push_back(amir_const(result, node.text));
                return result;
            }
            case AstKind::InterpolatedString: {
                const std::string result = temp();
                out.instructions.push_back(amir_eval(result, render_ast_expression(node)));
                return result;
            }
            case AstKind::Variable:
                return lower_variable(out, node.name);
            case AstKind::Unary: {
                const std::string value = node.children.empty() ? lower_fallback(out, node) : lower_expression(function, *node.children[0]);
                const std::string result = temp();
                out.instructions.push_back(amir_unary(result, ast_operator(node.op), value));
                return result;
            }
            case AstKind::Binary:
            case AstKind::Logical: {
                if (node.children.size() != 2) return lower_fallback(out, node);
                const std::string left = lower_expression(function, *node.children[0]);
                const std::string right = lower_expression(function, *node.children[1]);
                const std::string result = temp();
                out.instructions.push_back(amir_binary(result, ast_operator(node.op), left, right));
                return result;
            }
            case AstKind::Call:
            case AstKind::MethodCall:
            case AstKind::SuperCall:
                return lower_call(function, node);
            case AstKind::Index: {
                if (node.children.size() != 2) return lower_fallback(out, node);
                const std::string target = lower_expression(function, *node.children[0]);
                const std::string index = lower_expression(function, *node.children[1]);
                const std::string result = temp();
                out.instructions.push_back(amir_index(result, target, index));
                return result;
            }
            case AstKind::Array: {
                std::vector<std::string> items;
                for (const auto& child : node.children) items.push_back(lower_expression(function, *child));
                const std::string result = temp();
                out.instructions.push_back(amir_array(result, std::move(items)));
                return result;
            }
            case AstKind::Object: {
                std::vector<std::string> fields;
                for (const auto& [key, value] : node.named_children) {
                    fields.push_back(key + ":" + lower_expression(function, *value));
                }
                const std::string result = temp();
                out.instructions.push_back(amir_object(result, std::move(fields)));
                return result;
            }
            default:
                return lower_fallback(out, node);
        }
    }

    std::string lower_fallback(AmirBlock& out, const CanonicalAstNode& node) {
        const std::string result = temp();
        out.instructions.push_back(amir_eval(result, render_ast_expression(node)));
        return result;
    }

    std::string lower_variable(AmirBlock& out, const std::string& name) {
        const auto parts = split_identifier_path(name);
        if (parts.size() <= 1) {
            const std::string result = temp();
            out.instructions.push_back(amir_load(result, name));
            return result;
        }
        std::string target = temp();
        out.instructions.push_back(amir_load(target, parts.front()));
        for (std::size_t i = 1; i < parts.size(); ++i) {
            const std::string property = temp();
            out.instructions.push_back(amir_const(property, "\"" + escaped(parts[i]) + "\""));
            const std::string indexed = temp();
            out.instructions.push_back(amir_index(indexed, target, property));
            target = indexed;
        }
        return target;
    }

    std::string lower_call(AmirFunction& function, const CanonicalAstNode& node) {
        std::vector<std::string> args;
        std::string target = node.name;
        if (node.kind == AstKind::SuperCall) {
            target = node.name + "." + node.secondary_name;
            args.push_back(lower_variable(current_block(function), "SELF"));
        }
        for (const auto& child : node.children) args.push_back(lower_expression(function, *child));
        const std::string result = temp();
        AmirInstruction instruction = amir_call_value(result, target, std::move(args));
        if (node.kind == AstKind::MethodCall && has_parameter(function, node.secondary_name)) {
            instruction.kind = AmirInstruction::Kind::CallExternal;
        }
        current_block(function).instructions.push_back(std::move(instruction));
        return result;
    }

    void lower_statements(AmirFunction& function, const std::vector<CanonicalAstNodePtr>& statements) {
        for (const auto& statement : statements) {
            lower_statement(function, *statement);
            const auto& instructions = current_block(function).instructions;
            if (!instructions.empty() && instructions.back().kind == AmirInstruction::Kind::CpuHaltForever) {
                break;
            }
        }
    }

    void lower_statement(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.line_label >= 0) {
            current_block(function).instructions.push_back(amir_label("L" + std::to_string(node.line_label)));
        }
        current_block(function).instructions.push_back(amir_source(node.source_line));

        switch (node.kind) {
            case AstKind::Print:
                lower_print(function, node);
                break;
            case AstKind::Assign:
                lower_assignment(function, node);
                break;
            case AstKind::CompoundAssign:
                lower_compound(function, node);
                break;
            case AstKind::FlagOperation:
                lower_flag_operation(function, node);
                break;
            case AstKind::Flags:
                lower_flags(function, node);
                break;
            case AstKind::HardwareSemantic:
                current_block(function).instructions.push_back(
                    node.name == "CPU.HaltForever" ? amir_cpu_halt_forever() : amir_cpu_halt());
                break;
            case AstKind::ExpressionStatement:
                lower_expression_statement(function, node);
                break;
            case AstKind::NoOp:
                break;
            case AstKind::Return:
                current_block(function).instructions.push_back(
                    node.children.empty() ? amir_return("VALUE", "nothing")
                                          : amir_return(function.return_type, lower_expression(function, *node.children[0])));
                break;
            case AstKind::Goto:
                current_block(function).instructions.push_back(amir_jump("L" + std::to_string(node.integer)));
                break;
            case AstKind::Stop:
                current_block(function).instructions.push_back(amir_return("I32", "0"));
                break;
            case AstKind::LoopControl:
                lower_loop_control(function, node);
                break;
            case AstKind::Block:
                lower_statements(function, ast_group(node, "body"));
                break;
            case AstKind::If:
                lower_if(function, node);
                break;
            case AstKind::While:
                lower_while(function, node);
                break;
            case AstKind::Do:
                lower_do(function, node);
                break;
            case AstKind::For:
                lower_for(function, node);
                break;
            case AstKind::ForEach:
                lower_for_each(function, node);
                break;
            case AstKind::Select:
                lower_select(function, node);
                break;
            case AstKind::Try:
                lower_try(function, node);
                break;
            case AstKind::Function:
                lower_function(function, node);
                break;
            case AstKind::Class:
                lower_class(function, node);
                break;
            case AstKind::Interface:
                current_block(function).instructions.push_back(amir_declare_interface(node.name, {}));
                break;
            default:
                current_block(function).instructions.push_back(amir_unsupported("AST kind " + std::to_string(static_cast<int>(node.kind))));
                break;
        }
    }

    void lower_print(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.empty()) {
            current_block(function).instructions.push_back(amir_unsupported("PRINT without expression"));
            return;
        }
        const auto& expr = *node.children[0];
        // RUN's parser-level representation is a Print(Call RUN). The bytecode backend has never
        // supported that shell construct; retain the deterministic diagnostic without reparsing.
        if (expr.kind == AstKind::Call && upper_ascii(expr.name) == "RUN") {
            current_block(function).instructions.push_back(amir_unsupported("RUN " + render_ast_expression(expr)));
            return;
        }
        current_block(function).instructions.push_back(amir_call("Runtime.Print", {lower_expression(function, expr)}));
    }

    void lower_assignment(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.empty()) return;
        const auto parts = split_identifier_path(node.name);
        std::vector<std::string> indexes;
        for (std::size_t i = 1; i < parts.size(); ++i) {
            const std::string property = temp();
            current_block(function).instructions.push_back(amir_const(property, "\"" + escaped(parts[i]) + "\""));
            indexes.push_back(property);
        }
        const std::size_t explicit_indexes = static_cast<std::size_t>(std::max(0, node.integer));
        for (std::size_t i = 0; i < explicit_indexes && i < node.children.size() - 1; ++i) {
            indexes.push_back(lower_expression(function, *node.children[i]));
        }
        const std::string value = lower_expression(function, *node.children.back());
        if (indexes.empty()) {
            current_block(function).instructions.push_back(amir_store(node.name, value));
        } else {
            indexes.push_back(value);
            current_block(function).instructions.push_back(amir_store_index(parts.empty() ? node.name : parts.front(), std::move(indexes)));
        }
    }

    void lower_compound(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.empty()) return;
        const std::string current = temp();
        current_block(function).instructions.push_back(amir_load(current, node.name));
        const std::string value = lower_expression(function, *node.children[0]);
        const std::string result = temp();
        current_block(function).instructions.push_back(amir_binary(result, ast_operator(node.op), current, value));
        current_block(function).instructions.push_back(amir_store(node.name, result));
    }

    void lower_flag_operation(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.empty()) return;
        const std::string current = temp();
        current_block(function).instructions.push_back(amir_load(current, node.name));
        std::string mask = lower_expression(function, *node.children[0]);
        std::string op = node.op == TokenType::Add ? "|" : node.op == TokenType::Toggle ? "^" : "&";
        if (node.op == TokenType::Remove) {
            const std::string inverted = temp();
            current_block(function).instructions.push_back(amir_unary(inverted, "~", mask));
            mask = inverted;
        }
        const std::string result = temp();
        current_block(function).instructions.push_back(amir_binary(result, op, current, mask));
        current_block(function).instructions.push_back(amir_store(node.name, result));
    }

    void lower_flags(AmirFunction& function, const CanonicalAstNode& node) {
        std::vector<std::string> fields;
        for (const auto& [name, value] : node.named_children) {
            fields.push_back(name + ":" + lower_expression(function, *value));
        }
        const std::string result = temp();
        current_block(function).instructions.push_back(amir_object(result, std::move(fields)));
        current_block(function).instructions.push_back(amir_store(node.name, result));
    }

    void lower_expression_statement(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.empty()) {
            current_block(function).instructions.push_back(amir_unsupported("empty expression statement"));
            return;
        }
        const AstKind kind = node.children[0]->kind;
        if (kind == AstKind::Call || kind == AstKind::MethodCall || kind == AstKind::SuperCall) {
            (void)lower_expression(function, *node.children[0]);
        } else {
            current_block(function).instructions.push_back(amir_unsupported(render_ast_expression(*node.children[0])));
        }
    }

    void lower_loop_control(AmirFunction& function, const CanonicalAstNode& node) {
        const AstKind target_kind = node.integer == 0 ? AstKind::For : node.integer == 1 ? AstKind::While : AstKind::Do;
        for (auto loop = loop_stack_.rbegin(); loop != loop_stack_.rend(); ++loop) {
            if (loop->kind == target_kind) {
                current_block(function).instructions.push_back(amir_jump(node.flag ? loop->continue_target : loop->exit_target));
                return;
            }
        }
        current_block(function).instructions.push_back(amir_unsupported("loop control outside matching loop"));
    }

    void lower_if(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.empty()) return;
        const std::size_t then_block = add_block(function, "IfThen");
        const std::size_t else_block = add_block(function, "IfElse");
        const std::size_t end_block = add_block(function, "IfEnd");
        const std::string condition = lower_expression(function, *node.children[0]);
        current_block(function).instructions.push_back(
            amir_branch(condition, block_name(function, then_block), block_name(function, else_block)));

        current_block_ = then_block;
        lower_statements(function, ast_group(node, "then"));
        const std::size_t then_final = current_block_;
        jump_if_open(function, then_final, block_name(function, end_block));

        current_block_ = else_block;
        lower_statements(function, ast_group(node, "else"));
        const std::size_t else_final = current_block_;
        jump_if_open(function, else_final, block_name(function, end_block));
        current_block_ = end_block;
    }

    void lower_while(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.empty()) return;
        const std::size_t cond_block = add_block(function, "WhileCond");
        const std::size_t body_block = add_block(function, "WhileBody");
        const std::size_t end_block = add_block(function, "WhileEnd");
        current_block(function).instructions.push_back(amir_jump(block_name(function, cond_block)));

        current_block_ = cond_block;
        const std::string condition = lower_expression(function, *node.children[0]);
        current_block(function).instructions.push_back(
            amir_branch(condition, block_name(function, body_block), block_name(function, end_block)));

        current_block_ = body_block;
        loop_stack_.push_back(LoopTarget{AstKind::While, block_name(function, cond_block), block_name(function, end_block)});
        lower_statements(function, ast_group(node, "body"));
        loop_stack_.pop_back();
        const std::size_t body_final = current_block_;
        jump_if_open(function, body_final, block_name(function, cond_block));
        current_block_ = end_block;
    }

    void lower_do(AmirFunction& function, const CanonicalAstNode& node) {
        const bool has_pre = node.integer != 0;
        const bool has_post = node.children.size() > static_cast<std::size_t>(has_pre ? 1 : 0);
        const CanonicalAstNode* pre = has_pre && !node.children.empty() ? node.children[0].get() : nullptr;
        const CanonicalAstNode* post = has_post ? node.children[has_pre ? 1 : 0].get() : nullptr;
        const std::size_t body_block = add_block(function, "DoBody");
        const std::size_t end_block = add_block(function, "DoEnd");
        std::size_t cond_block = body_block;

        if (pre) {
            cond_block = add_block(function, "DoCond");
            current_block(function).instructions.push_back(amir_jump(block_name(function, cond_block)));
            current_block_ = cond_block;
            const std::string condition = lower_expression(function, *pre);
            current_block(function).instructions.push_back(
                node.flag ? amir_branch(condition, block_name(function, end_block), block_name(function, body_block))
                          : amir_branch(condition, block_name(function, body_block), block_name(function, end_block)));
        } else {
            current_block(function).instructions.push_back(amir_jump(block_name(function, body_block)));
        }

        current_block_ = body_block;
        loop_stack_.push_back(LoopTarget{AstKind::Do, block_name(function, cond_block), block_name(function, end_block)});
        lower_statements(function, ast_group(node, "body"));
        loop_stack_.pop_back();
        const std::size_t body_final = current_block_;
        if (post && (block(function, body_final).instructions.empty() ||
                     !is_terminal_instruction(block(function, body_final).instructions.back()))) {
            const std::string condition = lower_expression(function, *post);
            block(function, current_block_).instructions.push_back(
                node.flag2 ? amir_branch(condition, block_name(function, end_block), block_name(function, body_block))
                           : amir_branch(condition, block_name(function, body_block), block_name(function, end_block)));
        } else {
            jump_if_open(function, body_final, block_name(function, cond_block));
        }
        current_block_ = end_block;
    }

    void lower_for(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.size() < 2) return;
        const std::string end_name = hidden_name("for_end");
        const std::string step_name = hidden_name("for_step");
        current_block(function).instructions.push_back(amir_store(node.name, lower_expression(function, *node.children[0])));
        current_block(function).instructions.push_back(amir_store(end_name, lower_expression(function, *node.children[1])));
        if (node.children.size() >= 3) {
            current_block(function).instructions.push_back(amir_store(step_name, lower_expression(function, *node.children[2])));
        } else {
            const std::string one = temp();
            current_block(function).instructions.push_back(amir_const(one, "1"));
            current_block(function).instructions.push_back(amir_store(step_name, one));
        }

        const std::size_t cond_block = add_block(function, "ForCond");
        const std::size_t pos_block = add_block(function, "ForCondPos");
        const std::size_t neg_block = add_block(function, "ForCondNeg");
        const std::size_t body_block = add_block(function, "ForBody");
        const std::size_t inc_block = add_block(function, "ForInc");
        const std::size_t end_block = add_block(function, "ForEnd");
        current_block(function).instructions.push_back(amir_jump(block_name(function, cond_block)));

        current_block_ = cond_block;
        const std::string step_check = temp();
        const std::string zero = temp();
        const std::string positive = temp();
        current_block(function).instructions.push_back(amir_load(step_check, step_name));
        current_block(function).instructions.push_back(amir_const(zero, "0"));
        current_block(function).instructions.push_back(amir_binary(positive, ">=", step_check, zero));
        current_block(function).instructions.push_back(
            amir_branch(positive, block_name(function, pos_block), block_name(function, neg_block)));

        current_block_ = pos_block;
        emit_for_comparison(function, node.name, end_name, "<=", body_block, end_block);
        current_block_ = neg_block;
        emit_for_comparison(function, node.name, end_name, ">=", body_block, end_block);

        current_block_ = body_block;
        loop_stack_.push_back(LoopTarget{AstKind::For, block_name(function, inc_block), block_name(function, end_block)});
        lower_statements(function, ast_group(node, "body"));
        loop_stack_.pop_back();
        const std::size_t body_final = current_block_;
        jump_if_open(function, body_final, block_name(function, inc_block));

        current_block_ = inc_block;
        const std::string old_value = temp();
        const std::string step_value = temp();
        const std::string next_value = temp();
        current_block(function).instructions.push_back(amir_load(old_value, node.name));
        current_block(function).instructions.push_back(amir_load(step_value, step_name));
        current_block(function).instructions.push_back(amir_binary(next_value, "+", old_value, step_value));
        current_block(function).instructions.push_back(amir_store(node.name, next_value));
        current_block(function).instructions.push_back(amir_jump(block_name(function, cond_block)));
        current_block_ = end_block;
    }

    void emit_for_comparison(AmirFunction& function, const std::string& loop_var, const std::string& end_name,
                             const std::string& op, std::size_t body_block, std::size_t end_block) {
        const std::string current = temp();
        const std::string limit = temp();
        const std::string condition = temp();
        current_block(function).instructions.push_back(amir_load(current, loop_var));
        current_block(function).instructions.push_back(amir_load(limit, end_name));
        current_block(function).instructions.push_back(amir_binary(condition, op, current, limit));
        current_block(function).instructions.push_back(
            amir_branch(condition, block_name(function, body_block), block_name(function, end_block)));
    }

    void lower_for_each(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.empty()) return;
        const std::string items_name = hidden_name("each_items");
        const std::string index_name = hidden_name("each_index");
        current_block(function).instructions.push_back(amir_store(items_name, lower_expression(function, *node.children[0])));
        const std::string zero = temp();
        current_block(function).instructions.push_back(amir_const(zero, "0"));
        current_block(function).instructions.push_back(amir_store(index_name, zero));

        const std::size_t cond_block = add_block(function, "ForEachCond");
        const std::size_t body_block = add_block(function, "ForEachBody");
        const std::size_t inc_block = add_block(function, "ForEachInc");
        const std::size_t end_block = add_block(function, "ForEachEnd");
        current_block(function).instructions.push_back(amir_jump(block_name(function, cond_block)));

        current_block_ = cond_block;
        const std::string index = temp();
        const std::string items = temp();
        const std::string length = temp();
        const std::string condition = temp();
        current_block(function).instructions.push_back(amir_load(index, index_name));
        current_block(function).instructions.push_back(amir_load(items, items_name));
        current_block(function).instructions.push_back(amir_call_value(length, "LEN", {items}));
        current_block(function).instructions.push_back(amir_binary(condition, "<", index, length));
        current_block(function).instructions.push_back(
            amir_branch(condition, block_name(function, body_block), block_name(function, end_block)));

        current_block_ = body_block;
        const std::string item = temp();
        current_block(function).instructions.push_back(amir_index(item, items, index));
        current_block(function).instructions.push_back(amir_store(node.name, item));
        loop_stack_.push_back(LoopTarget{AstKind::For, block_name(function, inc_block), block_name(function, end_block)});
        lower_statements(function, ast_group(node, "body"));
        loop_stack_.pop_back();
        const std::size_t body_final = current_block_;
        jump_if_open(function, body_final, block_name(function, inc_block));

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
    }

    void lower_select(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.empty()) return;
        const std::string target_name = hidden_name("select_value");
        current_block(function).instructions.push_back(amir_store(target_name, lower_expression(function, *node.children[0])));
        std::vector<CanonicalAstNodePtr> branches = ast_group(node, "branches");
        const auto& else_body = ast_group(node, "else");
        if (!else_body.empty()) {
            auto else_branch = std::make_shared<CanonicalAstNode>();
            else_branch->kind = AstKind::SelectBranch;
            else_branch->groups.push_back(CanonicalAstGroup{"body", else_body});
            branches.push_back(std::move(else_branch));
        }
        if (branches.empty()) return;

        const std::size_t end_block = add_block(function, "SelectEnd");
        std::vector<std::size_t> test_blocks;
        std::vector<std::size_t> body_blocks;
        for (std::size_t i = 0; i < branches.size(); ++i) {
            test_blocks.push_back(add_block(function, "SelectCase"));
            body_blocks.push_back(add_block(function, "SelectBody"));
        }
        current_block(function).instructions.push_back(amir_jump(block_name(function, test_blocks.front())));

        for (std::size_t i = 0; i < branches.size(); ++i) {
            current_block_ = test_blocks[i];
            const auto& matches = ast_group(*branches[i], "matches");
            const std::string next = i + 1 < branches.size() ? block_name(function, test_blocks[i + 1])
                                                              : block_name(function, end_block);
            if (matches.empty()) {
                current_block(function).instructions.push_back(amir_jump(block_name(function, body_blocks[i])));
            } else {
                const std::string condition = lower_select_matches(function, target_name, matches);
                current_block(function).instructions.push_back(
                    amir_branch(condition, block_name(function, body_blocks[i]), next));
            }
            current_block_ = body_blocks[i];
            lower_statements(function, ast_group(*branches[i], "body"));
            const std::size_t body_final = current_block_;
            jump_if_open(function, body_final, block_name(function, end_block));
        }
        current_block_ = end_block;
    }

    std::string lower_select_matches(AmirFunction& function, const std::string& target_name,
                                     const std::vector<CanonicalAstNodePtr>& matches) {
        std::vector<std::string> conditions;
        for (const auto& match : matches) {
            if (match->children.empty()) continue;
            const std::string target = temp();
            current_block(function).instructions.push_back(amir_load(target, target_name));
            if (match->children.size() >= 2) {
                const std::string start = lower_expression(function, *match->children[0]);
                const std::string stop = lower_expression(function, *match->children[1]);
                const std::string ge = temp();
                const std::string le = temp();
                const std::string both = temp();
                current_block(function).instructions.push_back(amir_binary(ge, ">=", target, start));
                current_block(function).instructions.push_back(amir_binary(le, "<=", target, stop));
                current_block(function).instructions.push_back(amir_binary(both, "&&", ge, le));
                conditions.push_back(both);
            } else {
                const std::string value = lower_expression(function, *match->children[0]);
                const std::string equal = temp();
                current_block(function).instructions.push_back(amir_binary(equal, "==", target, value));
                conditions.push_back(equal);
            }
        }
        if (conditions.empty()) {
            const std::string value = temp();
            current_block(function).instructions.push_back(amir_const(value, "false"));
            return value;
        }
        std::string result = conditions.front();
        for (std::size_t i = 1; i < conditions.size(); ++i) {
            const std::string next = temp();
            current_block(function).instructions.push_back(amir_binary(next, "||", result, conditions[i]));
            result = next;
        }
        return result;
    }

    void lower_try(AmirFunction& function, const CanonicalAstNode& node) {
        const std::size_t catch_block = add_block(function, "Catch");
        const std::size_t end_block = add_block(function, "TryEnd");
        current_block(function).instructions.push_back(amir_try_begin(block_name(function, catch_block), node.name));
        lower_statements(function, ast_group(node, "try"));
        const std::size_t try_final = current_block_;
        if (block(function, try_final).instructions.empty() || !is_terminal_instruction(block(function, try_final).instructions.back())) {
            block(function, try_final).instructions.push_back(amir_try_end());
            block(function, try_final).instructions.push_back(amir_jump(block_name(function, end_block)));
        }
        current_block_ = catch_block;
        lower_statements(function, ast_group(node, "catch"));
        const std::size_t catch_final = current_block_;
        jump_if_open(function, catch_final, block_name(function, end_block));
        current_block_ = end_block;
    }

    std::string parameter_text(const CanonicalAstParameter& param) const {
        std::string result = param.name;
        if (!param.type_name.empty()) result += " AS " + param.type_name;
        if (param.default_value) result += " = " + render_ast_expression(*param.default_value);
        return result;
    }

    void lower_function(AmirFunction& owner, const CanonicalAstNode& node) {
        AmirFunction function;
        function.name = node.name;
        function.return_type = node.type_name.empty() ? "VALUE" : node.type_name;
        for (const auto& param : node.parameters) function.params.push_back(parameter_text(param));
        std::vector<std::string> metadata;
        if (!function.params.empty()) {
            std::ostringstream params;
            for (std::size_t i = 0; i < function.params.size(); ++i) {
                if (i > 0) params << ',';
                params << function.params[i];
            }
            metadata.push_back("params=" + params.str());
        }
        metadata.push_back("returns=" + function.return_type);
        current_block(owner).instructions.push_back(amir_declare_function(function.name, metadata));
        function.blocks.push_back(AmirBlock{"Entry"});

        const std::size_t saved_block = current_block_;
        const auto saved_loops = loop_stack_;
        current_block_ = 0;
        loop_stack_.clear();
        lower_statements(function, ast_group(node, "body"));
        ensure_terminated(function, current_block_, "VALUE", "nothing");
        current_block_ = saved_block;
        loop_stack_ = saved_loops;
        module_.functions.push_back(std::move(function));
    }

    void lower_class(AmirFunction& owner, const CanonicalAstNode& node) {
        std::vector<std::string> metadata;
        std::ostringstream header;
        if (!node.secondary_name.empty()) header << "EXTENDS " << node.secondary_name;
        if (!node.names.empty()) {
            if (header.tellp() > 0) header << ' ';
            header << "IMPLEMENTS";
            for (const auto& name : node.names) header << ' ' << name;
        }
        if (header.tellp() > 0) metadata.push_back(header.str());
        current_block(owner).instructions.push_back(amir_declare_class(node.name, std::move(metadata)));

        for (const auto& method : ast_group(node, "methods")) {
            if (method->kind != AstKind::ClassMethod || method->flag2) continue;
            AmirFunction function;
            function.name = node.name + "." + method->name;
            function.return_type = method->type_name.empty() ? "VALUE" : method->type_name;
            for (const auto& param : method->parameters) function.params.push_back(parameter_text(param));
            function.blocks.push_back(AmirBlock{"Entry"});
            const std::size_t saved_block = current_block_;
            const auto saved_loops = loop_stack_;
            current_block_ = 0;
            loop_stack_.clear();
            lower_statements(function, ast_group(*method, "body"));
            ensure_terminated(function, current_block_, "VALUE", "nothing");
            current_block_ = saved_block;
            loop_stack_ = saved_loops;
            module_.functions.push_back(std::move(function));
        }
    }

    void validate_module() {
        for (const auto& function : module_.functions) {
            std::vector<std::string> targets;
            for (const auto& current : function.blocks) {
                targets.push_back(current.name);
                for (const auto& instruction : current.instructions) {
                    if (instruction.kind == AmirInstruction::Kind::Label) targets.push_back(instruction.target);
                }
            }
            for (const auto& current : function.blocks) {
                if (current.instructions.empty()) {
                    module_.diagnostics.push_back("empty block " + function.name + "." + current.name);
                    continue;
                }
                if (!is_terminal_instruction(current.instructions.back())) {
                    module_.diagnostics.push_back("unterminated block " + function.name + "." + current.name);
                }
                for (const auto& instruction : current.instructions) {
                    if (instruction.kind == AmirInstruction::Kind::Unsupported && !instruction.operands.empty()) {
                        module_.diagnostics.push_back("unsupported lowering in " + function.name + "." + current.name + ": " +
                                                      instruction.operands.front());
                    } else if (instruction.kind == AmirInstruction::Kind::Jump) {
                        validate_target(function.name, current.name, instruction.target, targets);
                    } else if (instruction.kind == AmirInstruction::Kind::Branch && instruction.operands.size() >= 3) {
                        validate_target(function.name, current.name, instruction.operands[1], targets);
                        validate_target(function.name, current.name, instruction.operands[2], targets);
                    } else if (instruction.kind == AmirInstruction::Kind::TryBegin) {
                        validate_target(function.name, current.name, instruction.target, targets);
                    }
                }
            }
        }
    }

    void validate_target(const std::string& function, const std::string& source_block, const std::string& target,
                         const std::vector<std::string>& targets) {
        bool found = false;
        for (const auto& candidate : targets) {
            if (candidate == target) {
                found = true;
                break;
            }
        }
        if (!found) {
            module_.diagnostics.push_back("unresolved A-MIR target " + target + " from " + function + "." + source_block);
        }
    }

    AmirModule module_;
    std::vector<CanonicalAstNodePtr> roots_;
    std::vector<LoopTarget> loop_stack_;
    int temporary_ = 0;
    int hidden_counter_ = 0;
    std::size_t block_counter_ = 0;
    std::size_t current_block_ = 0;
};

AmirModule build_amir(const std::vector<std::unique_ptr<Stmt>>& statements, const std::string& source_name) {
    return AstAmirBuilder(statements, source_name).build();
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
        case AmirInstruction::Kind::CpuHalt:
            out << "    CPU.HALT\n";
            break;
        case AmirInstruction::Kind::CpuHaltForever:
            out << "    CPU.HALT_FOREVER\n";
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
                    case AmirInstruction::Kind::CpuHalt:
                        module.diagnostics.push_back("hardware semantic CPU.Halt is unsupported by hosted bytecode backend");
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Unsupported, {"CPU.Halt"}));
                        break;
                    case AmirInstruction::Kind::CpuHaltForever:
                        module.diagnostics.push_back("hardware semantic CPU.HaltForever is unsupported by hosted bytecode backend");
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Unsupported, {"CPU.HaltForever"}));
                        break;
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

            case AmirInstruction::Kind::CpuHalt:
                result.text.hlt();
                break;

            case AmirInstruction::Kind::CpuHaltForever:
                // Disable maskable interrupts, halt, and return to HLT if a non-maskable
                // event resumes execution. EB FD jumps back three bytes to the HLT.
                result.text.cli();
                result.text.hlt();
                result.text.jmp_rel8(-3);
                break;

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

                if (final_field->offset_bytes <= 0x7F) {
                    result.text.call_indirect_disp8(Reg::RAX, static_cast<std::uint8_t>(final_field->offset_bytes));
                } else {
                    result.text.call_indirect_disp32(Reg::RAX, static_cast<std::uint32_t>(final_field->offset_bytes));
                }
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
        auto statements = parser.parse();

        return {true, render_amir(build_amir(statements, source_name)), ""};
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
        auto statements = parser.parse();

        return {true, render_calling_convention(build_amir(statements, source_name)), ""};
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
        auto statements = parser.parse();

        const auto codegen = generate_x86_64_function(build_amir(statements, source_name), entry_function);
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
        auto statements = parser.parse();

        const auto codegen = generate_x86_64_function(build_amir(statements, source_name), entry_function);
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
        auto statements = parser.parse();

        const AmirModule amir = build_amir(statements, source_name);
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
        auto statements = parser.parse();

        Runtime runtime;
        std::ostringstream output;
        runtime.set_output(output);
        (void)execute_bytecode(build_bytecode(build_amir(statements, source_name)), runtime);
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
        auto statements = parser.parse();

        const AmirModule amir = build_amir(statements, path);
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
