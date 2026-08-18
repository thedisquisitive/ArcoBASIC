#include "arco/fission.hpp"

#include "arco/calling_convention.hpp"
#include "arco/runtime.hpp"
#include "arco/pe_image.hpp"
#include "arco/uefi_bindings.hpp"
#include "arco/utf16.hpp"
#include "arco/x86_64_encoder.hpp"

#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
            default:
                if (c < 32 || c > 126) {
                    out << "\\" << std::oct << std::setw(3) << std::setfill('0') << static_cast<int>(c)
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
        // A call through a declared function parameter (arcology-os/docs/systems/uefi-target.md section 5)
        // rather than a namespaced host/stdlib function -- e.g. calling a method reached off a
        // UEFI protocol pointer received as a parameter. Structurally identical to CallValue;
        // kept distinct so later work packages (ABI/codegen) can tell them apart without
        // re-deriving the classification. See Packet WP-004 "external or ABI-bound function
        // calls".
        CallExternal,
        CpuHalt,
        CpuHaltForever,
        CpuPause,
        Port,
        Memory,
        Barrier,
        Array,
        Tuple,
        Object,
        Index,
        Slice,
        Copy,
        AddressOf,
        Store,
        StoreIndex,
        StoreSlice,
        Destructure,
        Call,
        Jump,
        Branch,
        TryBegin,
        TryEnd,
        Throw,
        DeclareFunction,
        DeclareClass,
        DeclareInterface,
        Return,
        Unsupported,
    };

    Kind kind;
    std::string result;
    std::string result_type;
    std::string target;
    // Runtime-library ABI ownership metadata. Empty means ordinary value/call semantics.
    std::string ownership;
    std::vector<std::string> operands;
    std::vector<std::string> operand_types;
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
    std::optional<std::uint64_t> instruction_limit;
    std::vector<AmirFunction> functions;
    std::vector<std::string> diagnostics;
    // Child class name -> EXTENDS parent name (empty if none). Purely static/compile-time
    // information (ArcoBASIC has no dynamic reparenting), carried into BytecodeModule so instance
    // method dispatch can walk it at runtime without re-deriving it from the AST.
    std::unordered_map<std::string, std::string> class_parents;
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

AmirInstruction amir_tuple(std::string result, std::vector<std::string> operands) {
    AmirInstruction instruction{AmirInstruction::Kind::Tuple};
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

AmirInstruction amir_slice(std::string result, std::string target, std::string start, std::string end,
                           std::string step) {
    AmirInstruction instruction{AmirInstruction::Kind::Slice};
    instruction.result = std::move(result);
    instruction.target = std::move(target);
    instruction.operands = {std::move(start), std::move(end), std::move(step)};
    return instruction;
}

AmirInstruction amir_copy(std::string result, std::string value) {
    AmirInstruction instruction{AmirInstruction::Kind::Copy};
    instruction.result = std::move(result);
    instruction.operands.push_back(std::move(value));
    return instruction;
}

AmirInstruction amir_address_of(std::string result, std::string name) {
    AmirInstruction instruction{AmirInstruction::Kind::AddressOf};
    instruction.result = std::move(result);
    instruction.target = std::move(name);
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

AmirInstruction amir_store_slice(std::string target, std::string start, std::string end, std::string replacement) {
    AmirInstruction instruction{AmirInstruction::Kind::StoreSlice};
    instruction.target = std::move(target);
    instruction.operands = {std::move(start), std::move(end), std::move(replacement)};
    return instruction;
}

AmirInstruction amir_destructure(std::vector<std::string> targets, std::string source) {
    AmirInstruction instruction{AmirInstruction::Kind::Destructure};
    instruction.target = std::move(source);
    instruction.operands = std::move(targets);
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

AmirInstruction amir_throw(std::string value) {
    AmirInstruction instruction{AmirInstruction::Kind::Throw};
    instruction.operands.push_back(std::move(value));
    return instruction;
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

AmirInstruction amir_cpu_pause() {
    return AmirInstruction{AmirInstruction::Kind::CpuPause};
}

AmirInstruction amir_port(std::string operation, std::string result, std::vector<std::string> operands,
                          std::string result_type, std::vector<std::string> operand_types) {
    AmirInstruction instruction{AmirInstruction::Kind::Port};
    instruction.target = std::move(operation);
    instruction.result = std::move(result);
    instruction.operands = std::move(operands);
    instruction.result_type = std::move(result_type);
    instruction.operand_types = std::move(operand_types);
    return instruction;
}

AmirInstruction amir_memory(std::string operation, std::string result, std::vector<std::string> operands,
                             std::string result_type, std::vector<std::string> operand_types) {
    AmirInstruction instruction{AmirInstruction::Kind::Memory};
    instruction.target = std::move(operation);
    instruction.result = std::move(result);
    instruction.operands = std::move(operands);
    instruction.result_type = std::move(result_type);
    instruction.operand_types = std::move(operand_types);
    return instruction;
}

AmirInstruction amir_barrier(std::string operation) {
    AmirInstruction instruction{AmirInstruction::Kind::Barrier};
    instruction.target = std::move(operation);
    return instruction;
}

bool is_terminal_instruction(const AmirInstruction& instruction) {
    return instruction.kind == AmirInstruction::Kind::Return || instruction.kind == AmirInstruction::Kind::Jump ||
           instruction.kind == AmirInstruction::Kind::Branch || instruction.kind == AmirInstruction::Kind::Throw ||
           instruction.kind == AmirInstruction::Kind::CpuHaltForever;
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

CanonicalAstNodePtr replace_variable_name(const CanonicalAstNodePtr& node, const std::string& from, const std::string& to) {
    if (!node) return nullptr;
    auto copy = std::make_shared<CanonicalAstNode>(*node);
    if (copy->kind == AstKind::Variable && copy->name == from) copy->name = to;
    copy->children.clear();
    for (const auto& child : node->children) copy->children.push_back(replace_variable_name(child, from, to));
    copy->named_children.clear();
    for (const auto& [name, child] : node->named_children) {
        copy->named_children.push_back({name, replace_variable_name(child, from, to)});
    }
    copy->groups.clear();
    for (const auto& group : node->groups) {
        CanonicalAstGroup copied_group;
        copied_group.role = group.role;
        for (const auto& child : group.nodes) copied_group.nodes.push_back(replace_variable_name(child, from, to));
        copy->groups.push_back(std::move(copied_group));
    }
    copy->parameters.clear();
    for (const auto& parameter : node->parameters) {
        copy->parameters.push_back(CanonicalAstParameter{
            parameter.name,
            parameter.type_name,
            replace_variable_name(parameter.default_value, from, to),
        });
    }
    return copy;
}

std::string ast_operator(TokenType op) {
    switch (op) {
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Star: return "*";
        case TokenType::Slash: return "/";
        case TokenType::Backslash: return "\\";
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
        case TokenType::ShiftArithmeticRightWord: return "SAR";
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
        case AstKind::Slice:
            if (node.children.size() == 4) {
                return render_ast_expression(*node.children[0]) + "[" +
                    (node.children[1] ? render_ast_expression(*node.children[1]) : "") + ":" +
                    (node.children[2] ? render_ast_expression(*node.children[2]) : "") +
                    (node.children[3] ? ":" + render_ast_expression(*node.children[3]) : "") + "]";
            }
            return "nothing";
        case AstKind::Copy:
            return node.children.empty() ? "COPY nothing" : "COPY " + render_ast_expression(*node.children[0]);
        case AstKind::AddressOf:
            return "ADDRESSOF " + node.name;
        case AstKind::Tuple: {
            std::string result = "(";
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (i) result += ", ";
                result += render_ast_expression(*node.children[i]);
            }
            if (node.children.size() == 1) result += ',';
            return result + ")";
        }
        case AstKind::Array: {
            std::string result = "[";
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (i) result += ", ";
                result += render_ast_expression(*node.children[i]);
            }
            return result + "]";
        }
        case AstKind::ArrayComprehension:
            if (node.children.size() >= 2) {
                std::string result = "[" + render_ast_expression(*node.children[0]) + " FOR " + node.name +
                    " IN " + render_ast_expression(*node.children[1]);
                if (node.children.size() > 2 && node.children[2]) {
                    result += " IF " + render_ast_expression(*node.children[2]);
                }
                return result + "]";
            }
            return "[]";
        default:
            return node.text.empty() ? "nothing" : node.text;
    }
}

// Statement-level counterpart to render_ast_expression: walks the same RFC-0012 canonical AST
// (CanonicalAstNode) back into ArcoBASIC source text. This is the experiment for whether that
// canonical AST can serve as a single source of truth a graph editor and hand-written code both
// read/write -- if pretty_print_canonical(parse(source)) reparses to the same canonical AST as
// the original source, the AST is a safe round-trip substrate for that subset of the language.
// Deliberately covers only the small "v1 graph" subset agreed for the ArcoFlow experiment
// (sequence, branch, loop, function/return, calls, assignment) rather than the whole grammar;
// anything else renders as a visible placeholder rather than silently producing wrong code.
std::string indent_pad(int indent) {
    return std::string(static_cast<std::size_t>(std::max(0, indent)) * 4, ' ');
}

const CanonicalAstGroup* find_canonical_group(const CanonicalAstNode& node, const std::string& role) {
    for (const auto& group : node.groups) {
        if (group.role == role) {
            return &group;
        }
    }
    return nullptr;
}

void render_ast_statement(std::ostream& out, const CanonicalAstNode& node, int indent);

void render_ast_block(std::ostream& out, const std::vector<CanonicalAstNodePtr>& nodes, int indent) {
    for (const auto& child : nodes) {
        if (child) render_ast_statement(out, *child, indent);
    }
}

void render_ast_statement(std::ostream& out, const CanonicalAstNode& node, int indent) {
    const std::string pad = indent_pad(indent);
    // Traditional BASIC line numbers (`10 PRINT "hi"`) are tracked on every Stmt as line_label,
    // independent of statement kind. A label belongs on exactly the one physical line a numbered
    // statement started on -- multi-line constructs (IF/FOR/FUNCTION) use plain `pad`, not
    // `first_pad`, for their closing END IF/NEXT/END FUNCTION line, so the label doesn't repeat.
    const std::string first_pad = node.line_label >= 0 ? std::to_string(node.line_label) + " " + pad : pad;
    switch (node.kind) {
        case AstKind::Comment:
            out << first_pad << "REM " << node.text << "\n";
            return;
        case AstKind::Goto:
            out << first_pad << "GOTO " << node.integer << "\n";
            return;
        case AstKind::Stop:
            out << first_pad << "STOP\n";
            return;
        case AstKind::Print:
            out << first_pad << "PRINT " << render_ast_expression(*node.children.at(0)) << "\n";
            return;
        case AstKind::Assign: {
            std::string target = node.name;
            for (int i = 0; i < node.integer; ++i) {
                target += "[" + render_ast_expression(*node.children.at(static_cast<std::size_t>(i))) + "]";
            }
            out << first_pad << "LET " << target << " = " << render_ast_expression(*node.children.back()) << "\n";
            return;
        }
        case AstKind::ExpressionStatement:
            out << first_pad << render_ast_expression(*node.children.at(0)) << "\n";
            return;
        case AstKind::CompoundAssign: {
            std::string op_text;
            switch (node.op) {
                case TokenType::PlusEqual: op_text = "+="; break;
                case TokenType::MinusEqual: op_text = "-="; break;
                case TokenType::StarEqual: op_text = "*="; break;
                case TokenType::SlashEqual: op_text = "/="; break;
                case TokenType::AmpersandEqual: op_text = "&="; break;
                case TokenType::PipeEqual: op_text = "|="; break;
                case TokenType::CaretEqual: op_text = "^="; break;
                case TokenType::ShiftLeftEqual: op_text = "<<="; break;
                case TokenType::ShiftRightEqual: op_text = ">>="; break;
                default: op_text = "+="; break;
            }
            out << first_pad << node.name << " " << op_text << " " << render_ast_expression(*node.children.at(0)) << "\n";
            return;
        }
        case AstKind::Return:
            out << first_pad << "RETURN";
            if (!node.children.empty()) out << " " << render_ast_expression(*node.children.at(0));
            out << "\n";
            return;
        case AstKind::If: {
            out << first_pad << "IF " << render_ast_expression(*node.children.at(0)) << " THEN\n";
            if (const auto* then_group = find_canonical_group(node, "then")) render_ast_block(out, then_group->nodes, indent + 1);
            const auto* else_group = find_canonical_group(node, "else");
            if (else_group && !else_group->nodes.empty()) {
                out << pad << "ELSE\n";
                render_ast_block(out, else_group->nodes, indent + 1);
            }
            out << pad << "END IF\n";
            return;
        }
        case AstKind::For: {
            out << first_pad << "FOR " << node.name << " = " << render_ast_expression(*node.children.at(0)) << " TO "
                << render_ast_expression(*node.children.at(1));
            if (node.children.size() > 2 && node.children[2]) {
                out << " STEP " << render_ast_expression(*node.children[2]);
            }
            out << "\n";
            if (const auto* body = find_canonical_group(node, "body")) render_ast_block(out, body->nodes, indent + 1);
            out << pad << "NEXT\n";
            return;
        }
        case AstKind::Function: {
            out << first_pad << "FUNCTION " << node.name << "(";
            for (std::size_t i = 0; i < node.parameters.size(); ++i) {
                if (i) out << ", ";
                out << node.parameters[i].name;
                if (node.parameters[i].default_value) out << " = " << render_ast_expression(*node.parameters[i].default_value);
            }
            out << ")";
            if (!node.type_name.empty()) out << " AS " << node.type_name;
            out << "\n";
            if (const auto* body = find_canonical_group(node, "body")) render_ast_block(out, body->nodes, indent + 1);
            out << pad << "END FUNCTION\n";
            return;
        }
        default:
            out << first_pad << "REM <pretty-printing not yet implemented for this construct>\n";
            return;
    }
}

std::string pretty_print_canonical(const std::vector<std::unique_ptr<Stmt>>& statements) {
    std::ostringstream out;
    for (const auto& statement : statements) {
        render_ast_statement(out, *statement->canonical_ast(), 0);
    }
    return out.str();
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
        // Args (e.g. examples/arconote.abas's `IF LEN(Args) > 0 THEN ...`) is a plain local like
        // any other free identifier by the time it reaches lower_statements below -- seed it as
        // Main's first instructions from the runtime global of the same name (see
        // Runtime.Args in runtime.cpp) rather than teaching every LOAD site about a global
        // fallback. User code that assigns its own Args later just overwrites this normally.
        {
            const std::string args_temp = temp();
            current_block(main).instructions.push_back(amir_call_value(args_temp, "Runtime.Args", {}));
            current_block(main).instructions.push_back(amir_store("Args", args_temp));
        }
        lower_statements(main, roots_);
        ensure_terminated(main, current_block_, "I32", "0");
        apply_script_global_scoping(main);
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

    // Every FUNCTION lowers to its own independent AmirFunction with its own locals -- confirmed
    // by arcoflow/arcoflow.abas's `app = {...}` (assigned once, at script scope) crashing every
    // FUNCTION that merely reads app.Something with "undefined bytecode local: app", the same
    // failure Args had before Runtime.Args/the seed above, just for any ordinary top-level
    // variable instead of one specific name. The tree-walking runtime (arco_cli, ground truth --
    // see the reference "x"/"Leak" scoping check this fix was verified against) resolves a free
    // identifier a function never assigns by reading the enclosing script scope, while a plain
    // assignment inside the function only ever shadows a local copy (the outer variable is
    // unchanged when the function returns). This reproduces that: every plain assignment to a
    // script-scope name in Main also mirrors into a runtime global (Runtime.SetGlobal), and every
    // other function seeds its own same-named local from that global (Runtime.GetGlobal) in a
    // synthetic prologue -- so reads see the current script-scope value, and a function-local
    // assignment still only shadows its own slot, exactly like the tree-walker. Field/element
    // mutation (STORE_INDEX, e.g. `app.Count = ...`) needs no extra help: objects/arrays are
    // reference values, so mutating one through the seeded local mutates the same object Main
    // (and every other function) sees.
    //
    // Scoped to names actually shared with another function (assigned in Main AND referenced by
    // name -- Load/Store/StoreIndex/StoreSlice/Destructure, the same instruction kinds
    // build_bytecode's local_ref allocates a slot for -- somewhere else), not every Main-level
    // assignment: a script with no FUNCTIONs, or whose functions are self-contained, gets none of
    // this instrumentation and compiles exactly as before. That keeps the change scoped to the
    // pattern that actually needs it instead of shifting every program's temp/instruction count.
    void apply_script_global_scoping(AmirFunction& main) {
        std::unordered_set<std::string> assigned_in_main;
        for (const auto& block : main.blocks) {
            for (const auto& instruction : block.instructions) {
                if (instruction.kind != AmirInstruction::Kind::Store) continue;
                const std::string& name = instruction.target;
                if (name.empty() || name == "Args") continue;
                if (name.rfind("__fission_", 0) == 0) continue;
                assigned_in_main.insert(name);
            }
        }
        if (assigned_in_main.empty()) return;

        std::unordered_set<std::string> referenced_elsewhere;
        for (const auto& function : module_.functions) {
            for (const auto& block : function.blocks) {
                for (const auto& instruction : block.instructions) {
                    switch (instruction.kind) {
                        case AmirInstruction::Kind::Load:
                        case AmirInstruction::Kind::Store:
                        case AmirInstruction::Kind::StoreIndex:
                        case AmirInstruction::Kind::StoreSlice:
                            referenced_elsewhere.insert(instruction.target);
                            break;
                        case AmirInstruction::Kind::Destructure:
                            for (const auto& name : instruction.operands) referenced_elsewhere.insert(name);
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        std::unordered_set<std::string> script_globals;
        for (const auto& name : assigned_in_main) {
            if (referenced_elsewhere.count(name) > 0) script_globals.insert(name);
        }
        if (script_globals.empty()) return;

        for (auto& block : main.blocks) {
            std::vector<AmirInstruction> mirrored;
            mirrored.reserve(block.instructions.size());
            for (auto& instruction : block.instructions) {
                const bool needs_mirror = instruction.kind == AmirInstruction::Kind::Store &&
                                           script_globals.count(instruction.target) > 0;
                const std::string name = instruction.target;
                mirrored.push_back(std::move(instruction));
                if (needs_mirror) {
                    // Reload from the local rather than reusing the Store's own value operand:
                    // build_bytecode fuses an adjacent Const+Store into a single STORE_CONST and
                    // drops the Const's temp entirely, which left that temp undefined once this
                    // pass also referenced it here ("undefined bytecode temporary"). A fresh Load
                    // always materializes regardless of how the preceding Store got optimized.
                    const std::string reloaded = temp();
                    mirrored.push_back(amir_load(reloaded, name));
                    mirrored.push_back(amir_call_value(temp(), "Runtime.SetGlobal", {"\"" + escaped(name) + "\"", reloaded}));
                }
            }
            block.instructions = std::move(mirrored);
        }

        for (auto& function : module_.functions) {
            if (function.blocks.empty()) continue;
            std::vector<AmirInstruction> prologue;
            for (const auto& name : script_globals) {
                if (has_parameter(function, name)) continue;
                const std::string value_temp = temp();
                prologue.push_back(amir_call_value(value_temp, "Runtime.GetGlobal", {"\"" + escaped(name) + "\""}));
                prologue.push_back(amir_store(name, value_temp));
            }
            auto& entry = function.blocks.front().instructions;
            entry.insert(entry.begin(), std::make_move_iterator(prologue.begin()), std::make_move_iterator(prologue.end()));
        }
    }

    bool is_fixed_integer_type(const std::string& type) const {
        return type == "U8" || type == "U16" || type == "U32" || type == "U64" ||
               type == "I8" || type == "I16" || type == "I32" || type == "I64" || type == "BOOL";
    }

    bool is_port_type(const std::string& type) const { return type == "IOPORT"; }

    std::string type_of_expression(const CanonicalAstNode& node, const std::string& expected = "") const {
        if (node.kind == AstKind::Literal) {
            if (node.text == "true" || node.text == "false") return "BOOL";
            if (node.text.rfind("BITS \"", 0) == 0) return "BITVECTOR";
            return expected;
        }
        if (node.kind == AstKind::Tuple) return "TUPLE";
        if (node.kind == AstKind::ArrayComprehension) return "ARRAY";
        if (node.kind == AstKind::AddressOf) return "CALLABLE";
        if (node.kind == AstKind::Variable) {
            const auto found = types_.find(node.name);
            return found == types_.end() ? expected : found->second;
        }
        if (node.kind == AstKind::Unary) {
            if (node.op == TokenType::Bang) return "BOOL";
            return node.children.empty() ? expected : type_of_expression(*node.children.front(), expected);
        }
        if (node.kind == AstKind::Binary || node.kind == AstKind::Logical) {
            switch (node.op) {
                case TokenType::Equal: case TokenType::NotEqual: case TokenType::Less:
                case TokenType::LessEqual: case TokenType::Greater: case TokenType::GreaterEqual:
                case TokenType::LogicalAnd: case TokenType::LogicalOr: case TokenType::AndAlso: case TokenType::OrElse:
                    return "BOOL";
                default:
                    return node.children.empty() ? expected : type_of_expression(*node.children.front(), expected);
            }
        }
        if (node.kind == AstKind::Call || node.kind == AstKind::MethodCall || node.kind == AstKind::SuperCall) {
            const std::string name = upper_ascii(node.name);
            if (name == "CPU.READCR3") return "U64";
            if (name == "CPU.READCR2") return "U64";
            if (name == "CPU.READRSP") return "U64";
            if (name == "GRAPHICS.CREATESURFACE" || name == "GRAPHICS.PRIMARYSURFACE") return "SURFACE";
            if (name == "GRAPHICS.CREATEWINDOW") return "WINDOW";
            if (name == "GRAPHICS.CREATEIMAGE") return "IMAGE";
            if (name == "FILES.OPEN") return "FILE";
            if (name == "NETWORK.CONNECT") return "SOCKET";
        }
        if (node.kind == AstKind::PortOperation) {
            const std::string name = upper_ascii(node.name);
            if (name == "PORT.ADDRESS" || name == "PORT.OFFSET") return "IOPORT";
            if (name == "PORT.READ8" || name == "PORT.READBYTE") return "U8";
            if (name == "PORT.READ16" || name == "PORT.READWORD") return "U16";
            if (name == "PORT.READ32" || name == "PORT.READDWORD") return "U32";
            return "";
        }
        if (node.kind == AstKind::MemoryOperation) {
            const std::string name = upper_ascii(node.name);
            if (name == "CPU.READCR3") return "U64";
            if (name == "CPU.READCR2") return "U64";
            if (name == "UEFI.GOP.DISCOVER") return "UEFI.GraphicsOutputProtocol";
            if (name == "UEFI.GOP.MODE") return "UEFI.GraphicsOutputMode";
            if (name == "UEFI.GOP.FRAMEBUFFERBASE") return "PHYSICALPTR";
            if (name == "UEFI.GOP.FRAMEBUFFERSIZE") return "U64";
            if (name == "UEFI.GOP.WIDTH" || name == "UEFI.GOP.HEIGHT" || name == "UEFI.GOP.PIXELSPERSCANLINE" || name == "UEFI.GOP.PIXELFORMAT") return "U32";
            if (name == "ADDRESS.PHYSICAL") return "PHYSICALPTR";
            if (name == "ADDRESS.VIRTUAL") return "VIRTUALPTR";
            if (name == "ADDRESS.MMIO") return "MMIOPTR";
            if (name == "ADDRESS.VALUE") return "U64";
            if (name == "ADDRESS.LOCAL") return "PTR";
            if (name == "MEMORY.MAP") return "VIRTUALPTR";
            if (name == "MEMORY.MAPDEVICE") return "MMIOPTR";
            if (name == "GRAPHICS.PRIMARYSURFACE") return "SURFACE";
            if (name == "GRAPHICS.DESTROYSURFACE") return "BOOL";
            if (name == "MEMORY.READ8") return "U8";
            if (name == "MEMORY.READ16") return "U16";
            if (name == "MEMORY.READ32") return "U32";
            if (name == "MEMORY.READ64") return "U64";
            if (name == "MEMORY.ISALIGNED") return "BOOL";
            return "";
        }
        return expected;
    }

    void report_integer_error(const std::string& message) {
        module_.diagnostics.push_back(message);
    }

    // ANDALSO/ORELSE short-circuit lowering: evaluate the left side, branch on it without ever
    // touching the right side unless it's actually needed, and merge through a hidden local (the
    // same merge-through-a-hidden-local pattern ArrayComprehension above uses for its result) --
    // `out` in lower_expression goes stale the moment current_block_ moves to a new block, so
    // every push here goes through current_block(function) freshly, never a cached reference.
    std::string lower_short_circuit_logical(AmirFunction& function, const CanonicalAstNode& node) {
        const bool is_and = node.op == TokenType::AndAlso;
        const std::string result_name = hidden_name("shortcircuit");
        const std::string left = lower_expression(function, *node.children[0], "BOOL");
        current_block(function).instructions.push_back(amir_store(result_name, left));
        // Reload rather than branching on `left` directly: build_bytecode's peephole optimizer
        // fuses an adjacent Load+Load+Binary+Store into one BINARY_LOCAL_LOCAL op and drops the
        // Binary's own temp entirely (same trap apply_script_global_scoping's mirroring hit
        // above) -- `left` is consumed here by both the Store just above and the Branch below,
        // and the peephole only looks at the Store, leaving the Branch referencing a temp that
        // never got emitted ("undefined bytecode temporary"). A fresh Load always materializes.
        const std::string left_reloaded = temp();
        current_block(function).instructions.push_back(amir_load(left_reloaded, result_name));

        const std::size_t rhs_block = add_block(function, "ShortCircuitRhs");
        const std::size_t end_block = add_block(function, "ShortCircuitEnd");
        // AndAlso: only evaluate the right side when the left side was true; short-circuit
        // straight to the merge (carrying the already-false left value) otherwise. OrElse is the
        // mirror image.
        if (is_and) {
            current_block(function).instructions.push_back(
                amir_branch(left_reloaded, block_name(function, rhs_block), block_name(function, end_block)));
        } else {
            current_block(function).instructions.push_back(
                amir_branch(left_reloaded, block_name(function, end_block), block_name(function, rhs_block)));
        }

        current_block_ = rhs_block;
        const std::string right = lower_expression(function, *node.children[1], "BOOL");
        current_block(function).instructions.push_back(amir_store(result_name, right));
        current_block(function).instructions.push_back(amir_jump(block_name(function, end_block)));

        current_block_ = end_block;
        const std::string result = temp();
        current_block(function).instructions.push_back(amir_load(result, result_name));
        return result;
    }

    std::string lower_expression(AmirFunction& function, const CanonicalAstNode& node, const std::string& expected_type = "") {
        switch (node.kind) {
            case AstKind::Literal: {
                const std::string result = temp();
                auto instruction = amir_const(result, node.text);
                instruction.result_type = type_of_expression(node, expected_type);
                current_block(function).instructions.push_back(std::move(instruction));
                return result;
            }
            case AstKind::InterpolatedString: {
                const std::string result = temp();
                current_block(function).instructions.push_back(amir_eval(result, render_ast_expression(node)));
                return result;
            }
            case AstKind::Variable:
                return lower_variable(current_block(function), node.name);
            case AstKind::Unary: {
                const std::string result_type = type_of_expression(node, expected_type);
                const std::string value = node.children.empty() ? lower_fallback(current_block(function), node) : lower_expression(function, *node.children[0], result_type);
                const std::string result = temp();
                auto instruction = amir_unary(result, ast_operator(node.op), value);
                instruction.result_type = result_type;
                const std::string unary_type = type_of_expression(*node.children.front(), result_type);
                if (!unary_type.empty() || !result_type.empty()) instruction.operand_types = {unary_type};
                current_block(function).instructions.push_back(std::move(instruction));
                return result;
            }
            case AstKind::Binary:
            case AstKind::Logical: {
                if (node.children.size() != 2) return lower_fallback(current_block(function), node);
                // ANDALSO/ORELSE are ArcoBASIC's short-circuit logical operators (AND/OR/NOT stay
                // deliberately bitwise and always evaluate both sides -- see runtime_tests.cpp);
                // the generic path below evaluates both children unconditionally before emitting
                // a plain BINARY, which is correct for bitwise AND/OR but silently drops the
                // "short" from ANDALSO/ORELSE (found via arcoflow.abas's
                // `event.Type == "key" ANDALSO event.Action == ...` evaluating event.Action on
                // every event, including ones with no Action field at all: "undefined property:
                // Action" instead of just skipping the right-hand side).
                if (node.op == TokenType::AndAlso || node.op == TokenType::OrElse) {
                    return lower_short_circuit_logical(function, node);
                }
                const std::string result_type = type_of_expression(node, expected_type);
                const std::string inferred_left_type = type_of_expression(*node.children[0]);
                const std::string inferred_right_type = type_of_expression(*node.children[1]);
                const std::string left_type = inferred_left_type.empty() ? expected_type : inferred_left_type;
                const std::string right_type = inferred_right_type.empty() ? (left_type.empty() ? expected_type : left_type) : inferred_right_type;
                const bool comparison = result_type == "BOOL";
                if (!comparison && is_fixed_integer_type(left_type) && is_fixed_integer_type(right_type) && left_type != right_type) {
                    report_integer_error("operator " + ast_operator(node.op) + " requires matching fixed-width integer operands; received " + left_type + " and " + right_type + ".");
                }
                const auto is_address_domain = [](const std::string& type) {
                    return type == "PTR" || type == "VIRTUALPTR" || type == "PHYSICALPTR" || type == "MMIOPTR" || type == "IOPORT";
                };
                const auto is_runtime_handle = [](const std::string& type) {
                    return type == "SURFACE" || type == "WINDOW" || type == "IMAGE" || type == "FONT" ||
                           type == "FILE" || type == "DIRECTORY" || type == "TIMER" || type == "THREAD" ||
                           type == "MUTEX" || type == "SOCKET";
                };
                if (!comparison && (is_address_domain(left_type) || is_address_domain(right_type))) {
                    report_integer_error("address-domain values do not support ordinary arithmetic; use ADDRESS.Offset/AlignUp/AlignDown or PORT.Offset.");
                }
                if (!comparison && (is_port_type(left_type) || is_port_type(right_type))) {
                    report_integer_error("IOPORT values do not support ordinary arithmetic; use PORT.Offset for related ports.");
                }
                if (!comparison && (is_runtime_handle(left_type) || is_runtime_handle(right_type))) {
                    report_integer_error("runtime object handles do not support arithmetic or bitwise operations; use the owning library API.");
                }
                if (!comparison && (left_type == "BOOL" || right_type == "BOOL") &&
                    (node.op == TokenType::Plus || node.op == TokenType::Minus || node.op == TokenType::Star ||
                     node.op == TokenType::Backslash || node.op == TokenType::Mod)) {
                    report_integer_error("Boolean values do not support arithmetic in the UEFI systems profile.");
                }
                if ((node.op == TokenType::Backslash || node.op == TokenType::Mod) && node.children[1]->kind == AstKind::Literal &&
                    (node.children[1]->text == "0" || node.children[1]->text == "0x0" || node.children[1]->text == "0X0" ||
                     node.children[1]->text == "0b0" || node.children[1]->text == "0B0")) {
                    report_integer_error("integer division by a compile-time zero divisor is not allowed");
                }
                const std::string operand_expected = comparison ? left_type : result_type;
                const std::string left = lower_expression(function, *node.children[0], operand_expected);
                const std::string right = lower_expression(function, *node.children[1], operand_expected);
                const std::string result = temp();
                auto instruction = amir_binary(result, ast_operator(node.op), left, right);
                instruction.result_type = result_type;
                if (!left_type.empty() || !right_type.empty() || !result_type.empty()) instruction.operand_types = {left_type, right_type};
                current_block(function).instructions.push_back(std::move(instruction));
                return result;
            }
            case AstKind::Call:
            case AstKind::MethodCall:
            case AstKind::SuperCall:
                return lower_call(function, node);
            case AstKind::PortOperation: {
                const std::string full_name = upper_ascii(node.name);
                const std::string operation = full_name.rfind("PORT.", 0) == 0 ? full_name.substr(5) : full_name;
                const auto canonical = [&](const std::string& name) {
                    if (name == "READBYTE") return std::string("READ8");
                    if (name == "READWORD") return std::string("READ16");
                    if (name == "READDWORD") return std::string("READ32");
                    if (name == "WRITEBYTE") return std::string("WRITE8");
                    if (name == "WRITEWORD") return std::string("WRITE16");
                    if (name == "WRITEDWORD") return std::string("WRITE32");
                    return name;
                };
                const std::string op = canonical(operation);
                std::vector<std::string> args;
                for (const auto& child : node.children) args.push_back(lower_expression(function, *child));
                if (op == "ADDRESS") {
                    if (args.size() != 1) { report_integer_error("PORT.Address expects one U16 argument"); return lower_fallback(current_block(function), node); }
                    const std::string input_type = type_of_expression(*node.children[0], "U16");
                    if (input_type != "U16") report_integer_error("PORT.Address expects U16; received " + (input_type.empty() ? "unknown" : input_type));
                    if (node.children[0]->kind == AstKind::Literal) {
                        try {
                            std::string text = node.children[0]->text;
                            int base = 10;
                            if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { base = 16; text = text.substr(2); }
                            else if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) { base = 2; text = text.substr(2); }
                            if (std::stoull(text, nullptr, base) > 65535ULL) report_integer_error("PORT.Address literal is current_block(function) of range 0..65535");
                        } catch (...) {
                            report_integer_error("PORT.Address requires an exact integer literal or U16 value");
                        }
                    }
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_port("ADDRESS", result, std::move(args), "IOPORT", {input_type}));
                    return result;
                }
                if (op == "OFFSET") {
                    if (args.size() != 2) { report_integer_error("PORT.Offset expects IOPORT and I16 arguments"); return lower_fallback(current_block(function), node); }
                    if (node.children[1]->kind != AstKind::Literal) {
                        report_integer_error("PORT.Offset requires a statically known displacement in the initial x86-64 systems target");
                    } else {
                        try {
                            const long long displacement = std::stoll(node.children[1]->text, nullptr, 0);
                            if (displacement < -65535LL || displacement > 65535LL) report_integer_error("PORT.Offset displacement is outside the supported I16 range");
                        } catch (...) {
                            report_integer_error("PORT.Offset displacement must be an exact integer literal");
                        }
                    }
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_port("OFFSET", result, std::move(args), "IOPORT", {"IOPORT", "I16"}));
                    return result;
                }
                const bool read = op == "READ8" || op == "READ16" || op == "READ32";
                const bool write = op == "WRITE8" || op == "WRITE16" || op == "WRITE32";
                if (!read && !write) {
                    report_integer_error("unknown port intrinsic " + node.name);
                    return lower_fallback(current_block(function), node);
                }
                const std::string width = op.substr(read ? 4 : 5);
                const std::string value_type = "U" + width;
                if (args.empty() || args.size() > (write ? 2U : 1U)) {
                    report_integer_error("PORT." + op + " has the wrong argument count");
                    return lower_fallback(current_block(function), node);
                }
                const std::string port_type = type_of_expression(*node.children[0]);
                if (port_type != "IOPORT") report_integer_error("PORT." + op + " expects IOPORT; received " + (port_type.empty() ? "unknown" : port_type));
                if (write) {
                    const std::string supplied = type_of_expression(*node.children[1], value_type);
                    if (supplied != value_type) report_integer_error("PORT." + op + " expects " + value_type + "; received " + (supplied.empty() ? "unknown" : supplied));
                }
                const std::string result = temp();
                current_block(function).instructions.push_back(amir_port(op, result, std::move(args), read ? value_type : "", write ? std::vector<std::string>{"IOPORT", value_type} : std::vector<std::string>{"IOPORT"}));
                return result;
            }
            case AstKind::MemoryOperation: {
                const std::string name = upper_ascii(node.name);
                if (name == "ADDRESS.LOCAL") {
                    if (node.children.size() != 1 || node.children.front()->kind != AstKind::Variable) {
                        report_integer_error("ADDRESS.Local expects one local variable");
                        return lower_fallback(current_block(function), node);
                    }
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("LOCAL", result, {node.children.front()->name}, "PTR", {"PTR"}));
                    return result;
                }
                std::vector<std::string> args;
                for (const auto& child : node.children) args.push_back(lower_expression(function, *child));
                const auto barrier = [&](const std::string& n) {
                    return n == "CPU.READBARRIER" || n == "CPU.WRITEBARRIER" || n == "CPU.MEMORYBARRIER";
                };
                if (barrier(name)) {
                    current_block(function).instructions.push_back(amir_barrier(name.substr(4)));
                    return temp();
                }
                if (name == "CPU.READCR2" || name == "CPU.READCR3" || name == "CPU.WRITECR3" || name == "CPU.INVALIDATEPAGE" || name == "CPU.LOADGDT" || name == "CPU.LOADIDT" || name == "CPU.LOADTASKREGISTER") {
                    const std::string op = name == "CPU.READCR2" ? "READCR2" : (name == "CPU.READCR3" ? "READCR3" : (name == "CPU.WRITECR3" ? "WRITECR3" : "INVLPG"));
                    const std::string descriptor_op = name == "CPU.LOADGDT" ? "LGDT" : (name == "CPU.LOADIDT" ? "LIDT" : (name == "CPU.LOADTASKREGISTER" ? "LTR" : op));
                    const std::string result = temp();
                    const bool read = name == "CPU.READCR2" || name == "CPU.READCR3";
                    current_block(function).instructions.push_back(amir_memory(descriptor_op, read ? result : "", std::move(args), read ? "U64" : "", {"U64"}));
                    return result;
                }
                if (name == "CPU.READRSP") {
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("READRSP", result, {}, "U64", {}));
                    return result;
                }
                std::string op = name;
                if (name.rfind("UEFI.GOP.", 0) == 0) {
                    const std::string gop_op = name.substr(9);
                    const bool discover = gop_op == "DISCOVER";
                    const bool field = gop_op == "MODE" || gop_op == "FRAMEBUFFERBASE" || gop_op == "FRAMEBUFFERSIZE" || gop_op == "WIDTH" || gop_op == "HEIGHT" || gop_op == "PIXELSPERSCANLINE" || gop_op == "PIXELFORMAT";
                    if (node.children.size() != 1 || (discover && type_of_expression(*node.children.front()) != "UEFI.SystemTable") || (field && type_of_expression(*node.children.front()) != "UEFI.GraphicsOutputProtocol")) {
                        report_integer_error("UEFI.GOP." + gop_op + " received an invalid operand");
                    }
                    const std::string result = temp();
                    std::string target = discover ? "GOPDISCOVER" : "GOP" + gop_op;
                    current_block(function).instructions.push_back(amir_memory(target, result, std::move(args), type_of_expression(node), {discover ? "UEFI.SystemTable" : "UEFI.GraphicsOutputProtocol"}));
                    return result;
                }
                if (name == "GRAPHICS.PRIMARYSURFACE") {
                    // Freestanding bootstrap representation: the opaque SURFACE handle is the
                    // discovered GOP protocol pointer. No application-visible GOP type or
                    // framebuffer address is exposed; later backend work will replace this with
                    // a runtime-owned surface record without changing source.
                    const std::string system_table = lower_variable(current_block(function), "systemTable");
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("GOPDISCOVER", result, {system_table}, "SURFACE", {"UEFI.SystemTable"}));
                    return result;
                }
                if (name == "GRAPHICS.BIND") {
                    if (node.children.size() != 1 || type_of_expression(*node.children.front()) != "SURFACE") {
                        report_integer_error("GRAPHICS.Bind expects one SURFACE handle");
                    }
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("GRAPHICSBIND", result, std::move(args), "", {"SURFACE"}));
                    return result;
                }
                if (name.rfind("GRAPHICS.", 0) == 0) {
                    const std::string result = temp();
                    auto call = amir_call_value(result, name, std::move(args));
                    call.result_type = type_of_expression(node, expected_type);
                    if (name == "GRAPHICS.DESTROYSURFACE") call.ownership = "Consumed";
                    else call.ownership = "Borrowed";
                    current_block(function).instructions.push_back(std::move(call));
                    return result;
                }
                if (op.rfind("ADDRESS.", 0) == 0) op = op.substr(8);
                else if (op.rfind("MEMORY.", 0) == 0) op = op.substr(7);
                const bool physical_write = op == "PHYSICALWRITE64";
                const bool read = op == "READ8" || op == "READ16" || op == "READ32" || op == "READ64";
                const bool write = op == "WRITE8" || op == "WRITE16" || op == "WRITE32" || op == "WRITE64" || physical_write;
                const std::string result_type = type_of_expression(node, expected_type);
                const bool constructor = op == "PHYSICAL" || op == "VIRTUAL" || op == "MMIO" || op == "VALUE" || op == "MAP" || op == "MAPDEVICE";
                if (name == "ADDRESS.PHYSICAL" || name == "ADDRESS.VIRTUAL") {
                    if (node.children.size() != 1 || type_of_expression(*node.children.front(), "U64") != "U64") report_integer_error(name + " expects one U64 argument");
                } else if (name == "ADDRESS.MMIO") {
                    if (node.children.size() != 1 || type_of_expression(*node.children.front()) != "VIRTUALPTR") report_integer_error("ADDRESS.MMIO expects one VIRTUALPTR argument");
                } else if (name == "ADDRESS.VALUE") {
                    if (node.children.size() != 1) report_integer_error("ADDRESS.VALUE expects one address argument");
                } else if (name == "ADDRESS.OFFSET") {
                    if (node.children.size() != 2) report_integer_error("ADDRESS.OFFSET expects an address and a displacement");
                } else if (name == "ADDRESS.ALIGNUP" || name == "ADDRESS.ALIGNDOWN" || name == "ADDRESS.ISALIGNED") {
                    if (node.children.size() != 2) report_integer_error(name + " expects an address and an alignment");
                    else if (node.children[1]->kind == AstKind::Literal) {
                        try {
                            const auto alignment = std::stoull(node.children[1]->text, nullptr, 0);
                            if (alignment == 0 || (alignment & (alignment - 1)) != 0) report_integer_error(name + " alignment must be a nonzero power of two");
                        } catch (...) {
                            report_integer_error(name + " alignment must be an exact integer literal or validated integer");
                        }
                    }
                } else if (name == "MEMORY.MAP") {
                    if (node.children.size() != 3 || type_of_expression(*node.children.front()) != "PHYSICALPTR") report_integer_error("MEMORY.MAP expects PHYSICALPTR, U64 length, and MEMORYMAPFLAGS");
                } else if (name == "MEMORY.MAPDEVICE") {
                    if (node.children.size() != 2 || type_of_expression(*node.children.front()) != "PHYSICALPTR") report_integer_error("MEMORY.MAPDEVICE expects PHYSICALPTR and U64 length");
                }
                if ((read || write) && !physical_write && !node.children.empty() && type_of_expression(*node.children[0]) == "PHYSICALPTR") {
                    report_integer_error("MEMORY access cannot dereference PHYSICALPTR; map it to VIRTUALPTR or MMIOPTR first");
                }
                if (physical_write && (node.children.size() != 2 || type_of_expression(*node.children[0]) != "PHYSICALPTR" || type_of_expression(*node.children[1], "U64") != "U64")) {
                    report_integer_error("MEMORY.PhysicalWrite64 expects PHYSICALPTR and U64");
                }
                if (name == "ADDRESS.ALIGNUP" || name == "ADDRESS.ALIGNDOWN" || name == "ADDRESS.OFFSET") {
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory(op, result, std::move(args), result_type, {}));
                    return result;
                }
                if (name == "ADDRESS.ISALIGNED") {
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory(op, result, std::move(args), "BOOL", {}));
                    return result;
                }
                if (read || write || name == "ADDRESS.PHYSICAL" || name == "ADDRESS.VIRTUAL" || name == "ADDRESS.VALUE" || name == "ADDRESS.MMIO" || name == "MEMORY.MAP" || name == "MEMORY.MAPDEVICE") {
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory(op, result, std::move(args), write ? "" : result_type, {}));
                    return result;
                }
                if (!constructor && name != "ADDRESS.OFFSET" && name != "ADDRESS.ALIGNUP" && name != "ADDRESS.ALIGNDOWN" && name != "ADDRESS.ISALIGNED") report_integer_error("unknown memory/address intrinsic " + node.name);
                return lower_fallback(current_block(function), node);
            }
            case AstKind::Index: {
                if (node.children.size() != 2) return lower_fallback(current_block(function), node);
                const std::string target = lower_expression(function, *node.children[0]);
                const std::string index = lower_expression(function, *node.children[1]);
                const std::string result = temp();
                current_block(function).instructions.push_back(amir_index(result, target, index));
                return result;
            }
            case AstKind::Slice: {
                if (node.children.size() != 4) return lower_fallback(current_block(function), node);
                const std::string target = lower_expression(function, *node.children[0]);
                auto lower_optional = [&](const CanonicalAstNodePtr& child) {
                    if (child) return lower_expression(function, *child);
                    const std::string omitted = temp();
                    current_block(function).instructions.push_back(amir_const(omitted, "nothing"));
                    return omitted;
                };
                const std::string start = lower_optional(node.children[1]);
                const std::string end = lower_optional(node.children[2]);
                const std::string step = lower_optional(node.children[3]);
                const std::string result = temp();
                current_block(function).instructions.push_back(amir_slice(result, target, start, end, step));
                return result;
            }
            case AstKind::Copy: {
                if (node.children.empty()) return lower_fallback(current_block(function), node);
                const std::string value = lower_expression(function, *node.children[0]);
                const std::string result = temp();
                current_block(function).instructions.push_back(amir_copy(result, value));
                return result;
            }
            case AstKind::AddressOf: {
                const std::string result = temp();
                current_block(function).instructions.push_back(amir_address_of(result, node.name));
                return result;
            }
            case AstKind::Array: {
                std::vector<std::string> items;
                for (const auto& child : node.children) items.push_back(lower_expression(function, *child));
                const std::string result = temp();
                current_block(function).instructions.push_back(amir_array(result, std::move(items)));
                return result;
            }
            case AstKind::ArrayComprehension: {
                if (node.children.size() < 2) return lower_fallback(current_block(function), node);
                const std::string result_name = hidden_name("comp_result");
                const std::string items_name = hidden_name("comp_items");
                const std::string index_name = hidden_name("comp_index");
                const std::string binding_name = hidden_name("comp_item");
                const CanonicalAstNodePtr result_expr = replace_variable_name(node.children[0], node.name, binding_name);
                const CanonicalAstNodePtr filter_expr = node.children.size() > 2
                    ? replace_variable_name(node.children[2], node.name, binding_name)
                    : nullptr;
                const std::string empty = temp();
                current_block(function).instructions.push_back(amir_array(empty, {}));
                current_block(function).instructions.push_back(amir_store(result_name, empty));
                // lower_expression evaluated into a local first, current_block(function) fetched fresh
                // afterward: current_block(function) returns a reference into function.blocks (a
                // std::vector), which control-flow-bearing sub-expressions (this one, ANDALSO/ORELSE)
                // grow via add_block -- inlining both in one push_back call risks the reference going
                // stale mid-expression if the vector reallocates (see lower_short_circuit_logical).
                const std::string comprehension_items = lower_expression(function, *node.children[1]);
                current_block(function).instructions.push_back(amir_store(items_name, comprehension_items));
                const std::string zero = temp();
                current_block(function).instructions.push_back(amir_const(zero, "0"));
                current_block(function).instructions.push_back(amir_store(index_name, zero));

                const std::size_t cond_block = add_block(function, "ComprehensionCond");
                const std::size_t body_block = add_block(function, "ComprehensionBody");
                const std::size_t append_block = add_block(function, "ComprehensionAppend");
                const std::size_t inc_block = add_block(function, "ComprehensionInc");
                const std::size_t end_block = add_block(function, "ComprehensionEnd");
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
                current_block(function).instructions.push_back(amir_store(binding_name, item));
                if (filter_expr) {
                    const std::string filter = lower_expression(function, *filter_expr);
                    current_block(function).instructions.push_back(
                        amir_branch(filter, block_name(function, append_block), block_name(function, inc_block)));
                } else {
                    current_block(function).instructions.push_back(amir_jump(block_name(function, append_block)));
                }

                current_block_ = append_block;
                const std::string result_array = temp();
                current_block(function).instructions.push_back(amir_load(result_array, result_name));
                const std::string value = lower_expression(function, *result_expr);
                const std::string ignored = temp();
                current_block(function).instructions.push_back(amir_call_value(ignored, "Array.Add", {result_array, value}));
                current_block(function).instructions.push_back(amir_jump(block_name(function, inc_block)));

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
                const std::string result = temp();
                current_block(function).instructions.push_back(amir_load(result, result_name));
                return result;
            }
            case AstKind::Tuple: {
                std::vector<std::string> items;
                for (const auto& child : node.children) items.push_back(lower_expression(function, *child));
                const std::string result = temp();
                current_block(function).instructions.push_back(amir_tuple(result, std::move(items)));
                return result;
            }
            case AstKind::Object: {
                std::vector<std::string> fields;
                for (const auto& [key, value] : node.named_children) {
                    fields.push_back(key + ":" + lower_expression(function, *value));
                }
                const std::string result = temp();
                current_block(function).instructions.push_back(amir_object(result, std::move(fields)));
                return result;
            }
            default:
                return lower_fallback(current_block(function), node);
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
            auto instruction = amir_load(result, name);
            instruction.result_type = types_.count(name) ? types_.at(name) : "U64";
            out.instructions.push_back(std::move(instruction));
            return result;
        }
        std::string target = temp();
        auto load = amir_load(target, parts.front());
        load.result_type = types_.count(parts.front()) ? types_.at(parts.front()) : "U64";
        out.instructions.push_back(std::move(load));
        for (std::size_t i = 1; i < parts.size(); ++i) {
            const std::string property = temp();
            out.instructions.push_back(amir_const(property, "\"" + escaped(parts[i]) + "\""));
            const std::string indexed = temp();
            auto index_instruction = amir_index(indexed, target, property);
            index_instruction.result_type = "U64";
            out.instructions.push_back(std::move(index_instruction));
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
        const std::string upper_target = upper_ascii(target);
        if (upper_target == "CPU.READCR2" || upper_target == "CPU.READCR3") {
            auto instruction = amir_memory(upper_target == "CPU.READCR2" ? "READCR2" : "READCR3", result, {}, "U64", {});
            current_block(function).instructions.push_back(std::move(instruction));
            return result;
        }
        if (upper_target == "CPU.WRITECR3" || upper_target == "CPU.INVALIDATEPAGE" || upper_target == "CPU.LOADGDT" || upper_target == "CPU.LOADIDT" || upper_target == "CPU.LOADTASKREGISTER") {
            const std::string op = upper_target == "CPU.WRITECR3" ? "WRITECR3" : (upper_target == "CPU.INVALIDATEPAGE" ? "INVLPG" : (upper_target == "CPU.LOADGDT" ? "LGDT" : (upper_target == "CPU.LOADIDT" ? "LIDT" : "LTR")));
            current_block(function).instructions.push_back(amir_memory(op, "", std::move(args), "", {"U64"}));
            return result;
        }
        AmirInstruction instruction = amir_call_value(result, target, std::move(args));
        if (upper_target == "GRAPHICS.CREATESURFACE" || upper_target == "GRAPHICS.PRIMARYSURFACE" || upper_target == "GRAPHICS.CREATEWINDOW" ||
            upper_target == "GRAPHICS.CREATEIMAGE" || upper_target == "FILES.OPEN" || upper_target == "NETWORK.CONNECT") {
            instruction.result_type = type_of_expression(node);
            instruction.ownership = upper_target == "GRAPHICS.PRIMARYSURFACE" ? "Borrowed" : "Returned";
        } else if (upper_target == "GRAPHICS.BIND" || upper_target == "GRAPHICS.PUSHSURFACE" ||
                   upper_target == "GRAPHICS.CLEAR" || upper_target == "GRAPHICS.FILLRECT" ||
                   upper_target == "GRAPHICS.DRAWTEXT" || upper_target == "GRAPHICS.POPSURFACE") {
            instruction.ownership = "Borrowed";
        } else if (upper_target == "GRAPHICS.DESTROYSURFACE") {
            instruction.ownership = "Consumed";
        }
        const auto receiver_dot = target.find('.');
        const std::string receiver_name = receiver_dot == std::string::npos ? "" : target.substr(0, receiver_dot);
        // SELF is a parameter now that class methods bind an implicit receiver (see lower_class),
        // but SELF.Method(...) is always ordinary instance dispatch, never the freestanding/UEFI
        // external-call path this heuristic exists for -- exclude it explicitly rather than
        // route every method-calling-another-method-on-itself through CALL_EXTERNAL, which the
        // bytecode VM doesn't implement.
        if (node.kind == AstKind::MethodCall && node.secondary_name != "SELF" &&
            (has_parameter(function, node.secondary_name) ||
            (types_.count(receiver_name) != 0 && types_.at(receiver_name).rfind("UEFI.", 0) == 0))) {
            instruction.kind = AmirInstruction::Kind::CallExternal;
            if (!receiver_name.empty() && types_.count(receiver_name) != 0) instruction.operand_types = {types_.at(receiver_name)};
        }
        current_block(function).instructions.push_back(std::move(instruction));
        return result;
    }

    void lower_statements(AmirFunction& function, const std::vector<CanonicalAstNodePtr>& statements) {
        for (const auto& statement : statements) {
            lower_statement(function, *statement);
            const auto& instructions = current_block(function).instructions;
            if (!instructions.empty() && (instructions.back().kind == AmirInstruction::Kind::CpuHaltForever ||
                                          instructions.back().kind == AmirInstruction::Kind::Throw)) {
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
            case AstKind::SliceAssign:
                lower_slice_assignment(function, node);
                break;
            case AstKind::Destructure:
                if (!node.children.empty()) {
                    current_block(function).instructions.push_back(
                        amir_destructure(node.names, lower_expression(function, *node.children[0])));
                }
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
                if (node.name == "CPU.HaltForever") current_block(function).instructions.push_back(amir_cpu_halt_forever());
                else if (node.name == "CPU.Pause") current_block(function).instructions.push_back(amir_cpu_pause());
                else if (node.name == "CPU.ReadBarrier") current_block(function).instructions.push_back(amir_barrier("READBARRIER"));
                else if (node.name == "CPU.WriteBarrier") current_block(function).instructions.push_back(amir_barrier("WRITEBARRIER"));
                else if (node.name == "CPU.MemoryBarrier") current_block(function).instructions.push_back(amir_barrier("MEMORYBARRIER"));
                else if (node.name == "CPU.DisableInterrupts") current_block(function).instructions.push_back(amir_barrier("DISABLEINTERRUPTS"));
                else if (node.name == "CPU.EnableInterrupts") current_block(function).instructions.push_back(amir_barrier("ENABLEINTERRUPTS"));
                else if (node.name == "CPU.Breakpoint") current_block(function).instructions.push_back(amir_barrier("BREAKPOINT"));
                else current_block(function).instructions.push_back(amir_cpu_halt());
                break;
            case AstKind::ExpressionStatement:
                lower_expression_statement(function, node);
                break;
            case AstKind::NoOp:
            case AstKind::Comment:
                break;
            case AstKind::Return: {
                // Evaluate the return value (if any) before touching current_block(function): see
                // the comment on the ArrayComprehension case above -- inlining lower_expression
                // into this push_back would risk current_block(function)'s reference into
                // function.blocks going stale if the expression's own lowering reallocates that
                // vector (confirmed live with `RETURN a ANDALSO b ANDALSO c ANDALSO d`, which
                // silently returned "nothing" instead of the real value).
                if (node.children.empty()) {
                    current_block(function).instructions.push_back(amir_return("VALUE", "nothing"));
                } else {
                    const std::string return_value = lower_expression(function, *node.children[0]);
                    current_block(function).instructions.push_back(amir_return(function.return_type, return_value));
                }
                break;
            }
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
            case AstKind::Throw:
                if (node.children.empty()) {
                    current_block(function).instructions.push_back(amir_unsupported("THROW without message"));
                } else {
                    const std::string thrown_value = lower_expression(function, *node.children[0]);
                    current_block(function).instructions.push_back(amir_throw(thrown_value));
                }
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
        const std::string printed_value = lower_expression(function, expr);
        current_block(function).instructions.push_back(amir_call("Runtime.Print", {printed_value}));
    }

    void lower_assignment(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.empty()) return;
        const std::string declared_type = node.type_name.empty()
            ? (types_.count(node.name) ? types_.at(node.name) : type_of_expression(*node.children.back()))
            : node.type_name;
        if (!declared_type.empty()) types_[node.name] = declared_type;
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
        const std::string value = lower_expression(function, *node.children.back(), declared_type);
        if (indexes.empty()) {
            current_block(function).instructions.push_back(amir_store(node.name, value));
        } else {
            indexes.push_back(value);
            current_block(function).instructions.push_back(amir_store_index(parts.empty() ? node.name : parts.front(), std::move(indexes)));
        }
    }

    void lower_slice_assignment(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.size() != 3 || !node.children[2]) return;
        auto lower_optional = [&](const CanonicalAstNodePtr& child) {
            if (child) return lower_expression(function, *child);
            const std::string omitted = temp();
            current_block(function).instructions.push_back(amir_const(omitted, "nothing"));
            return omitted;
        };
        const std::string start = lower_optional(node.children[0]);
        const std::string end = lower_optional(node.children[1]);
        const std::string replacement = lower_expression(function, *node.children[2]);
        current_block(function).instructions.push_back(amir_store_slice(node.name, start, end, replacement));
    }

    void lower_compound(AmirFunction& function, const CanonicalAstNode& node) {
        if (node.children.empty()) return;
        const std::string current = temp();
        const std::string current_type = types_.count(node.name) ? types_.at(node.name) : "";
        auto load = amir_load(current, node.name);
        load.result_type = current_type;
        current_block(function).instructions.push_back(std::move(load));
        const std::string value = lower_expression(function, *node.children[0], current_type);
        const std::string result = temp();
        auto instruction = amir_binary(result, ast_operator(node.op), current, value);
        instruction.result_type = current_type;
        if (!current_type.empty()) instruction.operand_types = {current_type, current_type};
        current_block(function).instructions.push_back(std::move(instruction));
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
        if (kind == AstKind::Call || kind == AstKind::MethodCall || kind == AstKind::SuperCall || kind == AstKind::PortOperation || kind == AstKind::MemoryOperation) {
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
        std::string condition_type = type_of_expression(*node.children[0]);
        if (condition_type.empty() && node.children[0]->kind == AstKind::Literal) condition_type = "U64";
        if (condition_type != "BOOL") {
            report_integer_error("IF condition must be BOOL under #RUNTIME NONE; received " + (condition_type.empty() ? "unknown" : condition_type) + ". Compare the value explicitly.");
        }
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
        std::string condition_type = type_of_expression(*node.children[0]);
        if (condition_type.empty() && node.children[0]->kind == AstKind::Literal) condition_type = "U64";
        if (condition_type != "BOOL") {
            report_integer_error("WHILE condition must be BOOL under #RUNTIME NONE; received " + (condition_type.empty() ? "unknown" : condition_type) + ". Compare the value explicitly.");
        }
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
        const std::string for_start = lower_expression(function, *node.children[0]);
        current_block(function).instructions.push_back(amir_store(node.name, for_start));
        const std::string for_end = lower_expression(function, *node.children[1]);
        current_block(function).instructions.push_back(amir_store(end_name, for_end));
        if (node.children.size() >= 3) {
            const std::string for_step = lower_expression(function, *node.children[2]);
            current_block(function).instructions.push_back(amir_store(step_name, for_step));
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
        const std::string each_items = lower_expression(function, *node.children[0]);
        current_block(function).instructions.push_back(amir_store(items_name, each_items));
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
        const std::string select_value = lower_expression(function, *node.children[0]);
        current_block(function).instructions.push_back(amir_store(target_name, select_value));
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
        const auto saved_types = types_;
        current_block_ = 0;
        loop_stack_.clear();
        types_.clear();
        for (const auto& param : node.parameters) {
            if (!param.type_name.empty()) types_[param.name] = param.type_name;
        }
        lower_statements(function, ast_group(node, "body"));
        ensure_terminated(function, current_block_, "VALUE", "nothing");
        current_block_ = saved_block;
        loop_stack_ = saved_loops;
        types_ = saved_types;
        module_.functions.push_back(std::move(function));
    }

    // Compiles a CLASS declaration into real, callable bytecode functions instead of the
    // DECLARE_CLASS marker alone (which the bytecode VM treats as a no-op -- see
    // BytecodeOp::DeclareClass in execute_function). Synthesizes, per class:
    //   - ClassName.Method for each method (as before), now with an implicit SELF parameter
    //     (unless SHARED) so a method body's `SELF.Field` references have something bound to
    //     read.
    //   - ClassName.__new: builds a field-initialized instance, delegating to the parent's __new
    //     first (recursively) so inherited fields are present before this class's own field
    //     defaults and __class overlay them -- the same order the tree-walking interpreter's
    //     ClassStmt::exec/.__new (src/frontend/parser.cpp) already uses, just compiled instead of
    //     evaluated live.
    //   - ClassName itself: the public constructor. Calls __new, then Init (the CONSTRUCTOR body,
    //     which the method loop above already compiles under that name) if the class declares
    //     one, forwarding the constructor's own arguments positionally.
    // Instance method call dispatch (`instance.Method(...)` resolving to the *runtime* type of
    // `instance`, not whatever class the compile-time receiver expression happens to be) is a
    // separate fix in the CallValue interpreter case, since it's inherently a runtime concern.
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

        module_.class_parents[node.name] = node.secondary_name;

        const CanonicalAstNode* init_method = nullptr;
        for (const auto& method : ast_group(node, "methods")) {
            if (method->kind != AstKind::ClassMethod || method->flag2) continue;
            AmirFunction function;
            function.name = node.name + "." + method->name;
            function.return_type = method->type_name.empty() ? "VALUE" : method->type_name;
            if (!method->flag) function.params.push_back("SELF");
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
            if (method->name == "Init") init_method = method.get();
        }

        {
            AmirFunction new_function;
            new_function.name = node.name + ".__new";
            new_function.return_type = "VALUE";
            new_function.blocks.push_back(AmirBlock{"Entry"});
            const std::size_t saved_block = current_block_;
            const auto saved_loops = loop_stack_;
            current_block_ = 0;
            loop_stack_.clear();

            const std::string base_temp = temp();
            if (!node.secondary_name.empty()) {
                current_block(new_function).instructions.push_back(amir_call_value(base_temp, node.secondary_name + ".__new", {}));
            } else {
                current_block(new_function).instructions.push_back(amir_object(base_temp, {}));
            }
            const std::string instance_local = "__instance";
            current_block(new_function).instructions.push_back(amir_store(instance_local, base_temp));

            const std::string class_key = temp();
            current_block(new_function).instructions.push_back(amir_const(class_key, "\"__class\""));
            const std::string class_value = temp();
            current_block(new_function).instructions.push_back(amir_const(class_value, "\"" + escaped(node.name) + "\""));
            current_block(new_function).instructions.push_back(amir_store_index(instance_local, {class_key, class_value}));

            for (const auto& field : ast_group(node, "fields")) {
                if (field->kind != AstKind::ClassField || field->flag) continue;
                const std::string field_key = temp();
                current_block(new_function).instructions.push_back(amir_const(field_key, "\"" + escaped(field->name) + "\""));
                std::string field_value;
                if (!field->children.empty() && field->children[0]) {
                    field_value = lower_expression(new_function, *field->children[0]);
                } else {
                    field_value = temp();
                    current_block(new_function).instructions.push_back(amir_const(field_value, "nothing"));
                }
                current_block(new_function).instructions.push_back(amir_store_index(instance_local, {field_key, field_value}));
            }

            const std::string result_temp = lower_variable(current_block(new_function), instance_local);
            current_block(new_function).instructions.push_back(amir_return("VALUE", result_temp));
            current_block_ = saved_block;
            loop_stack_ = saved_loops;
            module_.functions.push_back(std::move(new_function));
        }

        {
            AmirFunction ctor_function;
            ctor_function.name = node.name;
            ctor_function.return_type = "VALUE";
            std::vector<std::string> forward_param_names;
            if (init_method) {
                for (const auto& param : init_method->parameters) {
                    ctor_function.params.push_back(parameter_text(param));
                    forward_param_names.push_back(param.name);
                }
            }
            ctor_function.blocks.push_back(AmirBlock{"Entry"});
            const std::size_t saved_block = current_block_;
            const auto saved_loops = loop_stack_;
            current_block_ = 0;
            loop_stack_.clear();

            const std::string instance_temp = temp();
            current_block(ctor_function).instructions.push_back(amir_call_value(instance_temp, node.name + ".__new", {}));
            if (init_method) {
                std::vector<std::string> call_args{instance_temp};
                for (const auto& name : forward_param_names) {
                    call_args.push_back(lower_variable(current_block(ctor_function), name));
                }
                const std::string discard = temp();
                current_block(ctor_function).instructions.push_back(amir_call_value(discard, node.name + ".Init", std::move(call_args)));
            }
            current_block(ctor_function).instructions.push_back(amir_return("VALUE", instance_temp));
            current_block_ = saved_block;
            loop_stack_ = saved_loops;
            module_.functions.push_back(std::move(ctor_function));
        }
    }

    void validate_module() {
        for (const auto& function : module_.functions) {
            std::vector<std::string> targets;
            std::unordered_set<std::string> seen_blocks;
            for (const auto& current : function.blocks) {
                if (!seen_blocks.insert(current.name).second) {
                    module_.diagnostics.push_back("duplicate A-MIR block " + function.name + "." + current.name);
                }
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
                for (std::size_t instruction_index = 0; instruction_index < current.instructions.size(); ++instruction_index) {
                    const auto& instruction = current.instructions[instruction_index];
                    const bool source_label_split = instruction.kind == AmirInstruction::Kind::Jump &&
                        instruction_index + 1 < current.instructions.size() &&
                        current.instructions[instruction_index + 1].kind == AmirInstruction::Kind::Label;
                    if (instruction_index + 1 < current.instructions.size() && is_terminal_instruction(instruction) && !source_label_split) {
                        module_.diagnostics.push_back("instruction after terminal operation in " + function.name + "." + current.name);
                    }
                    if (instruction.kind == AmirInstruction::Kind::Unsupported && !instruction.operands.empty()) {
                        module_.diagnostics.push_back("unsupported lowering in " + function.name + "." + current.name + ": " +
                                                      instruction.operands.front());
                    } else if (instruction.kind == AmirInstruction::Kind::Jump) {
                        validate_target(function.name, current.name, instruction.target, targets);
                    } else if (instruction.kind == AmirInstruction::Kind::Branch && instruction.operands.size() >= 3) {
                        validate_target(function.name, current.name, instruction.operands[1], targets);
                        validate_target(function.name, current.name, instruction.operands[2], targets);
                    } else if (instruction.kind == AmirInstruction::Kind::Branch) {
                        module_.diagnostics.push_back("malformed BRANCH in " + function.name + "." + current.name);
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
    std::unordered_map<std::string, std::string> types_;
    std::vector<LoopTarget> loop_stack_;
    int temporary_ = 0;
    int hidden_counter_ = 0;
    std::size_t block_counter_ = 0;
    std::size_t current_block_ = 0;
};

AmirModule build_amir(const std::vector<std::unique_ptr<Stmt>>& statements, const std::string& source_name,
                      std::optional<std::uint64_t> instruction_limit = std::nullopt) {
    AmirModule module = AstAmirBuilder(statements, source_name).build();
    module.instruction_limit = instruction_limit;
    return module;
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
            out << "    " << instruction.result;
            if (!instruction.result_type.empty()) out << " :" << instruction.result_type;
            out << " := " << (instruction.target == "~" ? "INT.NOT" : instruction.target == "-" ? "INT.NEG" : instruction.target)
                << ' ' << instruction.operands.front() << "\n";
            break;
        case AmirInstruction::Kind::Binary:
            {
            std::string operation = instruction.target;
            const std::string type = instruction.operand_types.empty() ? instruction.result_type : instruction.operand_types.front();
            const bool signed_value = type == "I8" || type == "I16" || type == "I32" || type == "I64";
            const bool typed = !instruction.result_type.empty() || !instruction.operand_types.empty();
            const std::string source_operation = operation;
            if (operation == "+") operation = "INT.ADD";
            else if (operation == "-") operation = "INT.SUB";
            else if (operation == "*") operation = "INT.MUL";
            else if (operation == "\\") operation = signed_value ? "INT.DIV_SIGNED" : "INT.DIV_UNSIGNED";
            else if (operation == "MOD") operation = signed_value ? "INT.MOD_SIGNED" : "INT.MOD_UNSIGNED";
            else if (operation == "&") operation = "INT.AND";
            else if (operation == "|") operation = "INT.OR";
            else if (operation == "^") operation = "INT.XOR";
            else if (operation == "<<") operation = "INT.SHL";
            else if (operation == ">>") operation = "INT.SHR";
            else if (operation == "SAR") operation = "INT.SAR";
            else if (operation == "==") operation = "INT.CMP_EQ";
            else if (operation == "!=") operation = "INT.CMP_NE";
            else if (operation == "<") operation = signed_value ? "INT.CMP_LT_SIGNED" : "INT.CMP_LT_UNSIGNED";
            else if (operation == "<=") operation = signed_value ? "INT.CMP_LE_SIGNED" : "INT.CMP_LE_UNSIGNED";
            else if (operation == ">") operation = signed_value ? "INT.CMP_GT_SIGNED" : "INT.CMP_GT_UNSIGNED";
            else if (operation == ">=") operation = signed_value ? "INT.CMP_GE_SIGNED" : "INT.CMP_GE_UNSIGNED";
            out << "    " << instruction.result;
            if (typed && !instruction.result_type.empty()) out << " :" << instruction.result_type;
            out << " := " << (typed ? operation : source_operation) << ' ' << instruction.operands[0] << ", " << instruction.operands[1];
            if (typed) {
                out << " [" << (instruction.operand_types.empty() ? "VALUE" : instruction.operand_types[0]) << ","
                    << (instruction.operand_types.size() < 2 ? "VALUE" : instruction.operand_types[1]) << "]";
            }
            out << "\n";
            }
            break;
        case AmirInstruction::Kind::CallValue:
            out << "    " << instruction.result;
            if (!instruction.result_type.empty()) out << " :" << instruction.result_type;
            out << " := CALL " << instruction.target;
            for (const auto& operand : instruction.operands) {
                out << ' ' << operand;
            }
            if (!instruction.ownership.empty()) out << " {" << instruction.ownership << "}";
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
        case AmirInstruction::Kind::CpuPause:
            out << "    CPU.PAUSE\n";
            break;
        case AmirInstruction::Kind::Port:
            out << "    ";
            if (!instruction.result.empty()) {
                out << instruction.result;
                if (!instruction.result_type.empty()) out << " :" << instruction.result_type;
                out << " = ";
            }
            out << "PORT." << instruction.target;
            if (!instruction.operands.empty()) {
                out << ' ' << instruction.operands[0];
                for (std::size_t i = 1; i < instruction.operands.size(); ++i) out << ", " << instruction.operands[i];
            }
            if (!instruction.operand_types.empty()) {
                out << " [";
                for (std::size_t i = 0; i < instruction.operand_types.size(); ++i) {
                    if (i) out << ',';
                    out << instruction.operand_types[i];
                }
                out << ']';
            }
            out << "\n";
            break;
        case AmirInstruction::Kind::Memory: {
            out << "    ";
            if (!instruction.result.empty()) {
                out << instruction.result;
                if (!instruction.result_type.empty()) out << " :" << instruction.result_type;
                out << " = ";
            }
            const bool address_op = instruction.target == "PHYSICAL" || instruction.target == "VIRTUAL" || instruction.target == "MMIO" ||
                instruction.target == "VALUE" || instruction.target == "OFFSET" || instruction.target == "ALIGNUP" || instruction.target == "ALIGNDOWN" || instruction.target == "ISALIGNED";
            if (instruction.target == "GOPDISCOVER") out << "UEFI.GOP.DISCOVER";
            else out << (address_op ? "ADDRESS." : "MEMORY.") << instruction.target;
            for (const auto& operand : instruction.operands) out << ' ' << operand;
            out << "\n";
            break;
        }
        case AmirInstruction::Kind::Barrier:
            out << "    CPU." << instruction.target << "\n";
            break;
        case AmirInstruction::Kind::Array:
            out << "    " << instruction.result << " := ARRAY";
            for (const auto& operand : instruction.operands) {
                out << ' ' << operand;
            }
            out << "\n";
            break;
        case AmirInstruction::Kind::Tuple:
            out << "    " << instruction.result << " := TUPLE";
            for (const auto& operand : instruction.operands) out << ' ' << operand;
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
        case AmirInstruction::Kind::Slice:
            out << "    " << instruction.result << " := SLICE " << instruction.target << ", "
                << instruction.operands[0] << ", " << instruction.operands[1] << ", " << instruction.operands[2] << "\n";
            break;
        case AmirInstruction::Kind::Copy:
            out << "    " << instruction.result << " := COPY " << instruction.operands.front() << "\n";
            break;
        case AmirInstruction::Kind::AddressOf:
            out << "    " << instruction.result << " := ADDRESSOF " << instruction.target << "\n";
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
        case AmirInstruction::Kind::StoreSlice:
            out << "    STORE_SLICE " << instruction.target << ' ' << instruction.operands[0] << ' '
                << instruction.operands[1] << ' ' << instruction.operands[2] << "\n";
            break;
        case AmirInstruction::Kind::Destructure:
            out << "    DESTRUCTURE " << instruction.target;
            for (const auto& operand : instruction.operands) out << ' ' << operand;
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
        case AmirInstruction::Kind::Throw:
            out << "    THROW " << instruction.operands.front() << "\n";
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
    if (module.instruction_limit.has_value()) {
        out << "METADATA INSTRUCTION_LIMIT " << *module.instruction_limit << "\n\n";
    }
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

// Renders the Microsoft x64 calling convention (arcology-os/docs/systems/calling-conventions.md) computed
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
    Throw = 23,
    Slice = 24,
    Copy = 25,
    StoreSlice = 26,
    Tuple = 27,
    Destructure = 28,
    AddressOf = 29,
    StoreConst = 30,
    BinaryLocalLocal = 31,
    BinaryLocalConst = 32,
    BranchLocalLocal = 33,
    IndexLocalConst = 34,
    Last = IndexLocalConst,
};

enum class BytecodeOperandKind {
    Empty,
    Temp,
    Constant,
    Local,
    InlineValue,
    Symbol,
};

// A slot holds an executed value in the shape the interpreter needs it in most: Number and
// Boolean are unpacked so hot numeric/comparison paths avoid touching the `Value` variant at
// all, and anything else (string, array, object, ...) falls back to carrying a full Value.
struct BytecodeSlot {
    enum class Kind {
        Undefined,
        Value,
        Number,
        Boolean,
    };
    Kind kind = Kind::Undefined;
    Value value;
    double number = 0.0;
    bool boolean = false;
};

BytecodeSlot slot_from_value(Value value) {
    BytecodeSlot slot;
    if (value.is_number()) {
        slot.kind = BytecodeSlot::Kind::Number;
        slot.number = value.as_number();
    } else if (value.is_bool()) {
        slot.kind = BytecodeSlot::Kind::Boolean;
        slot.boolean = value.truthy();
    } else {
        slot.kind = BytecodeSlot::Kind::Value;
        slot.value = std::move(value);
    }
    return slot;
}

struct BytecodeOperand {
    BytecodeOperandKind kind = BytecodeOperandKind::Empty;
    std::size_t index = 0;
    Value value;
    // For InlineValue operands, the slot form of `value` computed once by prepare_operand
    // instead of on every read.
    BytecodeSlot inline_slot;
};

struct BytecodeCursor {
    std::size_t block = 0;
    std::size_t instruction = 0;
};

struct BytecodeFunction;

// CALL_VALUE resolves its callee name against locals, globals, module functions, and
// host functions in that order (see the BytecodeOp::CallValue case in execute_function).
// For a fixed call site that name is almost always the same shape on every execution, so
// once resolution lands on a plain module function or host function (i.e. not a callable
// bound to a local/global variable, which can legitimately vary call to call) it is cached
// as an inline cache on the instruction. This is reset per-module by prepare_bytecode_module.
enum class CallSiteResolution : std::uint8_t {
    Unresolved,
    HostFunction,
    UserFunction,
};

// The textual operator ("+", "<=", "MOD", ...) carried by BINARY, BINARY_LOCAL_LOCAL,
// BINARY_LOCAL_CONST, and BRANCH_LOCAL_LOCAL instructions is fixed at compile time. Rather
// than re-comparing that string against every operator spelling on each execution, prepare
// time resolves it once into this enum for a plain switch at dispatch.
enum class NumericOp : std::uint8_t {
    Unknown,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Lt,
    Le,
    Gt,
    Ge,
    Eq,
    Ne,
};

inline NumericOp numeric_op_from_text(const std::string& op) {
    if (op == "+") return NumericOp::Add;
    if (op == "-") return NumericOp::Sub;
    if (op == "*") return NumericOp::Mul;
    if (op == "/") return NumericOp::Div;
    if (op == "MOD") return NumericOp::Mod;
    if (op == "<") return NumericOp::Lt;
    if (op == "<=") return NumericOp::Le;
    if (op == ">") return NumericOp::Gt;
    if (op == ">=") return NumericOp::Ge;
    if (op == "==" || op == "=") return NumericOp::Eq;
    if (op == "!=") return NumericOp::Ne;
    return NumericOp::Unknown;
}

inline bool numeric_op_is_comparison(NumericOp op) {
    switch (op) {
        case NumericOp::Lt:
        case NumericOp::Le:
        case NumericOp::Gt:
        case NumericOp::Ge:
        case NumericOp::Eq:
        case NumericOp::Ne:
            return true;
        default:
            return false;
    }
}

// Mirrors the lowercasing Runtime::call_host_function applies to build its host_functions_ key
// (see the anonymous-namespace function_key() in runtime.cpp), so a call site's key can be
// precomputed once at prepare time instead of re-lowered on every execution.
inline std::string lowered_call_key(const std::string& name) {
    std::string key = name;
    for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
}

struct BytecodeInstruction {
    BytecodeOp op = BytecodeOp::Unsupported;
    std::vector<std::string> operands;
    std::vector<BytecodeOperand> prepared_operands;
    std::vector<BytecodeCursor> prepared_targets;
    int prepared_source_line = 0;
    mutable CallSiteResolution call_site_resolution = CallSiteResolution::Unresolved;
    mutable const BytecodeFunction* call_site_function = nullptr;
    NumericOp prepared_numeric_op = NumericOp::Unknown;
    // Reused across executions of a CALL_VALUE/CALL_RUNTIME instruction to build the argument
    // list without a fresh heap allocation on every call. Safe under recursion: a call's args
    // are fully copied into the callee's frame before the callee's body (and thus any reentry
    // into this same instruction) ever runs, so nothing reads this buffer after that point.
    mutable std::vector<Value> call_args_scratch;
    // Lowercased callee name, precomputed once for a bare (unqualified, no '.') call target so
    // Runtime::call_host_function_prepared can skip re-lowering it on every execution. Empty
    // when the callee is namespaced (e.g. "Runtime.Print") or otherwise not eligible.
    std::string prepared_call_key;
    // Reused across executions of a STORE_INDEX instruction (assignments like arr[i] = v or
    // obj.field = v) to build the index list without a fresh heap allocation on every write.
    // assign_indexed recurses purely in C++ over this vector with no bytecode execution in
    // between, so unlike call_args_scratch there isn't even a reentrancy question here.
    mutable std::vector<Value> store_index_scratch;
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
    std::unordered_map<std::string, BytecodeCursor> targets;
    std::unordered_map<std::string, std::string> local_refs_by_base;
    std::vector<std::string> param_local_refs;
    std::vector<std::optional<Value>> param_defaults;
    std::size_t temp_count = 0;
};

struct BytecodeModule {
    std::string source_name;
    int version = 0;
    std::optional<std::uint64_t> instruction_limit;
    std::vector<std::string> constants;
    std::vector<Value> constant_values;
    std::vector<BytecodeSlot> constant_slots;
    std::vector<BytecodeFunction> functions;
    std::unordered_map<std::string, std::size_t> function_indices;
    std::vector<std::string> diagnostics;
    // Child class name -> EXTENDS parent name (empty if none); see AmirModule::class_parents.
    std::unordered_map<std::string, std::string> class_parents;
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
        case BytecodeOp::Throw:
            return "THROW";
        case BytecodeOp::Slice:
            return "SLICE";
        case BytecodeOp::Copy:
            return "COPY";
        case BytecodeOp::StoreSlice:
            return "STORE_SLICE";
        case BytecodeOp::Tuple:
            return "TUPLE";
        case BytecodeOp::Destructure:
            return "DESTRUCTURE";
        case BytecodeOp::AddressOf:
            return "ADDRESSOF";
        case BytecodeOp::StoreConst:
            return "STORE_CONST";
        case BytecodeOp::BinaryLocalLocal:
            return "BINARY_LOCAL_LOCAL";
        case BytecodeOp::BinaryLocalConst:
            return "BINARY_LOCAL_CONST";
        case BytecodeOp::BranchLocalLocal:
            return "BRANCH_LOCAL_LOCAL";
        case BytecodeOp::IndexLocalConst:
            return "INDEX_LOCAL_CONST";
    }
    return "UNSUPPORTED";
}

bool is_symbol_name(const std::string& text) {
    if (text.empty() || text[0] == '%' || text[0] == '"' || (text[0] >= '0' && text[0] <= '9')) {
        return false;
    }
    if (text == "true" || text == "false" || text == "nothing" || text == "null") {
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

std::string local_base_name(const std::string& name) {
    const auto space = name.find(' ');
    const auto equals = name.find('=');
    std::size_t end = std::min(space == std::string::npos ? name.size() : space,
                               equals == std::string::npos ? name.size() : equals);
    while (end > 0 && (name[end - 1] == ' ' || name[end - 1] == '\t')) --end;
    return name.substr(0, end);
}

// A parameter descriptor is rendered as "name[ AS Type][ = defaultExpr]" (see parameter_text).
// Returns the trimmed default-value text, or an empty string when the parameter has no default.
std::string param_default_text(const std::string& descriptor) {
    const auto equals = descriptor.find('=');
    if (equals == std::string::npos) return std::string();
    std::string text = descriptor.substr(equals + 1);
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) return std::string();
    const auto end = text.find_last_not_of(" \t");
    return text.substr(begin, end - begin + 1);
}

std::string local_ref(BytecodeFunction& function, const std::string& value) {
    const std::string base = local_base_name(value);
    for (std::size_t i = 0; i < function.locals.size(); ++i) {
        if (local_base_name(function.locals[i]) == base) {
            return "L" + std::to_string(i);
        }
    }
    function.locals.push_back(value);
    return "L" + std::to_string(function.locals.size() - 1);
}

BytecodeInstruction bytecode_instruction(BytecodeOp op, std::vector<std::string> operands) {
    BytecodeInstruction instruction;
    instruction.op = op;
    instruction.operands = std::move(operands);
    return instruction;
}

bool temp_ref_equals(const std::string& left, const std::string& right) {
    return !left.empty() && left[0] == '%' && left == right;
}

bool local_ref_text(const std::string& text) {
    return text.size() > 1 && text[0] == 'L' && text[1] >= '0' && text[1] <= '9';
}

bool const_ref_text(const std::string& text) {
    return text.size() > 1 && text[0] == 'K' && text[1] >= '0' && text[1] <= '9';
}

void optimize_bytecode_block(BytecodeBlock& block) {
    std::vector<BytecodeInstruction> optimized;
    optimized.reserve(block.instructions.size());

    for (std::size_t i = 0; i < block.instructions.size();) {
        if (i + 1 < block.instructions.size()) {
            const auto& first = block.instructions[i];
            const auto& second = block.instructions[i + 1];
            if (first.op == BytecodeOp::Const && first.operands.size() == 2 &&
                second.op == BytecodeOp::Store && second.operands.size() == 2 &&
                local_ref_text(second.operands[0]) && const_ref_text(first.operands[1]) &&
                temp_ref_equals(first.operands[0], second.operands[1])) {
                optimized.push_back(bytecode_instruction(BytecodeOp::StoreConst,
                    {second.operands[0], first.operands[1]}));
                i += 2;
                continue;
            }
        }

        if (i + 3 < block.instructions.size()) {
            const auto& load_left = block.instructions[i];
            const auto& load_right = block.instructions[i + 1];
            const auto& binary = block.instructions[i + 2];
            const auto& final = block.instructions[i + 3];
            if (load_left.op == BytecodeOp::Load && load_left.operands.size() == 2 &&
                load_right.op == BytecodeOp::Load && load_right.operands.size() == 2 &&
                binary.op == BytecodeOp::Binary && binary.operands.size() == 4 &&
                local_ref_text(load_left.operands[1]) && local_ref_text(load_right.operands[1]) &&
                temp_ref_equals(load_left.operands[0], binary.operands[2]) &&
                temp_ref_equals(load_right.operands[0], binary.operands[3])) {
                if (final.op == BytecodeOp::Store && final.operands.size() == 2 &&
                    local_ref_text(final.operands[0]) && temp_ref_equals(binary.operands[0], final.operands[1])) {
                    optimized.push_back(bytecode_instruction(BytecodeOp::BinaryLocalLocal,
                        {final.operands[0], binary.operands[1], load_left.operands[1], load_right.operands[1]}));
                    i += 4;
                    continue;
                }
                if (final.op == BytecodeOp::Branch && final.operands.size() == 3 &&
                    temp_ref_equals(binary.operands[0], final.operands[0])) {
                    optimized.push_back(bytecode_instruction(BytecodeOp::BranchLocalLocal,
                        {binary.operands[1], load_left.operands[1], load_right.operands[1], final.operands[1], final.operands[2]}));
                    i += 4;
                    continue;
                }
            }

            if (load_left.op == BytecodeOp::Load && load_left.operands.size() == 2 &&
                load_right.op == BytecodeOp::Const && load_right.operands.size() == 2 &&
                binary.op == BytecodeOp::Binary && binary.operands.size() == 4 &&
                final.op == BytecodeOp::Store && final.operands.size() == 2 &&
                local_ref_text(load_left.operands[1]) && const_ref_text(load_right.operands[1]) &&
                local_ref_text(final.operands[0]) &&
                temp_ref_equals(load_left.operands[0], binary.operands[2]) &&
                temp_ref_equals(load_right.operands[0], binary.operands[3]) &&
                temp_ref_equals(binary.operands[0], final.operands[1])) {
                optimized.push_back(bytecode_instruction(BytecodeOp::BinaryLocalConst,
                    {final.operands[0], binary.operands[1], load_left.operands[1], load_right.operands[1]}));
                i += 4;
                continue;
            }
        }

        // arr[k]/obj.field with a compile-time-constant index or property name lowers to
        // LOAD local; CONST key; INDEX target=local key=key -- fuse into one op. This is the
        // dominant shape for game-style entity/array access (obj.X, edge[0], ...).
        if (i + 2 < block.instructions.size()) {
            const auto& load_target = block.instructions[i];
            const auto& load_key = block.instructions[i + 1];
            const auto& index = block.instructions[i + 2];
            if (load_target.op == BytecodeOp::Load && load_target.operands.size() == 2 &&
                load_key.op == BytecodeOp::Const && load_key.operands.size() == 2 &&
                index.op == BytecodeOp::Index && index.operands.size() == 3 &&
                local_ref_text(load_target.operands[1]) && const_ref_text(load_key.operands[1]) &&
                temp_ref_equals(load_target.operands[0], index.operands[1]) &&
                temp_ref_equals(load_key.operands[0], index.operands[2])) {
                optimized.push_back(bytecode_instruction(BytecodeOp::IndexLocalConst,
                    {index.operands[0], load_target.operands[1], load_key.operands[1]}));
                i += 3;
                continue;
            }
        }

        optimized.push_back(std::move(block.instructions[i]));
        ++i;
    }

    block.instructions = std::move(optimized);
}

void optimize_bytecode_function(BytecodeFunction& function) {
    for (auto& block : function.blocks) {
        optimize_bytecode_block(block);
    }
}

BytecodeModule build_bytecode(const AmirModule& amir) {
    BytecodeModule module;
    module.source_name = amir.source_name;
    module.version = 0;
    module.instruction_limit = amir.instruction_limit;
    module.diagnostics = amir.diagnostics;
    module.class_parents = amir.class_parents;

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
                    case AmirInstruction::Kind::CpuPause:
                        module.diagnostics.push_back("hardware semantic CPU.Pause is unsupported by hosted bytecode backend");
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Unsupported, {"CPU.Pause"}));
                        break;
                    case AmirInstruction::Kind::Port:
                        module.diagnostics.push_back("PORT." + instruction.target + " is available only on a freestanding target with port-I/O support");
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Unsupported, {"PORT." + instruction.target}));
                        break;
                    case AmirInstruction::Kind::Memory:
                        module.diagnostics.push_back("MEMORY/ADDRESS operation is available only on a freestanding target with memory support");
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Unsupported, {"MEMORY." + instruction.target}));
                        break;
                    case AmirInstruction::Kind::Barrier:
                        module.diagnostics.push_back("CPU." + instruction.target + " is available only on a freestanding target with memory-ordering support");
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Unsupported, {"CPU." + instruction.target}));
                        break;
                    case AmirInstruction::Kind::Array: {
                        std::vector<std::string> operands{instruction.result};
                        operands.insert(operands.end(), instruction.operands.begin(), instruction.operands.end());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Array, std::move(operands)));
                        break;
                    }
                    case AmirInstruction::Kind::Tuple: {
                        std::vector<std::string> operands{instruction.result};
                        operands.insert(operands.end(), instruction.operands.begin(), instruction.operands.end());
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Tuple, std::move(operands)));
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
                    case AmirInstruction::Kind::Slice:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Slice,
                            {instruction.result, instruction.target, instruction.operands[0], instruction.operands[1], instruction.operands[2]}));
                        break;
                    case AmirInstruction::Kind::Copy:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Copy,
                            {instruction.result, instruction.operands.front()}));
                        break;
                    case AmirInstruction::Kind::AddressOf:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::AddressOf,
                            {instruction.result, instruction.target}));
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
                    case AmirInstruction::Kind::StoreSlice:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::StoreSlice,
                            {local_ref(function, instruction.target), instruction.operands[0], instruction.operands[1], instruction.operands[2]}));
                        break;
                    case AmirInstruction::Kind::Destructure: {
                        std::vector<std::string> operands{instruction.target};
                        for (const auto& name : instruction.operands) operands.push_back(local_ref(function, name));
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Destructure, std::move(operands)));
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
                    case AmirInstruction::Kind::Throw:
                        block.instructions.push_back(bytecode_instruction(BytecodeOp::Throw, instruction.operands));
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
        optimize_bytecode_function(function);
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
    if (module.instruction_limit.has_value()) {
        out << "METADATA INSTRUCTION_LIMIT " << *module.instruction_limit << "\n\n";
    }

    out << "OPCODES\n";
    for (int id = static_cast<int>(BytecodeOp::Label); id <= static_cast<int>(BytecodeOp::Last); ++id) {
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

    if (!module.class_parents.empty()) {
        out << "CLASS_PARENTS " << module.class_parents.size() << "\n";
        for (const auto& [child, parent] : module.class_parents) {
            out << "    " << child << " " << (parent.empty() ? "-" : parent) << "\n";
        }
        out << "END CLASS_PARENTS\n\n";
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
    for (int id = static_cast<int>(BytecodeOp::Label); id <= static_cast<int>(BytecodeOp::Last); ++id) {
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
    bool in_class_parents = false;

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line == "ARCOFISSION BYTECODE" || line == "FORMAT .arcof-text" || line == "OPCODES" ||
            line == "END OPCODES" || line == "END CONSTANTS" || line == "END DIAGNOSTICS") {
            continue;
        }
        if (line.rfind("CLASS_PARENTS ", 0) == 0) {
            in_class_parents = true;
            continue;
        }
        if (line == "END CLASS_PARENTS") {
            in_class_parents = false;
            continue;
        }
        if (in_class_parents) {
            const auto words = split_words(line);
            if (words.size() == 2) {
                module.class_parents.emplace(words[0], words[1] == "-" ? "" : words[1]);
            }
            continue;
        }

        if (line.rfind("VERSION ", 0) == 0) {
            module.version = std::stoi(line.substr(8));
        } else if (line.rfind("SOURCE ", 0) == 0) {
            module.source_name = line.substr(7);
        } else if (line.rfind("METADATA INSTRUCTION_LIMIT ", 0) == 0) {
            module.instruction_limit = std::stoull(line.substr(27));
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

void append_u8(std::string& out, std::uint8_t value) {
    out.push_back(static_cast<char>(value));
}

void append_u32(std::string& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}

void append_u64(std::string& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}

void append_string(std::string& out, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("bytecode string is too large for binary capsule format");
    }
    append_u32(out, static_cast<std::uint32_t>(value.size()));
    out.append(value);
}

std::string render_binary_bytecode(const BytecodeModule& module) {
    std::string out;
    out.reserve(render_bytecode(module).size());
    out.append("ARCOFBC1", 8);
    append_u32(out, static_cast<std::uint32_t>(module.version));
    append_string(out, module.source_name);
    append_u8(out, module.instruction_limit.has_value() ? 1 : 0);
    append_u64(out, module.instruction_limit.value_or(0));

    append_u32(out, static_cast<std::uint32_t>(module.diagnostics.size()));
    for (const auto& diagnostic : module.diagnostics) append_string(out, diagnostic);

    append_u32(out, static_cast<std::uint32_t>(module.constants.size()));
    for (const auto& constant : module.constants) append_string(out, constant);

    append_u32(out, static_cast<std::uint32_t>(module.functions.size()));
    for (const auto& function : module.functions) {
        append_string(out, function.name);
        append_string(out, function.return_type);
        append_u32(out, static_cast<std::uint32_t>(function.params.size()));
        for (const auto& param : function.params) append_string(out, param);
        append_u32(out, static_cast<std::uint32_t>(function.locals.size()));
        for (const auto& local : function.locals) append_string(out, local);
        append_u32(out, static_cast<std::uint32_t>(function.blocks.size()));
        for (const auto& block : function.blocks) {
            append_string(out, block.name);
            append_u32(out, static_cast<std::uint32_t>(block.instructions.size()));
            for (const auto& instruction : block.instructions) {
                append_u8(out, static_cast<std::uint8_t>(instruction.op));
                append_u32(out, static_cast<std::uint32_t>(instruction.operands.size()));
                for (const auto& operand : instruction.operands) append_string(out, operand);
            }
        }
    }

    append_u32(out, static_cast<std::uint32_t>(module.class_parents.size()));
    for (const auto& [child, parent] : module.class_parents) {
        append_string(out, child);
        append_string(out, parent);
    }
    return out;
}

struct BinaryReader {
    const std::string& input;
    std::size_t offset = 0;

    void require(std::size_t bytes) const {
        if (bytes > input.size() || offset > input.size() - bytes) {
            throw std::runtime_error("truncated binary bytecode capsule");
        }
    }

    std::uint8_t u8() {
        require(1);
        return static_cast<std::uint8_t>(input[offset++]);
    }

    std::uint32_t u32() {
        require(4);
        std::uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset++])) << shift;
        }
        return value;
    }

    std::uint64_t u64() {
        require(8);
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(input[offset++])) << shift;
        }
        return value;
    }

    std::string string() {
        const std::uint32_t size = u32();
        require(size);
        std::string value = input.substr(offset, size);
        offset += size;
        return value;
    }
};

BytecodeModule parse_binary_bytecode(const std::string& binary) {
    BinaryReader reader{binary};
    reader.require(8);
    if (binary.compare(0, 8, "ARCOFBC1") != 0) {
        throw std::runtime_error("invalid binary bytecode capsule");
    }
    reader.offset = 8;

    BytecodeModule module;
    module.version = static_cast<int>(reader.u32());
    module.source_name = reader.string();
    const bool has_instruction_limit = reader.u8() != 0;
    const std::uint64_t instruction_limit = reader.u64();
    if (has_instruction_limit) module.instruction_limit = instruction_limit;

    const std::uint32_t diagnostic_count = reader.u32();
    module.diagnostics.reserve(diagnostic_count);
    for (std::uint32_t i = 0; i < diagnostic_count; ++i) module.diagnostics.push_back(reader.string());

    const std::uint32_t constant_count = reader.u32();
    module.constants.reserve(constant_count);
    for (std::uint32_t i = 0; i < constant_count; ++i) module.constants.push_back(reader.string());

    const std::uint32_t function_count = reader.u32();
    module.functions.reserve(function_count);
    for (std::uint32_t i = 0; i < function_count; ++i) {
        BytecodeFunction function;
        function.name = reader.string();
        function.return_type = reader.string();
        const std::uint32_t param_count = reader.u32();
        function.params.reserve(param_count);
        for (std::uint32_t p = 0; p < param_count; ++p) function.params.push_back(reader.string());
        const std::uint32_t local_count = reader.u32();
        function.locals.reserve(local_count);
        for (std::uint32_t l = 0; l < local_count; ++l) function.locals.push_back(reader.string());
        const std::uint32_t block_count = reader.u32();
        function.blocks.reserve(block_count);
        for (std::uint32_t b = 0; b < block_count; ++b) {
            BytecodeBlock block;
            block.name = reader.string();
            const std::uint32_t instruction_count = reader.u32();
            block.instructions.reserve(instruction_count);
            for (std::uint32_t n = 0; n < instruction_count; ++n) {
                BytecodeInstruction instruction;
                instruction.op = static_cast<BytecodeOp>(reader.u8());
                const std::uint32_t operand_count = reader.u32();
                instruction.operands.reserve(operand_count);
                for (std::uint32_t o = 0; o < operand_count; ++o) instruction.operands.push_back(reader.string());
                block.instructions.push_back(std::move(instruction));
            }
            function.blocks.push_back(std::move(block));
        }
        module.functions.push_back(std::move(function));
    }

    const std::uint32_t class_parent_count = reader.u32();
    module.class_parents.reserve(class_parent_count);
    for (std::uint32_t i = 0; i < class_parent_count; ++i) {
        std::string child = reader.string();
        std::string parent = reader.string();
        module.class_parents.emplace(std::move(child), std::move(parent));
    }

    if (reader.offset != binary.size()) {
        throw std::runtime_error("binary bytecode capsule has trailing data");
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
    struct InternalCallFixup {
        std::size_t disp_field_offset;
        std::string target;
    };
    std::vector<InternalCallFixup> internal_calls;
    std::string entry_symbol;
};

// Generates x86-64 machine code for a single named function within `module` (Packet WP-008/WP-006,
// arcology-os/docs/systems/x86-64-codegen.md). Deliberately narrow: supports the A-MIR instruction
// kinds required by the systems fixtures, including explicit multi-block branches, with a uniform
// spill-everything strategy (Packet non-goal: "register allocator sophistication beyond correctness"
// -- every named value gets its own stack slot, always reloaded before use, never kept live in a
// register across instructions).
// Any other instruction kind, or any construct this milestone's UEFI bindings/calling convention
// do not cover, produces a clear error rather than an incorrect or silently wrong encoding.
X86_64CodegenResult generate_x86_64_function(const AmirModule& module, const std::string& function_name) {
    X86_64CodegenResult result;
    result.entry_symbol = function_name;
    using Reg = systems::x86_64::Reg;

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
    for (const auto& block : target->blocks) {
        for (const auto& instruction : block.instructions) {
            add_slot(instruction.result);
            if (instruction.kind == AmirInstruction::Kind::Store || instruction.kind == AmirInstruction::Kind::Load) {
                add_slot(instruction.target);
            }
        }
    }

    const int shadow = systems::kShadowSpaceBytes;
    int max_outgoing_stack_args = 0;
    for (const auto& block : target->blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.kind != AmirInstruction::Kind::CallExternal && instruction.kind != AmirInstruction::Kind::CallValue) continue;
            // Reserve conservatively for the implicit UEFI `This` argument. This may reserve one
            // extra slot, but keeps the frame layout deterministic without backend-specific type
            // lookup during frame construction.
            const int total_args = static_cast<int>(instruction.operands.size()) + 1;
            max_outgoing_stack_args = std::max(max_outgoing_stack_args, std::max(0, total_args - 4));
        }
    }
    const int outgoing_base = shadow;
    const int slot_base = outgoing_base + 8 * max_outgoing_stack_args;
    const int scratch_base = slot_base + 8 * static_cast<int>(slot_names.size());
    int frame_size = scratch_base + 32;
    // RSP is kEntryRspMod16 (8) mod 16 at function entry; after `sub rsp, frame_size`, RSP must
    // be 0 mod 16 immediately before any CALL this function makes, which requires
    // frame_size % 16 == kEntryRspMod16.
    while (frame_size % 16 != systems::kEntryRspMod16) {
        ++frame_size;
    }
    if (frame_size > 4095) {
        result.ok = false;
        result.error = "function \"" + function_name + "\" needs a stack frame larger than the "
            "systems backend's supported stack layout";
        return result;
    }
    for (std::size_t i = 0; i < slot_names.size(); ++i) {
        slot_offsets[slot_names[i]] = slot_base + 8 * static_cast<int>(i);
    }
    const auto slot_of = [&](const std::string& name) -> int {
        const auto found = slot_offsets.find(name);
        return found == slot_offsets.end() ? -1 : found->second;
    };

    const auto width_bits = [](const std::string& type) -> int {
        if (type == "U8" || type == "I8" || type == "BOOL") return 8;
        if (type == "IOPORT") return 16;
        if (type == "U16" || type == "I16") return 16;
        if (type == "U32" || type == "I32") return 32;
        return 64;
    };
    const auto signed_type = [](const std::string& type) {
        return type == "I8" || type == "I16" || type == "I32" || type == "I64";
    };
    const auto normalize = [&](Reg reg, const std::string& type) {
        const int bits = width_bits(type);
        if (bits < 64) {
            // REX.W AND r64, imm32 sign-extends the immediate. For 0xFFFFFFFF that means
            // "AND with all ones", so it does not truncate U32 at all. A 32-bit MOV writes
            // the low dword and architecturally clears the upper 32 bits.
            if (bits == 32) result.text.mov_reg32_reg32(reg, reg);
            else result.text.and_reg_imm32(reg, (1U << bits) - 1U);
            if (signed_type(type)) {
                result.text.shl_reg_imm8(reg, static_cast<std::uint8_t>(64 - bits));
                result.text.sar_reg_imm8(reg, static_cast<std::uint8_t>(64 - bits));
            }
        }
    };
    const auto store_result = [&](const std::string& name, const std::string& type, Reg reg = Reg::RAX) -> bool {
        const int offset = slot_of(name);
        if (offset < 0) {
            result.ok = false;
            result.error = "value \"" + name + "\" has no assigned stack slot";
            return false;
        }
        normalize(reg, type);
        if (offset <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(offset), reg);
        else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(offset), reg);
        return true;
    };
    const auto load_value = [&](const std::string& name, const std::string& type, Reg reg) -> bool {
        const int offset = slot_of(name);
        if (offset < 0) {
            result.ok = false;
            result.error = "value \"" + name + "\" has no assigned stack slot";
            return false;
        }
        if (offset <= 127) result.text.mov_load_disp8(reg, Reg::RSP, static_cast<std::uint8_t>(offset));
        else result.text.mov_load_disp32(reg, Reg::RSP, static_cast<std::uint32_t>(offset));
        normalize(reg, type);
        return true;
    };
    const auto parse_integer = [](const std::string& text, std::uint64_t& value) -> bool {
        try {
            std::size_t consumed = 0;
            int base = 10;
            std::string digits = text;
            if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
                base = 16; digits = digits.substr(2);
            } else if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'b' || digits[1] == 'B')) {
                base = 2; digits = digits.substr(2);
            }
            value = std::stoull(digits, &consumed, base);
            return consumed == digits.size();
        } catch (...) {
            return false;
        }
    };

    static const std::unordered_map<std::string, systems::x86_64::Reg> kRegisterByName = {
        {"RCX", systems::x86_64::Reg::RCX}, {"RDX", systems::x86_64::Reg::RDX},
        {"R8", systems::x86_64::Reg::R8}, {"R9", systems::x86_64::Reg::R9},
    };

    struct BranchFixup {
        std::size_t displacement_offset = 0;
        std::string target;
    };
    std::vector<BranchFixup> branch_fixups;
    std::unordered_map<std::string, std::size_t> block_offsets;

    std::unordered_map<std::string, std::size_t> block_indices;
    for (std::size_t i = 0; i < target->blocks.size(); ++i) block_indices[target->blocks[i].name] = i;
    std::unordered_set<std::string> reachable_blocks;
    std::vector<std::string> pending_blocks;
    if (!target->blocks.empty()) pending_blocks.push_back(target->blocks.front().name);
    while (!pending_blocks.empty()) {
        const std::string name = pending_blocks.back();
        pending_blocks.pop_back();
        if (!reachable_blocks.insert(name).second) continue;
        const auto found = block_indices.find(name);
        if (found == block_indices.end()) continue;
        const auto& instructions = target->blocks[found->second].instructions;
        if (instructions.empty()) continue;
        const auto& terminator = instructions.back();
        if (terminator.kind == AmirInstruction::Kind::Jump && !terminator.target.empty()) {
            pending_blocks.push_back(terminator.target);
        } else if (terminator.kind == AmirInstruction::Kind::Branch && terminator.operands.size() >= 3) {
            pending_blocks.push_back(terminator.operands[1]);
            pending_blocks.push_back(terminator.operands[2]);
        }
    }

    if (frame_size <= 255) result.text.sub_rsp_imm8(static_cast<std::uint8_t>(frame_size));
    else result.text.sub_rsp_imm32(static_cast<std::uint32_t>(frame_size));

    // Spill incoming arguments. Register arguments arrive in the four Microsoft x64 integer
    // registers; later arguments are homed by the caller at entry-RSP+40, +48, ... (32 bytes of
    // shadow space plus the return address). After our prologue, entry-RSP is frame_size bytes
    // above the current RSP, so stack parameters are loaded from frame_size+stack_offset.
    {
        const auto locations = systems::assign_argument_locations(static_cast<int>(target->params.size()));
        for (std::size_t i = 0; i < target->params.size(); ++i) {
            const std::string name = bare_parameter_name(target->params[i]);
            const int parameter_slot = slot_of(name);
            if (locations[i].in_register) {
                if (parameter_slot <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(parameter_slot),
                                             kRegisterByName.at(locations[i].register_name));
                else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(parameter_slot),
                                             kRegisterByName.at(locations[i].register_name));
            } else {
                const std::uint32_t incoming_offset = static_cast<std::uint32_t>(frame_size + locations[i].stack_offset_bytes);
                if (incoming_offset <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(incoming_offset));
                else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, incoming_offset);
                if (parameter_slot <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(parameter_slot), Reg::RAX);
                else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(parameter_slot), Reg::RAX);
            }
        }
    }

    for (const auto& current_block : target->blocks) {
        if (reachable_blocks.count(current_block.name) == 0) continue;
        block_offsets[current_block.name] = result.text.size();
        for (const auto& instruction : current_block.instructions) {
        switch (instruction.kind) {
            case AmirInstruction::Kind::Source:
            case AmirInstruction::Kind::Label:
                break;

            case AmirInstruction::Kind::Jump: {
                const std::size_t displacement = result.text.jmp_rel32_placeholder();
                branch_fixups.push_back({displacement, instruction.target});
                break;
            }

            case AmirInstruction::Kind::Branch: {
                if (instruction.operands.size() < 3) {
                    result.ok = false;
                    result.error = "malformed BRANCH instruction";
                    return result;
                }
                const std::string condition_type = instruction.result_type.empty() ? "BOOL" : instruction.result_type;
                if (condition_type != "BOOL") {
                    result.ok = false;
                    result.error = "BRANCH condition must be BOOL; received " + condition_type;
                    return result;
                }
                if (!load_value(instruction.operands[0], condition_type, Reg::RAX)) return result;
                result.text.cmp_reg_imm32(Reg::RAX, 0);
                const std::size_t true_displacement = result.text.jcc_rel32_placeholder(0x5); // JNE
                branch_fixups.push_back({true_displacement, instruction.operands[1]});
                const std::size_t false_displacement = result.text.jmp_rel32_placeholder();
                branch_fixups.push_back({false_displacement, instruction.operands[2]});
                break;
            }

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
                    if (!store_result(instruction.result, instruction.result_type.empty() ? "U64" : instruction.result_type)) return result;
                } else {
                    std::uint64_t value = 0;
                    if (!parse_integer(text_operand, value)) {
                        result.ok = false;
                        result.error = "numeric constant \"" + text_operand + "\" is not an exact "
                            "integer literal, which is all this milestone's code generator supports";
                        return result;
                    }
                    result.text.mov_reg_imm64(Reg::RAX, value);
                    if (!store_result(instruction.result, instruction.result_type.empty() ? "U64" : instruction.result_type)) return result;
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
                if (source_slot <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(source_slot));
                else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(source_slot));
                normalize(Reg::RAX, instruction.result_type.empty() ? "U64" : instruction.result_type);
                if (!store_result(instruction.result, instruction.result_type.empty() ? "U64" : instruction.result_type)) return result;
                break;
            }

            case AmirInstruction::Kind::Unary: {
                const std::string type = instruction.result_type.empty() ? "U64" : instruction.result_type;
                const std::string operand_type = instruction.operand_types.empty() ? type : instruction.operand_types.front();
                if (!load_value(instruction.operands.front(), operand_type, Reg::RAX)) return result;
                if (instruction.target == "-") result.text.neg_reg(Reg::RAX);
                else if (instruction.target == "~") result.text.not_reg(Reg::RAX);
                else if (instruction.target == "!") {
                    result.text.cmp_reg_imm32(Reg::RAX, 0);
                    result.text.setcc_al(0x4);
                    result.text.movzx_eax_al();
                } else {
                    result.ok = false;
                    result.error = "unsupported systems unary operation " + instruction.target;
                    return result;
                }
                if (!store_result(instruction.result, type)) return result;
                break;
            }

            case AmirInstruction::Kind::Binary: {
                if (instruction.operands.size() < 2) {
                    result.ok = false; result.error = "malformed integer operation"; return result;
                }
                const std::string type = instruction.result_type.empty() ? "U64" : instruction.result_type;
                const std::string left_type = instruction.operand_types.size() > 0 ? instruction.operand_types[0] : type;
                const std::string right_type = instruction.operand_types.size() > 1 ? instruction.operand_types[1] : type;
                if (!load_value(instruction.operands[0], left_type, Reg::RAX) ||
                    !load_value(instruction.operands[1], right_type, Reg::RCX)) return result;
                const std::string op = instruction.target;
                if (op == "+") result.text.add_reg_reg(Reg::RAX, Reg::RCX);
                else if (op == "-") result.text.sub_reg_reg(Reg::RAX, Reg::RCX);
                else if (op == "*") result.text.imul_reg_reg(Reg::RAX, Reg::RCX);
                else if (op == "&") result.text.and_reg_reg(Reg::RAX, Reg::RCX);
                else if (op == "|") result.text.or_reg_reg(Reg::RAX, Reg::RCX);
                else if (op == "^") result.text.xor_reg_reg(Reg::RAX, Reg::RCX);
                else if (op == "\\" || op == "MOD") {
                    if (signed_type(left_type)) result.text.cqo(); else result.text.xor_rdx_rdx();
                    if (signed_type(left_type)) result.text.idiv_reg(Reg::RCX); else result.text.div_reg(Reg::RCX);
                    if (op == "MOD") result.text.mov_reg_reg(Reg::RAX, Reg::RDX);
                } else if (op == "<<" || op == ">>") {
                    result.text.mov_reg_reg(Reg::R8, Reg::RAX);
                    result.text.mov_reg_reg(Reg::RCX, Reg::RCX);
                    if (op == "<<") result.text.shl_reg_cl(Reg::RAX);
                    else result.text.shr_reg_cl(Reg::RAX);
                    result.text.cmp_reg_imm32(Reg::RCX, static_cast<std::uint32_t>(width_bits(left_type)));
                    result.text.setcc_al(0x2);
                    result.text.movzx_eax_al();
                    result.text.neg_reg(Reg::RAX);
                    result.text.and_reg_reg(Reg::R8, Reg::RAX);
                    result.text.mov_reg_reg(Reg::RAX, Reg::R8);
                } else if (op == "SAR") {
                    result.text.mov_reg_reg(Reg::R8, Reg::RAX);
                    result.text.mov_reg_reg(Reg::R9, Reg::RAX);
                    result.text.sar_reg_imm8(Reg::R9, 63);
                    result.text.sar_reg_cl(Reg::R8);
                    result.text.cmp_reg_imm32(Reg::RCX, static_cast<std::uint32_t>(width_bits(left_type)));
                    result.text.setcc_al(0x2);
                    result.text.movzx_eax_al();
                    result.text.neg_reg(Reg::RAX);
                    result.text.and_reg_reg(Reg::R8, Reg::RAX);
                    result.text.not_reg(Reg::RAX);
                    result.text.and_reg_reg(Reg::R9, Reg::RAX);
                    result.text.or_reg_reg(Reg::R8, Reg::R9);
                    result.text.mov_reg_reg(Reg::RAX, Reg::R8);
                } else if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
                    result.text.cmp_reg_reg(Reg::RAX, Reg::RCX);
                    std::uint8_t condition = 0x4;
                    if (op == "!=") condition = 0x5;
                    else if (op == "<") condition = signed_type(left_type) ? 0xC : 0x2;
                    else if (op == "<=") condition = signed_type(left_type) ? 0xE : 0x6;
                    else if (op == ">") condition = signed_type(left_type) ? 0xF : 0x7;
                    else if (op == ">=") condition = signed_type(left_type) ? 0xD : 0x3;
                    result.text.setcc_al(condition);
                    result.text.movzx_eax_al();
                } else {
                    result.ok = false;
                    result.error = "unsupported systems integer operation " + op;
                    return result;
                }
                if (!store_result(instruction.result, type)) return result;
                break;
            }

            case AmirInstruction::Kind::Store: {
                if (instruction.operands.empty()) {
                    result.ok = false; result.error = "malformed STORE"; return result;
                }
                const int target_slot = slot_of(instruction.target);
                const int value_slot = slot_of(instruction.operands.front());
                if (target_slot < 0 || value_slot < 0) {
                    result.ok = false;
                    result.error = "STORE references an unknown value";
                    return result;
                }
                if (value_slot <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(value_slot));
                else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(value_slot));
                if (target_slot <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(target_slot), Reg::RAX);
                else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(target_slot), Reg::RAX);
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

            case AmirInstruction::Kind::CpuPause:
                result.text.pause();
                break;

            case AmirInstruction::Kind::Port: {
                if (instruction.operands.empty()) {
                    result.ok = false;
                    result.error = "malformed PORT." + instruction.target;
                    return result;
                }
                const std::string port_type = instruction.operand_types.empty() ? "IOPORT" : instruction.operand_types.front();
                if (!load_value(instruction.operands[0], port_type, Reg::RAX)) return result;
                result.text.mov_reg_reg(Reg::RDX, Reg::RAX);
                if (instruction.target == "ADDRESS") {
                    if (!store_result(instruction.result, "IOPORT")) return result;
                } else if (instruction.target == "OFFSET") {
                    if (instruction.operands.size() != 2 || !load_value(instruction.operands[1], "I16", Reg::RCX)) return result;
                    result.text.add_reg_reg(Reg::RDX, Reg::RCX);
                    result.text.and_reg_imm32(Reg::RDX, 0xFFFF);
                    result.text.mov_reg_reg(Reg::RAX, Reg::RDX);
                    if (!store_result(instruction.result, "IOPORT")) return result;
                } else {
                    const bool read = instruction.target == "READ8" || instruction.target == "READ16" || instruction.target == "READ32";
                    const bool write = instruction.target == "WRITE8" || instruction.target == "WRITE16" || instruction.target == "WRITE32";
                    if (!read && !write) {
                        result.ok = false;
                        result.error = "unsupported systems port operation " + instruction.target;
                        return result;
                    }
                    const std::string width = instruction.target.substr(read ? 4 : 5);
                    const std::string value_type = "U" + width;
                    if (write) {
                        if (instruction.operands.size() != 2 || !load_value(instruction.operands[1], value_type, Reg::RCX)) return result;
                        result.text.mov_reg_reg(Reg::RAX, Reg::RCX);
                        if (width == "8") result.text.out_dx_al();
                        else if (width == "16") result.text.out_dx_ax();
                        else result.text.out_dx_eax();
                    } else {
                        if (width == "8") result.text.in_al_dx();
                        else if (width == "16") result.text.in_ax_dx();
                        else result.text.in_eax_dx();
                        if (!store_result(instruction.result, value_type)) return result;
                    }
                }
                break;
            }

            case AmirInstruction::Kind::Barrier:
                if (instruction.target == "READBARRIER") result.text.lfence();
                else if (instruction.target == "WRITEBARRIER") result.text.sfence();
                else if (instruction.target == "MEMORYBARRIER") result.text.mfence();
                else if (instruction.target == "DISABLEINTERRUPTS") result.text.cli();
                else if (instruction.target == "ENABLEINTERRUPTS") result.text.sti();
                else if (instruction.target == "BREAKPOINT") result.text.int3();
                else { result.ok = false; result.error = "unsupported memory barrier " + instruction.target; return result; }
                break;

            case AmirInstruction::Kind::Memory: {
                const std::string op = instruction.target;
                if (op == "READRSP") {
                    result.text.mov_rax_rsp();
                    if (!store_result(instruction.result, "U64")) return result;
                    break;
                }
                if (op == "READCR3") {
                    result.text.mov_rax_cr3();
                    if (!store_result(instruction.result, "U64")) return result;
                    break;
                }
                if (op == "READCR2") {
                    result.text.mov_rax_cr2();
                    if (!store_result(instruction.result, "U64")) return result;
                    break;
                }
                if (op == "WRITECR3" || op == "INVLPG") {
                    if (instruction.operands.size() != 1 || !load_value(instruction.operands[0], "U64", Reg::RAX)) return result;
                    if (op == "WRITECR3") result.text.mov_cr3_rax();
                    else result.text.invlpg_rax();
                    break;
                }
                if (op == "LGDT" || op == "LIDT" || op == "LTR") {
                    if (instruction.operands.size() != 1 || !load_value(instruction.operands[0], "U64", Reg::RAX)) { result.ok = false; result.error = "malformed descriptor-table operation"; return result; }
                    if (op == "LGDT") result.text.lgdt_rax();
                    else if (op == "LIDT") result.text.lidt_rax();
                    else result.text.ltr_rax();
                    break;
                }
                if (instruction.operands.empty()) { result.ok = false; result.error = "malformed memory operation"; return result; }
                if (op == "GRAPHICSBIND") {
                    if (instruction.operands.size() != 1 || !load_value(instruction.operands[0], "SURFACE", Reg::RAX)) return result;
                    const int surface_offset = scratch_base + 24;
                    if (surface_offset <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(surface_offset), Reg::RAX);
                    else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(surface_offset), Reg::RAX);
                    break;
                }
                if (op == "LOCAL") {
                    if (instruction.operands.size() != 1) { result.ok = false; result.error = "ADDRESS.LOCAL requires one local variable"; return result; }
                    const int local_offset = slot_of(instruction.operands[0]);
                    if (local_offset < 0) { result.ok = false; result.error = "ADDRESS.LOCAL references an unknown local"; return result; }
                    if (local_offset <= 127) result.text.lea_rsp_disp8(Reg::RAX, static_cast<std::uint8_t>(local_offset));
                    else result.text.lea_rsp_disp32(Reg::RAX, static_cast<std::uint32_t>(local_offset));
                    if (!store_result(instruction.result, "PTR")) return result;
                    break;
                }
                if (op == "GOPDISCOVER") {
                    if (!load_value(instruction.operands[0], "UEFI.SystemTable", Reg::RAX)) return result;
                    result.text.mov_load_disp8(Reg::R11, Reg::RAX, 0x60); // SystemTable.BootServices
                    const int guid_offset = scratch_base;
                    const int output_offset = scratch_base + 16;
                    result.text.mov_reg_imm64(Reg::RDX, 0x4A3823DC9042A9DEULL);
                    if (guid_offset <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(guid_offset), Reg::RDX);
                    else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(guid_offset), Reg::RDX);
                    result.text.mov_reg_imm64(Reg::RDX, 0x6A5180D0DE7AFB96ULL);
                    if (guid_offset + 8 <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(guid_offset + 8), Reg::RDX);
                    else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(guid_offset + 8), Reg::RDX);
                    if (guid_offset <= 127) result.text.lea_rsp_disp8(Reg::RCX, static_cast<std::uint8_t>(guid_offset));
                    else result.text.lea_rsp_disp32(Reg::RCX, static_cast<std::uint32_t>(guid_offset));
                    result.text.xor_rdx_rdx();
                    if (output_offset <= 127) result.text.lea_rsp_disp8(Reg::R8, static_cast<std::uint8_t>(output_offset));
                    else result.text.lea_rsp_disp32(Reg::R8, static_cast<std::uint32_t>(output_offset));
                    result.text.mov_reg_imm64(Reg::RAX, 0);
                    if (output_offset <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(output_offset), Reg::RAX);
                    else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(output_offset), Reg::RAX);
                    result.text.call_indirect_disp32(Reg::R11, 0x140);
                    // LocateProtocol returns EFI_STATUS in RAX. Do not consume the output
                    // pointer unless the status is EFI_SUCCESS (zero). A failed discovery is
                    // represented as a null typed GOP value for source-level guards.
                    result.text.cmp_rax_imm8(0);
                    const std::size_t failed_displacement = result.text.jcc_rel32_placeholder(0x5); // JNE
                    if (output_offset <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(output_offset));
                    else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(output_offset));
                    const std::size_t success_jump = result.text.jmp_rel32_placeholder();
                    const std::size_t failure_target = result.text.size();
                    result.text.mov_reg_imm64(Reg::RAX, 0);
                    const std::size_t done_target = result.text.size();
                    result.text.patch_i32(failed_displacement, static_cast<std::int32_t>(failure_target) -
                        static_cast<std::int32_t>(failed_displacement + 4));
                    result.text.patch_i32(success_jump, static_cast<std::int32_t>(done_target) -
                        static_cast<std::int32_t>(success_jump + 4));
                    if (!store_result(instruction.result, "UEFI.GraphicsOutputProtocol")) return result;
                    break;
                }
                if (op.rfind("GOP", 0) == 0 && op != "GOPDISCOVER") {
                    if (!load_value(instruction.operands[0], "UEFI.GraphicsOutputProtocol", Reg::RAX)) return result;
                    result.text.mov_load_disp8(Reg::RAX, Reg::RAX, 0x18); // GOP.Mode
                    if (op == "GOPMODE") {
                        if (!store_result(instruction.result, "UEFI.GraphicsOutputMode")) return result;
                        break;
                    }
                    const bool info_field = op == "GOPWIDTH" || op == "GOPHEIGHT" || op == "GOPPIXELSPERSCANLINE" || op == "GOPPIXELFORMAT";
                    if (info_field) result.text.mov_load_disp8(Reg::RAX, Reg::RAX, 0x08); // Mode.Info
                    int offset = 0x18;
                    if (op == "GOPFRAMEBUFFERSIZE") offset = 0x20;
                    else if (op == "GOPWIDTH") offset = 0x04;
                    else if (op == "GOPHEIGHT") offset = 0x08;
                    else if (op == "GOPPIXELSPERSCANLINE") offset = 0x20;
                    else if (op == "GOPPIXELFORMAT") offset = 0x0C;
                    if (info_field) {
                        if (offset <= 127) result.text.mov_load32_disp8(Reg::RAX, Reg::RAX, static_cast<std::uint8_t>(offset));
                        else result.text.mov_load32_disp32(Reg::RAX, Reg::RAX, static_cast<std::uint32_t>(offset));
                    } else {
                        if (offset <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RAX, static_cast<std::uint8_t>(offset));
                        else result.text.mov_load_disp32(Reg::RAX, Reg::RAX, static_cast<std::uint32_t>(offset));
                    }
                    if (!store_result(instruction.result, instruction.result_type)) return result;
                    break;
                }
                const bool physical_write = op == "PHYSICALWRITE64";
                if (op == "OFFSET") {
                    if (instruction.operands.size() != 2) { result.ok = false; result.error = "ADDRESS.OFFSET requires two operands"; return result; }
                    if (!load_value(instruction.operands[0], instruction.operand_types.empty() ? "VIRTUALPTR" : instruction.operand_types.front(), Reg::RAX)) return result;
                    if (!load_value(instruction.operands[1], instruction.operand_types.size() > 1 ? instruction.operand_types[1] : "I64", Reg::RCX)) return result;
                    result.text.add_reg_reg(Reg::RAX, Reg::RCX);
                    if (!store_result(instruction.result, instruction.result_type.empty() ? "VIRTUALPTR" : instruction.result_type)) return result;
                    break;
                }
                if (op == "ALIGNDOWN" || op == "ALIGNUP" || op == "ISALIGNED") {
                    if (instruction.operands.size() != 2) { result.ok = false; result.error = "address alignment operation requires two operands"; return result; }
                    if (!load_value(instruction.operands[0], instruction.operand_types.empty() ? "VIRTUALPTR" : instruction.operand_types.front(), Reg::RAX) ||
                        !load_value(instruction.operands[1], instruction.operand_types.size() > 1 ? instruction.operand_types[1] : "U64", Reg::RCX)) return result;
                    if (op == "ISALIGNED") {
                        result.text.mov_reg_reg(Reg::RDX, Reg::RCX);
                        result.text.mov_reg_imm64(Reg::RCX, 1);
                        result.text.sub_reg_reg(Reg::RDX, Reg::RCX);
                        result.text.and_reg_reg(Reg::RAX, Reg::RDX);
                        result.text.cmp_reg_imm32(Reg::RAX, 0);
                        result.text.setcc_al(0x04);
                        result.text.movzx_eax_al();
                        if (!store_result(instruction.result, "BOOL")) return result;
                    } else {
                        result.text.mov_reg_imm64(Reg::RDX, 1);
                        if (op == "ALIGNUP") { result.text.sub_reg_reg(Reg::RCX, Reg::RDX); result.text.add_reg_reg(Reg::RAX, Reg::RCX); }
                        result.text.not_reg(Reg::RCX);
                        result.text.and_reg_reg(Reg::RAX, Reg::RCX);
                        if (!store_result(instruction.result, instruction.result_type.empty() ? "VIRTUALPTR" : instruction.result_type)) return result;
                    }
                    break;
                }
                if (op == "PHYSICAL" || op == "VIRTUAL" || op == "MMIO" || op == "VALUE" || op == "MAP" || op == "MAPDEVICE") {
                    if (!load_value(instruction.operands[0], instruction.operand_types.empty() ? "U64" : instruction.operand_types.front(), Reg::RAX)) return result;
                    if (!store_result(instruction.result, instruction.result_type.empty() ? "U64" : instruction.result_type)) return result;
                    break;
                }
                const bool read = op == "READ8" || op == "READ16" || op == "READ32" || op == "READ64";
                const bool write = op == "WRITE8" || op == "WRITE16" || op == "WRITE32" || op == "WRITE64" || physical_write;
                if (!read && !write) { result.ok = false; result.error = "unsupported memory operation " + op; return result; }
                if (!load_value(instruction.operands[0], instruction.operand_types.empty() ? (physical_write ? "PHYSICALPTR" : "VIRTUALPTR") : instruction.operand_types.front(), Reg::RAX)) return result;
                if (read) {
                    if (op == "READ8") result.text.mov_load8_rax();
                    else if (op == "READ16") result.text.mov_load16_rax();
                    else if (op == "READ32") result.text.mov_load32_rax();
                    else result.text.mov_load64_rax();
                    if (!store_result(instruction.result, instruction.result_type)) return result;
                } else {
                    if (instruction.operands.size() < 2 || !load_value(instruction.operands[1], instruction.operand_types.size() > 1 ? instruction.operand_types[1] : "U64", Reg::RCX)) return result;
                    if (physical_write) result.text.mov_store64_rax_from_rcx();
                    else if (op == "WRITE8") result.text.mov_store8_rax_from_cl();
                    else if (op == "WRITE16") result.text.mov_store16_rax_from_cx();
                    else if (op == "WRITE32") result.text.mov_store32_rax_from_ecx();
                    else result.text.mov_store64_rax_from_rcx();
                }
                break;
            }

            case AmirInstruction::Kind::CallValue: {
                const AmirFunction* callee = nullptr;
                for (const auto& candidate : module.functions) {
                    if (candidate.name == instruction.target) { callee = &candidate; break; }
                }
                if (!callee) {
                    result.ok = false;
                    result.error = "freestanding call target \"" + instruction.target + "\" is not a declared function";
                    return result;
                }
                if (instruction.operands.size() != callee->params.size()) {
                    result.ok = false;
                    result.error = "call to \"" + instruction.target + "\" expects " +
                        std::to_string(callee->params.size()) + " arguments, got " + std::to_string(instruction.operands.size());
                    return result;
                }
                const auto call_locations = systems::assign_argument_locations(static_cast<int>(instruction.operands.size()));
                for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                    const int argument_slot = slot_of(instruction.operands[i]);
                    if (argument_slot < 0) {
                        result.ok = false;
                        result.error = "internal call argument \"" + instruction.operands[i] + "\" has no assigned stack slot";
                        return result;
                    }
                    if (call_locations[i].in_register) {
                        if (argument_slot <= 127) result.text.mov_load_disp8(kRegisterByName.at(call_locations[i].register_name), Reg::RSP,
                            static_cast<std::uint8_t>(argument_slot));
                        else result.text.mov_load_disp32(kRegisterByName.at(call_locations[i].register_name), Reg::RSP,
                            static_cast<std::uint32_t>(argument_slot));
                    } else {
                        if (argument_slot <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(argument_slot));
                        else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(argument_slot));
                        const std::uint32_t outgoing_offset = static_cast<std::uint32_t>(outgoing_base + 8 * (static_cast<int>(i) - 4));
                        if (outgoing_offset <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(outgoing_offset), Reg::RAX);
                        else result.text.mov_store_disp32(Reg::RSP, outgoing_offset, Reg::RAX);
                    }
                }
                const auto call_disp = result.text.call_rel32_placeholder();
                result.internal_calls.push_back({call_disp, instruction.target});
                if (!store_result(instruction.result, instruction.result_type.empty() ? callee->return_type : instruction.result_type)) return result;
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
                std::string receiver_type = instruction.operand_types.empty() ? "" : instruction.operand_types.front();
                if (receiver_type.empty()) {
                for (const auto& declared_parameter : target->params) {
                    if (bare_parameter_name(declared_parameter) == receiver) {
                        receiver_type = declared_parameter_type(declared_parameter);
                        break;
                    }
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
                if (receiver_slot <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(receiver_slot));
                else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(receiver_slot));

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
                    if (field->offset_bytes <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RAX, static_cast<std::uint8_t>(field->offset_bytes));
                    else result.text.mov_load_disp32(Reg::RAX, Reg::RAX, static_cast<std::uint32_t>(field->offset_bytes));
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

                // RAX now holds the resolved "This" pointer (arcology-os/docs/systems/uefi-bindings.md: the
                // implicit first argument real UEFI protocol methods take in the underlying C ABI).
                const int explicit_arg_count = static_cast<int>(instruction.operands.size());
                const auto locations = systems::assign_argument_locations(
                    explicit_arg_count + (final_field->implicit_this_argument ? 1 : 0));
                const bool has_stack_arguments = std::any_of(locations.begin(), locations.end(),
                    [](const systems::ArgumentLocation& location) { return !location.in_register; });
                if (has_stack_arguments) {
                    // RAX currently holds the resolved protocol method table. Preserve it while
                    // loading values for outgoing stack arguments; R11 is caller-saved under the
                    // Microsoft x64 ABI and is safe until the indirect call.
                    result.text.mov_reg_reg(Reg::R11, Reg::RAX);
                }
                std::size_t location_index = 0;
                if (final_field->implicit_this_argument) {
                    if (locations[location_index].in_register) {
                        result.text.mov_reg_reg(kRegisterByName.at(locations[location_index].register_name), Reg::RAX);
                    } else {
                        result.ok = false;
                        result.error = "external call \"" + instruction.target + "\" cannot pass implicit This on the stack";
                        return result;
                    }
                    ++location_index;
                }
                for (int i = 0; i < explicit_arg_count; ++i, ++location_index) {
                    const int argument_slot = slot_of(instruction.operands[static_cast<std::size_t>(i)]);
                    if (argument_slot < 0) {
                        result.ok = false;
                        result.error = "external call argument \"" + instruction.operands[static_cast<std::size_t>(i)] +
                            "\" has no assigned stack slot";
                        return result;
                    }
                    if (locations[location_index].in_register) {
                        if (argument_slot <= 127) result.text.mov_load_disp8(kRegisterByName.at(locations[location_index].register_name), Reg::RSP,
                                                    static_cast<std::uint8_t>(argument_slot));
                        else result.text.mov_load_disp32(kRegisterByName.at(locations[location_index].register_name), Reg::RSP,
                                                    static_cast<std::uint32_t>(argument_slot));
                    } else {
                        if (argument_slot <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(argument_slot));
                        else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(argument_slot));
                        const std::uint32_t outgoing_offset = static_cast<std::uint32_t>(outgoing_base +
                            8 * (static_cast<int>(location_index) - 4));
                        if (outgoing_offset <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(outgoing_offset), Reg::RAX);
                        else result.text.mov_store_disp32(Reg::RSP, outgoing_offset, Reg::RAX);
                    }
                }

                if (final_field->offset_bytes <= 0x7F) {
                    result.text.call_indirect_disp8(has_stack_arguments ? Reg::R11 : Reg::RAX, static_cast<std::uint8_t>(final_field->offset_bytes));
                } else {
                    result.text.call_indirect_disp32(has_stack_arguments ? Reg::R11 : Reg::RAX, static_cast<std::uint32_t>(final_field->offset_bytes));
                }
                const int result_slot = slot_of(instruction.result);
                if (result_slot < 0) {
                    result.ok = false;
                    result.error = "external call result has no assigned stack slot";
                    return result;
                }
                if (result_slot <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(result_slot), Reg::RAX);
                else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(result_slot), Reg::RAX);
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
                    if (value_slot <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(value_slot));
                    else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(value_slot));
                }
                if (frame_size <= 255) result.text.add_rsp_imm8(static_cast<std::uint8_t>(frame_size));
                else result.text.add_rsp_imm32(static_cast<std::uint32_t>(frame_size));
                result.text.ret();
                break;
            }

            default:
                result.ok = false;
                result.error = "this milestone's code generator does not support this A-MIR instruction kind";
                return result;
        }
        }
    }

    for (const auto& fixup : branch_fixups) {
        const auto target_offset = block_offsets.find(fixup.target);
        if (target_offset == block_offsets.end()) {
            result.ok = false;
            result.error = "unresolved x86-64 branch target \"" + fixup.target + "\"";
            return result;
        }
        const std::int64_t next_instruction = static_cast<std::int64_t>(fixup.displacement_offset + 4);
        const std::int64_t displacement = static_cast<std::int64_t>(target_offset->second) - next_instruction;
        if (displacement < std::numeric_limits<std::int32_t>::min() || displacement > std::numeric_limits<std::int32_t>::max()) {
            result.ok = false;
            result.error = "x86-64 branch displacement overflow for target \"" + fixup.target + "\"";
            return result;
        }
        result.text.patch_i32(fixup.displacement_offset, static_cast<std::int32_t>(displacement));
    }

    return result;
}

// Emit the entry function followed by every declared helper. Calls use rel32 displacements and
// are patched only after all function bases are known. The synthetic top-level wrapper is omitted
// when a real function with the same name is selected, preserving the existing last-declaration
// entry rule.
X86_64CodegenResult generate_x86_64_program(const AmirModule& module, const std::string& entry_function) {
    X86_64CodegenResult combined;
    combined.entry_symbol = entry_function;
    std::vector<std::pair<std::string, X86_64CodegenResult>> fragments;
    std::unordered_set<std::string> emitted;
    auto add_fragment = [&](const std::string& name) -> bool {
        if (!emitted.insert(name).second) return true;
        auto fragment = generate_x86_64_function(module, name);
        if (!fragment.ok) { combined.ok = false; combined.error = fragment.error; return false; }
        fragments.emplace_back(name, std::move(fragment));
        return true;
    };
    if (!add_fragment(entry_function)) return combined;
    for (const auto& function : module.functions) {
        if (function.name == entry_function) continue;
        bool declaration_wrapper = false;
        if (function.name == "Main") {
            for (const auto& block : function.blocks) {
                for (const auto& instruction : block.instructions) {
                    if (instruction.kind == AmirInstruction::Kind::DeclareFunction) {
                        declaration_wrapper = true;
                        break;
                    }
                }
                if (declaration_wrapper) break;
            }
        }
        if (declaration_wrapper) continue;
        bool has_body = false;
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if (instruction.kind != AmirInstruction::Kind::DeclareFunction) {
                    has_body = true;
                    break;
                }
            }
            if (has_body) break;
        }
        if (!has_body) continue; // synthetic declaration-only wrapper
        if (!add_fragment(function.name)) return combined;
    }

    std::unordered_map<std::string, std::size_t> function_offsets;
    for (const auto& fragment : fragments) {
        function_offsets[fragment.first] = combined.text.size();
        const auto text_base = combined.text.size();
        const auto rdata_base = combined.rdata.size();
        combined.text.append_bytes(fragment.second.text.bytes());
        combined.rdata.insert(combined.rdata.end(), fragment.second.rdata.begin(), fragment.second.rdata.end());
        for (const auto& relocation : fragment.second.relocations) {
            combined.relocations.push_back({text_base + relocation.disp_field_offset,
                text_base + relocation.instruction_end_offset, rdata_base + relocation.rdata_offset});
        }
        for (const auto& call : fragment.second.internal_calls) {
            combined.internal_calls.push_back({text_base + call.disp_field_offset, call.target});
        }
    }
    for (const auto& call : combined.internal_calls) {
        const auto target = function_offsets.find(call.target);
        if (target == function_offsets.end()) {
            combined.ok = false;
            combined.error = "unresolved internal function target \"" + call.target + "\"";
            return combined;
        }
        const std::int64_t next_instruction = static_cast<std::int64_t>(call.disp_field_offset + 4);
        const std::int64_t displacement = static_cast<std::int64_t>(target->second) - next_instruction;
        if (displacement < std::numeric_limits<std::int32_t>::min() || displacement > std::numeric_limits<std::int32_t>::max()) {
            combined.ok = false;
            combined.error = "internal call displacement overflow for target \"" + call.target + "\"";
            return combined;
        }
        combined.text.patch_i32(call.disp_field_offset, static_cast<std::int32_t>(displacement));
    }
    return combined;
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
    out << "\nINTERNAL_CALLS " << codegen.internal_calls.size() << "\n";
    for (const auto& call : codegen.internal_calls) {
        out << "    TEXT+" << std::hex << call.disp_field_offset << " REL32_TO " << call.target << std::dec << "\n";
    }
    return out.str();
}

Value parse_constant_value(const std::string& text) {
    if (text == "nothing" || text == "null") {
        return Value();
    }
    if (text == "true") {
        return true;
    }
    if (text == "false") {
        return false;
    }
    if (text == "[]") {
        return Value(Value::Array{});
    }
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        return unquote_constant(text);
    }
    if (text.rfind("BITS \"", 0) == 0 && text.size() >= 7 && text.back() == '"') {
        return Value(BitVector::from_string(text.substr(6, text.size() - 7)));
    }
    return std::stod(text);
}

bool looks_like_inline_bytecode_value(const std::string& text) {
    if (text.empty()) return false;
    if (text == "nothing" || text == "null" || text == "true" || text == "false" || text == "[]") return true;
    if (text.front() == '"') return true;
    if (text.rfind("BITS \"", 0) == 0) return true;
    if (text.front() >= '0' && text.front() <= '9') return true;
    if ((text.front() == '-' || text.front() == '+') && text.size() > 1 && text[1] >= '0' && text[1] <= '9') return true;
    return false;
}

std::size_t ref_index(const std::string& ref, char prefix, const std::string& what) {
    if (ref.empty() || ref[0] != prefix) {
        throw std::runtime_error("invalid bytecode " + what + " reference: " + ref);
    }
    std::size_t offset = 1;
    if (prefix == '%' && offset < ref.size() && ref[offset] == 't') {
        ++offset;
    }
    if (offset >= ref.size() || ref.find_first_not_of("0123456789", offset) != std::string::npos) {
        throw std::runtime_error("invalid bytecode " + what + " reference: " + ref);
    }
    return static_cast<std::size_t>(std::stoul(ref.substr(offset)));
}

BytecodeOperand prepare_operand(const std::string& text) {
    if (text.empty()) return {};
    if (text[0] == '%') return {BytecodeOperandKind::Temp, ref_index(text, '%', "temporary"), Value()};
    if (text.size() > 1 && text[0] == 'K' && text[1] >= '0' && text[1] <= '9') {
        return {BytecodeOperandKind::Constant, ref_index(text, 'K', "constant"), Value()};
    }
    if (text.size() > 1 && text[0] == 'L' && text[1] >= '0' && text[1] <= '9') {
        return {BytecodeOperandKind::Local, ref_index(text, 'L', "local"), Value()};
    }
    if (looks_like_inline_bytecode_value(text)) {
        BytecodeOperand operand{BytecodeOperandKind::InlineValue, 0, parse_constant_value(text), {}};
        operand.inline_slot = slot_from_value(operand.value);
        return operand;
    }
    return {BytecodeOperandKind::Symbol, 0, Value()};
}

void prepare_bytecode_module(BytecodeModule& module) {
    module.constant_values.clear();
    module.constant_values.reserve(module.constants.size());
    module.constant_slots.clear();
    module.constant_slots.reserve(module.constants.size());
    for (const auto& constant : module.constants) {
        module.constant_values.push_back(parse_constant_value(constant));
        module.constant_slots.push_back(slot_from_value(module.constant_values.back()));
    }

    module.function_indices.clear();
    for (std::size_t function_index = 0; function_index < module.functions.size(); ++function_index) {
        // Keep the historical first-match behavior used by find_function.
        module.function_indices.emplace(module.functions[function_index].name, function_index);
    }

    for (auto& function : module.functions) {
        function.targets.clear();
        function.local_refs_by_base.clear();
        function.param_local_refs.clear();
        function.param_defaults.clear();
        function.temp_count = 0;

        for (std::size_t local_index = 0; local_index < function.locals.size(); ++local_index) {
            function.local_refs_by_base.emplace(local_base_name(function.locals[local_index]), "L" + std::to_string(local_index));
        }

        function.param_local_refs.reserve(function.params.size());
        function.param_defaults.reserve(function.params.size());
        for (const auto& param : function.params) {
            const auto found = function.local_refs_by_base.find(local_base_name(param));
            function.param_local_refs.push_back(found == function.local_refs_by_base.end() ? std::string() : found->second);
            const std::string default_text = param_default_text(param);
            function.param_defaults.push_back(default_text.empty() ? std::optional<Value>() : std::optional<Value>(parse_constant_value(default_text)));
        }

        for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
            function.targets[function.blocks[block_index].name] = BytecodeCursor{block_index, 0};
            auto& block = function.blocks[block_index];
            for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
                auto& instruction = block.instructions[instruction_index];
                instruction.call_site_resolution = CallSiteResolution::Unresolved;
                instruction.call_site_function = nullptr;
                instruction.call_args_scratch.clear();
                instruction.prepared_call_key.clear();
                instruction.store_index_scratch.clear();
                instruction.prepared_operands.clear();
                instruction.prepared_operands.reserve(instruction.operands.size());
                for (const auto& operand : instruction.operands) {
                    instruction.prepared_operands.push_back(prepare_operand(operand));
                    if (!operand.empty() && operand[0] == '%') {
                        function.temp_count = std::max(function.temp_count, ref_index(operand, '%', "temporary") + 1);
                    }
                }
                if (instruction.op == BytecodeOp::Source && !instruction.operands.empty()) {
                    instruction.prepared_source_line = std::stoi(instruction.operands.front());
                }
                if (instruction.op == BytecodeOp::Label && !instruction.operands.empty()) {
                    function.targets[instruction.operands.front()] = BytecodeCursor{block_index, instruction_index + 1};
                }
                switch (instruction.op) {
                    case BytecodeOp::Binary:
                    case BytecodeOp::BinaryLocalLocal:
                    case BytecodeOp::BinaryLocalConst:
                        if (instruction.operands.size() > 1) {
                            instruction.prepared_numeric_op = numeric_op_from_text(instruction.operands[1]);
                        }
                        break;
                    case BytecodeOp::BranchLocalLocal:
                        if (!instruction.operands.empty()) {
                            instruction.prepared_numeric_op = numeric_op_from_text(instruction.operands[0]);
                        }
                        break;
                    case BytecodeOp::CallValue:
                        if (instruction.operands.size() > 1 && instruction.operands[1].find('.') == std::string::npos) {
                            instruction.prepared_call_key = lowered_call_key(instruction.operands[1]);
                        }
                        break;
                    case BytecodeOp::CallRuntime:
                        if (!instruction.operands.empty() && instruction.operands[0].find('.') == std::string::npos) {
                            instruction.prepared_call_key = lowered_call_key(instruction.operands[0]);
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        const auto prepared_target = [&](const std::string& name) {
            const auto found = function.targets.find(name);
            if (found == function.targets.end()) {
                throw std::runtime_error("unresolved bytecode target: " + name);
            }
            return found->second;
        };
        for (auto& block : function.blocks) {
            for (auto& instruction : block.instructions) {
                instruction.prepared_targets.clear();
                if (instruction.op == BytecodeOp::Jump && !instruction.operands.empty()) {
                    instruction.prepared_targets.push_back(prepared_target(instruction.operands.front()));
                } else if (instruction.op == BytecodeOp::Branch && instruction.operands.size() >= 3) {
                    instruction.prepared_targets.push_back(prepared_target(instruction.operands[1]));
                    instruction.prepared_targets.push_back(prepared_target(instruction.operands[2]));
                } else if (instruction.op == BytecodeOp::BranchLocalLocal && instruction.operands.size() >= 5) {
                    instruction.prepared_targets.push_back(prepared_target(instruction.operands[3]));
                    instruction.prepared_targets.push_back(prepared_target(instruction.operands[4]));
                } else if (instruction.op == BytecodeOp::TryBegin && !instruction.operands.empty()) {
                    instruction.prepared_targets.push_back(prepared_target(instruction.operands.front()));
                }
            }
        }
    }
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
        if (left.is_bit_vector() || right.is_bit_vector()) {
            if (!left.is_bit_vector() || !right.is_bit_vector()) {
                throw std::runtime_error("bit vector concatenation requires two BITVECTOR values");
            }
            return Value(BitVector::from_string(left.as_bit_vector().string() + right.as_bit_vector().string()));
        }
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
        if (left.is_range()) {
            const double value = right.as_number();
            if (!std::isfinite(value) || std::floor(value) != value) return false;
            return left.as_range().contains(static_cast<long long>(value));
        }
        return left.to_string().find(right.to_string()) != std::string::npos;
    }
    if (op == "IN") {
        if (right.is_array()) {
            for (const auto& item : right.as_array()) {
                if (values_equal(left, item)) return true;
            }
            return false;
        }
        if (right.is_range()) {
            const double value = left.as_number();
            if (!std::isfinite(value) || std::floor(value) != value) return false;
            return right.as_range().contains(static_cast<long long>(value));
        }
        return right.to_string().find(left.to_string()) != std::string::npos;
    }
    throw std::runtime_error("unsupported bytecode binary operator: " + op);
}

struct BytecodeFrame {
    std::vector<BytecodeSlot> temps;
    std::vector<BytecodeSlot> locals;
};

Value slot_value(const BytecodeSlot& slot, const std::string& diagnostic_ref) {
    switch (slot.kind) {
        case BytecodeSlot::Kind::Number:
            return slot.number;
        case BytecodeSlot::Kind::Boolean:
            return slot.boolean;
        case BytecodeSlot::Kind::Value:
            return slot.value;
        case BytecodeSlot::Kind::Undefined:
            throw std::runtime_error("undefined bytecode value: " + diagnostic_ref);
    }
    return Value();
}

std::optional<double> slot_number(const BytecodeSlot& slot) {
    if (slot.kind == BytecodeSlot::Kind::Number) return slot.number;
    if (slot.kind == BytecodeSlot::Kind::Boolean) return slot.boolean ? 1.0 : 0.0;
    return std::nullopt;
}

bool slot_truthy(const BytecodeSlot& slot, const std::string& diagnostic_ref) {
    if (slot.kind == BytecodeSlot::Kind::Number) return slot.number != 0.0;
    if (slot.kind == BytecodeSlot::Kind::Boolean) return slot.boolean;
    return slot_value(slot, diagnostic_ref).truthy();
}

std::size_t local_index_from_ref(const std::string& ref) {
    return ref_index(ref, 'L', "local");
}

Value temp_value(const BytecodeFrame& frame, std::size_t index, const std::string& ref) {
    if (index >= frame.temps.size() || frame.temps[index].kind == BytecodeSlot::Kind::Undefined) {
        throw std::runtime_error("undefined bytecode temporary: " + ref);
    }
    return slot_value(frame.temps[index], ref);
}

void set_temp(BytecodeFrame& frame, const BytecodeOperand& target, Value value, const std::string& ref) {
    if (target.kind != BytecodeOperandKind::Temp) {
        throw std::runtime_error("invalid bytecode temporary target: " + ref);
    }
    if (target.index >= frame.temps.size()) {
        frame.temps.resize(target.index + 1);
    }
    frame.temps[target.index] = slot_from_value(std::move(value));
}

void set_temp_slot(BytecodeFrame& frame, const BytecodeOperand& target, BytecodeSlot slot, const std::string& ref) {
    if (target.kind != BytecodeOperandKind::Temp) {
        throw std::runtime_error("invalid bytecode temporary target: " + ref);
    }
    if (target.index >= frame.temps.size()) {
        frame.temps.resize(target.index + 1);
    }
    frame.temps[target.index] = std::move(slot);
}

void set_temp_number(BytecodeFrame& frame, const BytecodeOperand& target, double value, const std::string& ref) {
    BytecodeSlot slot;
    slot.kind = BytecodeSlot::Kind::Number;
    slot.number = value;
    set_temp_slot(frame, target, std::move(slot), ref);
}

void set_temp_bool(BytecodeFrame& frame, const BytecodeOperand& target, bool value, const std::string& ref) {
    BytecodeSlot slot;
    slot.kind = BytecodeSlot::Kind::Boolean;
    slot.boolean = value;
    set_temp_slot(frame, target, std::move(slot), ref);
}

Value local_value(const BytecodeFunction& function, const BytecodeFrame& frame, std::size_t index, const std::string& ref) {
    if (index >= function.locals.size()) {
        throw std::runtime_error("local index out of range: " + ref);
    }
    if (index < frame.locals.size() && frame.locals[index].kind != BytecodeSlot::Kind::Undefined) {
        return slot_value(frame.locals[index], ref);
    }
    throw std::runtime_error("undefined bytecode local: " + function.locals[index]);
}

void set_local(const BytecodeFunction& function, BytecodeFrame& frame, std::size_t index, Value value, const std::string& ref) {
    if (index >= function.locals.size()) {
        throw std::runtime_error("local index out of range: " + ref);
    }
    if (index >= frame.locals.size()) {
        frame.locals.resize(index + 1);
    }
    frame.locals[index] = slot_from_value(std::move(value));
}

void set_local_slot(const BytecodeFunction& function, BytecodeFrame& frame, std::size_t index, BytecodeSlot slot, const std::string& ref) {
    if (index >= function.locals.size()) {
        throw std::runtime_error("local index out of range: " + ref);
    }
    if (index >= frame.locals.size()) {
        frame.locals.resize(index + 1);
    }
    frame.locals[index] = std::move(slot);
}

Value operand_value(const BytecodeModule& module, const BytecodeFunction& function, const BytecodeFrame& frame, const std::string& operand) {
    if (operand.empty()) {
        return Value();
    }
    if (operand[0] == '%') {
        return temp_value(frame, ref_index(operand, '%', "temporary"), operand);
    }
    if (operand[0] == 'K') {
        const std::size_t index = static_cast<std::size_t>(std::stoul(operand.substr(1)));
        if (index >= module.constants.size()) {
            throw std::runtime_error("constant index out of range: " + operand);
        }
        if (index < module.constant_values.size()) {
            return module.constant_values[index];
        }
        return parse_constant_value(module.constants[index]);
    }
    if (operand[0] == 'L') {
        const std::size_t index = static_cast<std::size_t>(std::stoul(operand.substr(1)));
        if (index >= function.locals.size()) {
            throw std::runtime_error("local index out of range: " + operand);
        }
        if (index < frame.locals.size() && frame.locals[index].kind != BytecodeSlot::Kind::Undefined) {
            return slot_value(frame.locals[index], operand);
        }
        throw std::runtime_error("undefined bytecode local: " + function.locals[index]);
    }
    return parse_constant_value(operand);
}

// Returns a reference into existing storage (a frame slot, or a slot precomputed once by
// prepare_bytecode_module/prepare_operand) rather than a fresh BytecodeSlot, so reading an
// operand for arithmetic/branching doesn't copy a Value on every instruction.
const BytecodeSlot& operand_slot(const BytecodeModule& module, const BytecodeFunction& function, const BytecodeFrame& frame,
                                 const BytecodeOperand& operand, const std::string& text) {
    switch (operand.kind) {
        case BytecodeOperandKind::Temp:
            if (operand.index >= frame.temps.size() || frame.temps[operand.index].kind == BytecodeSlot::Kind::Undefined) {
                throw std::runtime_error("undefined bytecode temporary: " + text);
            }
            return frame.temps[operand.index];
        case BytecodeOperandKind::Constant:
            if (operand.index >= module.constant_slots.size()) {
                throw std::runtime_error("constant index out of range: " + text);
            }
            return module.constant_slots[operand.index];
        case BytecodeOperandKind::Local:
            if (operand.index >= function.locals.size()) {
                throw std::runtime_error("local index out of range: " + text);
            }
            if (operand.index >= frame.locals.size() || frame.locals[operand.index].kind == BytecodeSlot::Kind::Undefined) {
                throw std::runtime_error("undefined bytecode local: " + function.locals[operand.index]);
            }
            return frame.locals[operand.index];
        case BytecodeOperandKind::InlineValue:
            return operand.inline_slot;
        case BytecodeOperandKind::Empty:
        case BytecodeOperandKind::Symbol:
            break;
    }
    throw std::runtime_error("bytecode operand is not a prepared value: " + text);
}

Value operand_value(const BytecodeModule& module, const BytecodeFunction& function, const BytecodeFrame& frame,
                    const BytecodeOperand& operand, const std::string& text) {
    return slot_value(operand_slot(module, function, frame, operand, text), text);
}

std::optional<double> operand_number(const BytecodeModule& module, const BytecodeFunction& function, const BytecodeFrame& frame,
                                     const BytecodeOperand& operand, const std::string& text) {
    return slot_number(operand_slot(module, function, frame, operand, text));
}

bool eval_numeric_comparison(const std::string& op, double left, double right) {
    if (op == "<") return left < right;
    if (op == "<=") return left <= right;
    if (op == ">") return left > right;
    if (op == ">=") return left >= right;
    if (op == "==" || op == "=") return left == right;
    if (op == "!=") return left != right;
    throw std::runtime_error("unsupported fused numeric comparison: " + op);
}

double eval_numeric_arithmetic(const std::string& op, double left, double right) {
    if (op == "+") return left + right;
    if (op == "-") return left - right;
    if (op == "*") return left * right;
    if (op == "/") return left / right;
    if (op == "MOD") {
        if (right == 0.0) throw std::runtime_error("MOD divisor cannot be zero");
        return std::fmod(left, right);
    }
    throw std::runtime_error("unsupported fused numeric arithmetic: " + op);
}

bool eval_numeric_comparison(NumericOp op, double left, double right) {
    switch (op) {
        case NumericOp::Lt: return left < right;
        case NumericOp::Le: return left <= right;
        case NumericOp::Gt: return left > right;
        case NumericOp::Ge: return left >= right;
        case NumericOp::Eq: return left == right;
        case NumericOp::Ne: return left != right;
        default:
            throw std::runtime_error("unsupported fused numeric comparison");
    }
}

double eval_numeric_arithmetic(NumericOp op, double left, double right) {
    switch (op) {
        case NumericOp::Add: return left + right;
        case NumericOp::Sub: return left - right;
        case NumericOp::Mul: return left * right;
        case NumericOp::Div: return left / right;
        case NumericOp::Mod:
            if (right == 0.0) throw std::runtime_error("MOD divisor cannot be zero");
            return std::fmod(left, right);
        default:
            throw std::runtime_error("unsupported fused numeric arithmetic");
    }
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
    const auto indexed = module.function_indices.find(name);
    if (indexed != module.function_indices.end() && indexed->second < module.functions.size()) {
        return &module.functions[indexed->second];
    }
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
        const auto points = utf8_codepoints(target.to_string());
        if (index < 0 || static_cast<std::size_t>(index) >= points.size()) {
            throw std::runtime_error("string index out of range");
        }
        return points[static_cast<std::size_t>(index)];
    }
    if (target.is_bit_vector()) {
        const double numeric_index = index_value.as_number();
        if (!std::isfinite(numeric_index) || std::floor(numeric_index) != numeric_index || numeric_index < 0 ||
            numeric_index >= static_cast<double>(target.as_bit_vector().length)) {
            throw std::runtime_error("bit vector index out of range");
        }
        return target.as_bit_vector().get(static_cast<std::size_t>(numeric_index)) ? 1.0 : 0.0;
    }
    if (target.is_tuple()) {
        const double numeric_index = index_value.as_number();
        if (!std::isfinite(numeric_index) || std::floor(numeric_index) != numeric_index || numeric_index < 0 ||
            numeric_index >= static_cast<double>(target.as_tuple().size())) {
            throw std::runtime_error("tuple index out of range");
        }
        return target.as_tuple()[static_cast<std::size_t>(numeric_index)];
    }
    if (target.is_range()) {
        const double numeric_index = index_value.as_number();
        if (!std::isfinite(numeric_index) || std::floor(numeric_index) != numeric_index || numeric_index < 0 ||
            numeric_index >= static_cast<double>(target.as_range().length)) {
            throw std::runtime_error("range index out of range");
        }
        return static_cast<double>(target.as_range().at(static_cast<std::size_t>(numeric_index)));
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

Value execute_function(const BytecodeModule& module, const BytecodeFunction& function, Runtime& runtime, const std::vector<Value>& args,
                       bool count_instructions) {
    if (function.blocks.empty()) {
        throw std::runtime_error(function.name + " has no bytecode blocks");
    }

    struct TryHandler {
        BytecodeCursor catch_cursor;
        std::string error_name;
    };

    const auto jump_to = [&](const std::string& target) {
        const auto found = function.targets.find(target);
        if (found == function.targets.end()) {
            throw std::runtime_error("unresolved bytecode target: " + target);
        }
        return found->second;
    };

    BytecodeFrame frame;
    frame.temps.resize(function.temp_count);
    frame.locals.resize(function.locals.size());
    for (std::size_t i = 0; i < function.param_local_refs.size(); ++i) {
        if (function.param_local_refs[i].empty()) {
            continue;
        }
        const std::size_t local_index = local_index_from_ref(function.param_local_refs[i]);
        if (local_index >= frame.locals.size()) {
            throw std::runtime_error("local index out of range: " + function.param_local_refs[i]);
        }
        if (i < args.size()) {
            set_local(function, frame, local_index, args[i], function.param_local_refs[i]);
        } else if (i < function.param_defaults.size() && function.param_defaults[i].has_value()) {
            set_local(function, frame, local_index, *function.param_defaults[i], function.param_local_refs[i]);
        }
    }

    BytecodeCursor cursor{0, 0};
    std::vector<TryHandler> try_stack;
    int current_source_line = 0;
    while (cursor.block < function.blocks.size()) {
        const BytecodeBlock& block = function.blocks[cursor.block];
        if (cursor.instruction >= block.instructions.size()) {
            return Value();
        }
        const BytecodeInstruction& instruction = block.instructions[cursor.instruction++];
        const auto& prepared = instruction.prepared_operands;
        const auto value_at = [&](std::size_t index) {
            return operand_value(module, function, frame, prepared[index], instruction.operands[index]);
        };
        const auto slot_at = [&](std::size_t index) -> const BytecodeSlot& {
            return operand_slot(module, function, frame, prepared[index], instruction.operands[index]);
        };
        const auto number_at = [&](std::size_t index) {
            return operand_number(module, function, frame, prepared[index], instruction.operands[index]);
        };
        const auto set_temp_at = [&](std::size_t index, Value value) {
            set_temp(frame, prepared[index], std::move(value), instruction.operands[index]);
        };
        const auto set_temp_slot_at = [&](std::size_t index, BytecodeSlot slot) {
            set_temp_slot(frame, prepared[index], std::move(slot), instruction.operands[index]);
        };
        const auto local_index_at = [&](std::size_t index) {
            if (index < prepared.size() && prepared[index].kind == BytecodeOperandKind::Local) return prepared[index].index;
            return local_index_from_ref(instruction.operands[index]);
        };
        if (count_instructions) runtime.tick();
        try {
            switch (instruction.op) {
            case BytecodeOp::Label:
                break;
            case BytecodeOp::Source:
                current_source_line = instruction.prepared_source_line;
                break;
            case BytecodeOp::Const:
                set_temp_slot_at(0, slot_at(1));
                break;
            case BytecodeOp::Load:
                set_temp_slot_at(0, slot_at(1));
                break;
            case BytecodeOp::Store: {
                set_local_slot(function, frame, local_index_at(0), slot_at(1), instruction.operands[0]);
                break;
            }
            case BytecodeOp::StoreIndex: {
                if (instruction.operands.size() < 3) {
                    throw std::runtime_error("STORE_INDEX expects a target, at least one index, and a value");
                }
                Value target = value_at(0);
                auto& indexes = instruction.store_index_scratch;
                indexes.clear();
                indexes.reserve(instruction.operands.size() > 2 ? instruction.operands.size() - 2 : 0);
                for (std::size_t i = 1; i + 1 < instruction.operands.size(); ++i) {
                    indexes.push_back(value_at(i));
                }
                Value value = value_at(instruction.operands.size() - 1);
                assign_indexed(target, indexes, 0, value);
                set_local(function, frame, local_index_at(0), target, instruction.operands[0]);
                break;
            }
            case BytecodeOp::StoreSlice: {
                if (instruction.operands.size() != 4) throw std::runtime_error("STORE_SLICE expects four operands");
                Value target = value_at(0);
                const Value first = value_at(1);
                const Value last = value_at(2);
                const Value replacement = value_at(3);
                const auto start = first.is_null() ? std::nullopt
                    : std::optional<long long>(exact_slice_integer(first.as_number(), "start"));
                const auto end = last.is_null() ? std::nullopt
                    : std::optional<long long>(exact_slice_integer(last.as_number(), "end"));
                target = replace_array_slice(target, start, end, replacement);
                set_local(function, frame, local_index_at(0), target, instruction.operands[0]);
                break;
            }
            case BytecodeOp::Unary:
                set_temp_at(0, eval_unary(instruction.operands[1], value_at(2)));
                break;
            case BytecodeOp::Binary: {
                bool handled = false;
                if (const auto left = number_at(2), right = number_at(3); left.has_value() && right.has_value()) {
                    switch (instruction.prepared_numeric_op) {
                        case NumericOp::Add: set_temp_number(frame, prepared[0], *left + *right, instruction.operands[0]); handled = true; break;
                        case NumericOp::Sub: set_temp_number(frame, prepared[0], *left - *right, instruction.operands[0]); handled = true; break;
                        case NumericOp::Mul: set_temp_number(frame, prepared[0], *left * *right, instruction.operands[0]); handled = true; break;
                        case NumericOp::Div: set_temp_number(frame, prepared[0], *left / *right, instruction.operands[0]); handled = true; break;
                        case NumericOp::Lt: set_temp_bool(frame, prepared[0], *left < *right, instruction.operands[0]); handled = true; break;
                        case NumericOp::Le: set_temp_bool(frame, prepared[0], *left <= *right, instruction.operands[0]); handled = true; break;
                        case NumericOp::Gt: set_temp_bool(frame, prepared[0], *left > *right, instruction.operands[0]); handled = true; break;
                        case NumericOp::Ge: set_temp_bool(frame, prepared[0], *left >= *right, instruction.operands[0]); handled = true; break;
                        case NumericOp::Eq: set_temp_bool(frame, prepared[0], *left == *right, instruction.operands[0]); handled = true; break;
                        case NumericOp::Ne: set_temp_bool(frame, prepared[0], *left != *right, instruction.operands[0]); handled = true; break;
                        default: break;
                    }
                }
                if (!handled) {
                    set_temp_at(0, eval_binary(instruction.operands[1], value_at(2), value_at(3)));
                }
                break;
            }
            case BytecodeOp::CallValue: {
                auto& args = instruction.call_args_scratch;
                args.clear();
                args.reserve(instruction.operands.size() > 2 ? instruction.operands.size() - 2 : 0);
                for (std::size_t i = 2; i < instruction.operands.size(); ++i) {
                    args.push_back(value_at(i));
                }
                const auto call_host = [&](const std::vector<Value>& call_args) {
                    return instruction.prepared_call_key.empty()
                        ? runtime.call_host_function(instruction.operands[1], call_args)
                        : runtime.call_host_function_prepared(instruction.prepared_call_key, call_args);
                };
                if (instruction.call_site_resolution == CallSiteResolution::UserFunction) {
                    set_temp_at(0, execute_function(module, *instruction.call_site_function, runtime, args, count_instructions));
                    break;
                }
                if (instruction.call_site_resolution == CallSiteResolution::HostFunction) {
                    set_temp_at(0, call_host(args));
                    break;
                }
                std::optional<Value> callable_target;
                const auto local_ref = function.local_refs_by_base.find(instruction.operands[1]);
                if (local_ref != function.local_refs_by_base.end()) {
                    const std::size_t local_index = local_index_from_ref(local_ref->second);
                    if (local_index < frame.locals.size() && frame.locals[local_index].kind != BytecodeSlot::Kind::Undefined) {
                        Value callable_value = slot_value(frame.locals[local_index], local_ref->second);
                        if (runtime.is_callable(callable_value)) {
                            callable_target = std::move(callable_value);
                        }
                    }
                }
                if (!callable_target.has_value() && runtime.has_global(instruction.operands[1]) &&
                    runtime.is_callable(runtime.get_global(instruction.operands[1]))) {
                    callable_target = runtime.get_global(instruction.operands[1]);
                }

                // Instance method dispatch: `receiver.Method(...)` where `receiver` is a plain
                // local/global holding a class instance (an Object with __class), not a callable
                // bound via ADDRESSOF (that's the callable_target branch below). The compile-time-
                // baked "receiver.Method" text can't be resolved statically the way "GUI.Text"-
                // style namespaced host calls can: ArcoBASIC is dynamically typed, so the same
                // call site can see different runtime types across calls (polymorphism through
                // EXTENDS). Always re-resolves against the receiver's actual __class, walking
                // class_parents for inherited/overridden methods, rather than being cached like
                // the branches below -- same reasoning as why callable_target is left uncached.
                bool dispatched_instance_method = false;
                if (!callable_target.has_value()) {
                    const auto dot = instruction.operands[1].find('.');
                    if (dot != std::string::npos) {
                        const std::string receiver_name = instruction.operands[1].substr(0, dot);
                        const std::string method_name = instruction.operands[1].substr(dot + 1);
                        std::optional<Value> receiver_value;
                        const auto receiver_local = function.local_refs_by_base.find(receiver_name);
                        if (receiver_local != function.local_refs_by_base.end()) {
                            const std::size_t local_index = local_index_from_ref(receiver_local->second);
                            if (local_index < frame.locals.size() && frame.locals[local_index].kind != BytecodeSlot::Kind::Undefined) {
                                receiver_value = slot_value(frame.locals[local_index], receiver_local->second);
                            }
                        } else if (runtime.has_global(receiver_name)) {
                            receiver_value = runtime.get_global(receiver_name);
                        }
                        if (receiver_value.has_value() && receiver_value->is_object()) {
                            const auto& receiver_object = receiver_value->as_object();
                            const auto class_field = receiver_object.find("__class");
                            if (class_field != receiver_object.end()) {
                                std::string class_name = class_field->second.to_string();
                                const BytecodeFunction* method_function = nullptr;
                                while (!class_name.empty()) {
                                    method_function = find_function(module, class_name + "." + method_name);
                                    if (method_function) break;
                                    const auto parent = module.class_parents.find(class_name);
                                    class_name = parent == module.class_parents.end() ? std::string() : parent->second;
                                }
                                if (method_function) {
                                    args.insert(args.begin(), *receiver_value);
                                    set_temp_at(0, execute_function(module, *method_function, runtime, args, count_instructions));
                                    dispatched_instance_method = true;
                                }
                            }
                        }
                    }
                }

                if (dispatched_instance_method) {
                    // handled above
                } else if (callable_target.has_value()) {
                    const Value callable = *callable_target;
                    const CallableDescriptor descriptor = runtime.callable_descriptor(callable);
                    std::string resolved_name = descriptor.name;
                    if (descriptor.receiver.has_value()) {
                        const std::string method = resolved_name.substr(resolved_name.rfind('.') + 1);
                        resolved_name = descriptor.receiver->get_property("__class").to_string() + "." + method;
                        args.insert(args.begin(), *descriptor.receiver);
                    }
                    if (const BytecodeFunction* user_function = find_function(module, resolved_name)) {
                        set_temp_at(0, execute_function(module, *user_function, runtime, args, count_instructions));
                    } else {
                        set_temp_at(0, runtime.call_callable(callable,
                            descriptor.receiver.has_value() ? std::vector<Value>(args.begin() + 1, args.end()) : args));
                    }
                    // A callable bound through a local/global variable is left uncached: the
                    // variable's value can legitimately differ across calls to this same site.
                } else if (const BytecodeFunction* user_function = find_function(module, instruction.operands[1])) {
                    set_temp_at(0, execute_function(module, *user_function, runtime, args, count_instructions));
                    if (local_ref == function.local_refs_by_base.end()) {
                        // No local in this function can ever shadow this name (locals are fixed
                        // at compile time), so this call site's target is stable going forward.
                        instruction.call_site_resolution = CallSiteResolution::UserFunction;
                        instruction.call_site_function = user_function;
                    }
                } else {
                    set_temp_at(0, call_host(args));
                    if (local_ref == function.local_refs_by_base.end()) {
                        instruction.call_site_resolution = CallSiteResolution::HostFunction;
                    }
                }
                break;
            }
            case BytecodeOp::CallRuntime: {
                auto& args = instruction.call_args_scratch;
                args.clear();
                args.reserve(instruction.operands.size() > 1 ? instruction.operands.size() - 1 : 0);
                for (std::size_t i = 1; i < instruction.operands.size(); ++i) {
                    args.push_back(value_at(i));
                }
                if (instruction.operands[0] == "Runtime.Print") {
                    if (!args.empty()) {
                        runtime.output() << args[0].to_string();
                    }
                    runtime.output() << '\n';
                } else if (instruction.prepared_call_key.empty()) {
                    (void)runtime.call_host_function(instruction.operands[0], args);
                } else {
                    (void)runtime.call_host_function_prepared(instruction.prepared_call_key, args);
                }
                break;
            }
            case BytecodeOp::Array: {
                Value::Array values;
                values.reserve(instruction.operands.size() > 1 ? instruction.operands.size() - 1 : 0);
                for (std::size_t i = 1; i < instruction.operands.size(); ++i) {
                    values.push_back(value_at(i));
                }
                set_temp_at(0, Value(std::move(values)));
                break;
            }
            case BytecodeOp::Tuple: {
                Value::Array values;
                values.reserve(instruction.operands.size() > 1 ? instruction.operands.size() - 1 : 0);
                for (std::size_t i = 1; i < instruction.operands.size(); ++i) {
                    values.push_back(value_at(i));
                }
                set_temp_at(0, Value::tuple(std::move(values)));
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
                set_temp_at(0, Value(std::move(values)));
                break;
            }
            case BytecodeOp::Index:
            case BytecodeOp::IndexLocalConst:
                set_temp_at(0, index_value(value_at(1), value_at(2)));
                break;
            case BytecodeOp::Slice: {
                if (instruction.operands.size() != 5) throw std::runtime_error("SLICE expects five operands");
                const Value first = value_at(2);
                const Value last = value_at(3);
                const Value stride = value_at(4);
                const auto start = first.is_null() ? std::nullopt
                    : std::optional<long long>(exact_slice_integer(first.as_number(), "start"));
                const auto end = last.is_null() ? std::nullopt
                    : std::optional<long long>(exact_slice_integer(last.as_number(), "end"));
                const long long step = stride.is_null() ? 1 : exact_slice_integer(stride.as_number(), "step");
                set_temp_at(0, slice_value(value_at(1), start, end, step));
                break;
            }
            case BytecodeOp::Copy:
                if (instruction.operands.size() != 2) throw std::runtime_error("COPY expects two operands");
                set_temp_at(0, shallow_copy_value(value_at(1)));
                break;
            case BytecodeOp::Destructure: {
                if (instruction.operands.size() < 2) throw std::runtime_error("DESTRUCTURE expects a source and targets");
                const Value source = value_at(0);
                if (!source.is_array() && !source.is_tuple()) {
                    throw std::runtime_error("destructuring expects an array or tuple with arity " +
                                             std::to_string(instruction.operands.size() - 1));
                }
                const auto& elements = source.is_tuple() ? source.as_tuple() : source.as_array();
                if (elements.size() != instruction.operands.size() - 1) {
                    throw std::runtime_error("destructuring arity mismatch: expected " +
                        std::to_string(instruction.operands.size() - 1) + ", received " + std::to_string(elements.size()));
                }
                const Value::Array captured(elements.begin(), elements.end());
                for (std::size_t i = 1; i < instruction.operands.size(); ++i) {
                    set_local(function, frame, local_index_at(i), captured[i - 1], instruction.operands[i]);
                }
                break;
            }
            case BytecodeOp::AddressOf: {
                if (instruction.operands.size() != 2) throw std::runtime_error("ADDRESSOF expects a result and name");
                const std::string name = instruction.operands[1];
                const auto dot = name.find('.');
                if (dot != std::string::npos && runtime.has_global(name.substr(0, dot))) {
                    set_temp_at(0, runtime.make_callable(name, runtime.get_global(name.substr(0, dot)), false));
                } else {
                    if (!find_function(module, name) && !runtime.has_function(name)) {
                        throw std::runtime_error("ADDRESSOF cannot resolve callable: " + name);
                    }
                    set_temp_at(0, runtime.make_callable(name, std::nullopt, false));
                }
                break;
            }
            case BytecodeOp::StoreConst:
                if (instruction.operands.size() != 2) throw std::runtime_error("STORE_CONST expects local and constant operands");
                set_local_slot(function, frame, local_index_at(0), slot_at(1), instruction.operands[0]);
                break;
            case BytecodeOp::BinaryLocalLocal: {
                if (instruction.operands.size() != 4) throw std::runtime_error("BINARY_LOCAL_LOCAL expects local, op, local, local");
                const auto left = number_at(2);
                const auto right = number_at(3);
                if (left.has_value() && right.has_value() && instruction.prepared_numeric_op != NumericOp::Unknown) {
                    if (numeric_op_is_comparison(instruction.prepared_numeric_op)) {
                        BytecodeSlot result;
                        result.kind = BytecodeSlot::Kind::Boolean;
                        result.boolean = eval_numeric_comparison(instruction.prepared_numeric_op, *left, *right);
                        set_local_slot(function, frame, local_index_at(0), std::move(result), instruction.operands[0]);
                    } else {
                        BytecodeSlot result;
                        result.kind = BytecodeSlot::Kind::Number;
                        result.number = eval_numeric_arithmetic(instruction.prepared_numeric_op, *left, *right);
                        set_local_slot(function, frame, local_index_at(0), std::move(result), instruction.operands[0]);
                    }
                    break;
                }
                set_local(function, frame, local_index_at(0), eval_binary(instruction.operands[1], value_at(2), value_at(3)), instruction.operands[0]);
                break;
            }
            case BytecodeOp::BinaryLocalConst: {
                if (instruction.operands.size() != 4) throw std::runtime_error("BINARY_LOCAL_CONST expects local, op, local, constant");
                const auto left = number_at(2);
                const auto right = number_at(3);
                if (left.has_value() && right.has_value() && instruction.prepared_numeric_op != NumericOp::Unknown) {
                    BytecodeSlot result;
                    if (numeric_op_is_comparison(instruction.prepared_numeric_op)) {
                        result.kind = BytecodeSlot::Kind::Boolean;
                        result.boolean = eval_numeric_comparison(instruction.prepared_numeric_op, *left, *right);
                    } else {
                        result.kind = BytecodeSlot::Kind::Number;
                        result.number = eval_numeric_arithmetic(instruction.prepared_numeric_op, *left, *right);
                    }
                    set_local_slot(function, frame, local_index_at(0), std::move(result), instruction.operands[0]);
                    break;
                }
                set_local(function, frame, local_index_at(0), eval_binary(instruction.operands[1], value_at(2), value_at(3)), instruction.operands[0]);
                break;
            }
            case BytecodeOp::BranchLocalLocal: {
                if (instruction.operands.size() != 5) throw std::runtime_error("BRANCH_LOCAL_LOCAL expects op, local, local, true, false");
                const auto left = number_at(1);
                const auto right = number_at(2);
                bool condition = false;
                if (left.has_value() && right.has_value() && instruction.prepared_numeric_op != NumericOp::Unknown) {
                    condition = eval_numeric_comparison(instruction.prepared_numeric_op, *left, *right);
                } else {
                    condition = eval_binary(instruction.operands[0], value_at(1), value_at(2)).truthy();
                }
                if (instruction.prepared_targets.size() >= 2) {
                    cursor = condition ? instruction.prepared_targets[0] : instruction.prepared_targets[1];
                } else {
                    cursor = condition ? jump_to(instruction.operands[3]) : jump_to(instruction.operands[4]);
                }
                break;
            }
            case BytecodeOp::Jump:
                cursor = instruction.prepared_targets.empty() ? jump_to(instruction.operands.front()) : instruction.prepared_targets.front();
                break;
            case BytecodeOp::Branch:
                if (instruction.prepared_targets.size() >= 2) {
                    cursor = slot_truthy(slot_at(0), instruction.operands[0]) ? instruction.prepared_targets[0] : instruction.prepared_targets[1];
                } else {
                    cursor = slot_truthy(slot_at(0), instruction.operands[0]) ? jump_to(instruction.operands[1]) : jump_to(instruction.operands[2]);
                }
                break;
            case BytecodeOp::TryBegin:
                try_stack.push_back(TryHandler{
                    instruction.prepared_targets.empty() ? jump_to(instruction.operands.front()) : instruction.prepared_targets.front(),
                    instruction.operands.size() > 1 ? instruction.operands[1] : ""});
                break;
            case BytecodeOp::TryEnd:
                if (!try_stack.empty()) {
                    try_stack.pop_back();
                }
                break;
            case BytecodeOp::Throw: {
                if (instruction.operands.size() != 1) throw std::runtime_error("THROW expects one value");
                const Value message = value_at(0);
                if (!message.is_string()) {
                    std::string type = "Object";
                    if (message.is_null()) type = "Null";
                    else if (message.is_bool()) type = "Boolean";
                    else if (message.is_number()) type = "Number";
                    else if (message.is_array()) type = "Array";
                    else if (message.is_handle()) type = message.as_handle().type;
                    throw std::runtime_error("THROW message must be String; received " + type);
                }
                throw UserError(message.to_string(), current_source_line, 1);
            }
            case BytecodeOp::DeclareFunction:
            case BytecodeOp::DeclareClass:
            case BytecodeOp::DeclareInterface:
                break;
            case BytecodeOp::Return:
                return instruction.operands.size() > 1 ? value_at(1) : Value();
            case BytecodeOp::Unsupported:
                if (!instruction.operands.empty() &&
                    (instruction.operands.back().rfind("PORT.", 0) == 0 || instruction.operands.back() == "CPU.Pause")) {
                    throw std::runtime_error(instruction.operands.back() + " is available only on a freestanding target with port-I/O support");
                }
                if (!instruction.operands.empty() &&
                    (instruction.operands.back().rfind("MEMORY.", 0) == 0 || instruction.operands.back().rfind("ADDRESS.", 0) == 0 || instruction.operands.back().rfind("CPU.", 0) == 0)) {
                    throw std::runtime_error(instruction.operands.back() + " is available only on a freestanding target with memory support");
                }
                throw std::runtime_error("cannot execute unsupported bytecode instruction" +
                                         (instruction.operands.empty() ? std::string() : ": " + instruction.operands.back()));
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
                object["Type"] = dynamic_cast<const UserError*>(&error) ? "UserError" : "RuntimeError";
                const Value error_value(std::move(object));
                bool stored_local = false;
                for (std::size_t local_index = 0; local_index < function.locals.size(); ++local_index) {
                    if (function.locals[local_index] == handler.error_name) {
                        set_local(function, frame, local_index, error_value, "L" + std::to_string(local_index));
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

Value execute_bytecode(BytecodeModule& module, Runtime& runtime, bool count_instructions = true) {
    // Args (command-line arguments the running program sees, e.g. examples/arconote.abas's
    // `IF LEN(Args) > 0 THEN initial_path = Args[0]`) previously existed only as a global arcosh
    // set up for shell-invoked scripts (src/shell/arcosh.cpp) -- run_bytecode/run_bytecode_binary/
    // compile_run never set it at all, so merely reading Args crashed ("undefined bytecode local:
    // Args") outside the shell, in every hosted and native-capsule execution path. Callers that
    // have real argv (see run_bytecode_binary below) set Args before calling here; this is just
    // the universal fallback so an unset Args is an empty array, never undefined.
    if (!runtime.has_global("Args")) {
        runtime.set_global("Args", Value(Value::Array{}));
    }
    if (module.constant_values.size() != module.constants.size() || module.function_indices.empty()) {
        prepare_bytecode_module(module);
    }
    if (module.functions.empty()) {
        throw std::runtime_error("bytecode module has no functions");
    }
    const BytecodeFunction* main = find_function(module, "Main");
    if (!main) {
        throw std::runtime_error("bytecode module has no Main function");
    }
    for (const auto& function : module.functions) {
        if (function.name == "Main") continue;
        const BytecodeFunction* callable_function = &function;
        runtime.register_function(function.name, [&module, &runtime, callable_function, count_instructions](const std::vector<Value>& args) {
            return execute_function(module, *callable_function, runtime, args, count_instructions);
        });
    }
    runtime.prepare_execution(module.instruction_limit);
    return execute_function(module, *main, runtime, {}, count_instructions);
}

std::filesystem::path source_root_path() {
#ifdef ARCO_SOURCE_ROOT
    return std::filesystem::path(ARCO_SOURCE_ROOT);
#else
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
#endif
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

// Reads an executable target's own link.txt and returns everything from the first ArcoBASIC
// static library onward (relativized against build_dir), i.e. exactly the flags/libraries a
// native capsule needs to reproduce that target's link behavior. Shared by the full path (reads
// ArcoFission's own link line) and the lean "core" path (reads a throwaway probe executable's).
std::vector<std::string> native_link_dependencies_from(const std::filesystem::path& build_dir,
                                                        const std::filesystem::path& link_txt_relative,
                                                        const std::vector<std::string>& start_markers) {
    std::ifstream input(build_dir / link_txt_relative);
    std::string line;
    if (!std::getline(input, line)) {
        return {};
    }

    const auto words = split_shell_like(line);
    std::vector<std::string> deps;
    bool reached_arco_libraries = false;
    for (const auto& word : words) {
        if (!reached_arco_libraries) {
            const std::string filename = std::filesystem::path(word).filename().string();
            if (std::find(start_markers.begin(), start_markers.end(), filename) != start_markers.end()) {
                reached_arco_libraries = true;
            } else {
                continue;
            }
        }
        if (!word.empty() && word[0] != '-') {
            const std::filesystem::path dependency(word);
            if (!dependency.is_absolute()) {
                deps.push_back((build_dir / dependency).lexically_normal().string());
                continue;
            }
        }
        deps.push_back(word);
    }
    return deps;
}

std::vector<std::string> native_link_dependencies(const std::filesystem::path& build_dir) {
    return native_link_dependencies_from(build_dir, std::filesystem::path("CMakeFiles") / "ArcoFission.dir" / "link.txt",
        {"libarco.a", "libarco_compiler.a"});
}

// Only present when the build tree opted in with `cmake --build . --target
// ArcoFissionCapsuleCoreProbe` (see CMakeLists.txt); empty otherwise, which callers treat as
// "the lean core libraries aren't built here yet".
std::vector<std::string> native_core_link_dependencies(const std::filesystem::path& build_dir) {
    return native_link_dependencies_from(build_dir,
        std::filesystem::path("CMakeFiles") / "ArcoFissionCapsuleCoreProbe.dir" / "link.txt", {"libarco_compiler_core.a"});
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

// True for a PE32+ image whose optional header reports the x86-64 machine type -- enough to
// confirm the cross-compiler actually produced a Windows x86-64 executable rather than, say,
// silently falling back to a host binary. Doesn't attempt full PE validation.
// True for a file starting with WebAssembly's magic number + version 1 -- enough to confirm
// Emscripten actually produced a wasm binary rather than, say, silently failing to write one.
// Doesn't attempt full module validation.
bool is_wasm_file(const std::filesystem::path& path) {
    unsigned char header[8] = {};
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(header), sizeof(header));
    return input.gcount() == static_cast<std::streamsize>(sizeof(header)) &&
           header[0] == 0x00 && header[1] == 'a' && header[2] == 's' && header[3] == 'm' &&
           header[4] == 0x01 && header[5] == 0x00 && header[6] == 0x00 && header[7] == 0x00;
}

bool is_pe64_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    unsigned char dos_header[64] = {};
    input.read(reinterpret_cast<char*>(dos_header), sizeof(dos_header));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(dos_header)) || dos_header[0] != 'M' || dos_header[1] != 'Z') {
        return false;
    }
    const std::uint32_t pe_offset = static_cast<std::uint32_t>(dos_header[60]) | (static_cast<std::uint32_t>(dos_header[61]) << 8) |
        (static_cast<std::uint32_t>(dos_header[62]) << 16) | (static_cast<std::uint32_t>(dos_header[63]) << 24);
    input.seekg(pe_offset);
    unsigned char pe_header[6] = {};
    input.read(reinterpret_cast<char*>(pe_header), sizeof(pe_header));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(pe_header))) {
        return false;
    }
    const std::uint16_t machine = static_cast<std::uint16_t>(pe_header[4]) | (static_cast<std::uint16_t>(pe_header[5]) << 8);
    return pe_header[0] == 'P' && pe_header[1] == 'E' && pe_header[2] == 0 && pe_header[3] == 0 && machine == 0x8664;
}
#endif

// Shared by every native capsule target: a tiny launcher that embeds the compiled bytecode as a
// byte string and hands it to the same hosted VM (run_bytecode_binary) the capsule is linked
// against. What differs per target is only how that launcher gets compiled and linked.
std::string native_launcher_source(const std::string& bytecode_binary, std::optional<std::size_t> instruction_limit_override) {
    std::ostringstream out;
    out << "#include \"arco/fission.hpp\"\n"
        << "#include <iostream>\n"
        << "#include <vector>\n"
        << "\n"
        << "int main(int argc, char** argv) {\n"
        << "    const std::string bytecode(" << cpp_string_literal(bytecode_binary) << ", "
        << bytecode_binary.size() << ");\n"
        << "    std::vector<std::string> script_args;\n"
        << "    for (int i = 1; i < argc; ++i) script_args.emplace_back(argv[i]);\n"
        << "    const auto result = arco::fission::run_bytecode_binary(bytecode, ";
    if (instruction_limit_override.has_value()) {
        out << "std::optional<std::size_t>(" << *instruction_limit_override << ")";
    } else {
        out << "std::nullopt";
    }
    out << ", script_args);\n"
        << "    if (!result.ok) {\n"
        << "        std::cerr << result.error << '\\n';\n"
        << "        return 1;\n"
        << "    }\n"
        << "    std::cout << result.output;\n"
        << "    return 0;\n"
        << "}\n";
    return out.str();
}

#if defined(__linux__)
// Locates a mingw-w64-targeted build tree (arco_runtime/arco_compiler/arcology_os built with
// cmake/toolchains/mingw-w64-x86_64.cmake) so the Windows capsule target can link against it.
// Not auto-discovered from the running ArcoFission's own build tree the way the Linux path is,
// since that tree was built for the host, not for Windows -- callers must point at one via
// ARCOFISSION_WINDOWS_TOOLCHAIN_DIR.
std::optional<std::filesystem::path> windows_toolchain_build_dir() {
    const char* env = std::getenv("ARCOFISSION_WINDOWS_TOOLCHAIN_DIR");
    if (!env || !*env) {
        return std::nullopt;
    }
    return std::filesystem::path(env);
}

Result build_native_windows_bytecode(const std::string& bytecode_binary, const std::string& output_path,
                                     std::optional<std::size_t> instruction_limit_override) {
    try {
        const auto windows_build_dir = windows_toolchain_build_dir();
        if (!windows_build_dir.has_value()) {
            return {false, "",
                "Windows capsule builds need a mingw-w64-targeted build tree (configure one with "
                "cmake/toolchains/mingw-w64-x86_64.cmake and build the arco_compiler target), then point "
                "ARCOFISSION_WINDOWS_TOOLCHAIN_DIR at it"};
        }
        const std::filesystem::path source_root = source_root_path();
        const std::filesystem::path compiler_lib = *windows_build_dir / "libarco_compiler.a";
        const std::filesystem::path runtime_lib = *windows_build_dir / "libarco_runtime.a";
        const std::filesystem::path arcology_lib = *windows_build_dir / "arcology-os" / "libarcology_os.a";
        for (const auto& lib : {compiler_lib, runtime_lib, arcology_lib}) {
            if (!std::filesystem::exists(lib)) {
                return {false, "", "missing " + lib.string() +
                    " -- build the arco_compiler target in the mingw-w64 tree ARCOFISSION_WINDOWS_TOOLCHAIN_DIR points at"};
            }
        }

        const std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() /
                                              ("arcofission-native-windows-" + std::to_string(static_cast<long long>(::getpid())));
        std::filesystem::create_directories(tmp_dir);
        const std::filesystem::path launcher = tmp_dir / "launcher.cpp";
        {
            std::ofstream out(launcher);
            if (!out) {
                return {false, "", "could not write native launcher source"};
            }
            out << native_launcher_source(bytecode_binary, instruction_limit_override);
        }

        const char* env_cross_cxx = std::getenv("ARCOFISSION_WINDOWS_CXX");
        const std::string compiler = env_cross_cxx && *env_cross_cxx ? env_cross_cxx : "x86_64-w64-mingw32-g++";

        std::vector<std::string> args{
            compiler,
            "-std=c++17",
            "-O2",
            "-static",
            "-static-libgcc",
            "-static-libstdc++",
            launcher.string(),
            "-o",
            output_path,
            "-I" + (source_root / "include").string(),
            compiler_lib.string(),
            runtime_lib.string(),
            arcology_lib.string(),
            "-lws2_32",
        };

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
            return {false, "", "could not launch the mingw-w64 cross-compiler"};
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            return {false, "", "mingw-w64 cross-compiler failed while building the Windows capsule"};
        }
        if (!is_pe64_file(output_path)) {
            return {false, "", "cross-compiler did not produce a PE32+ x86-64 executable"};
        }

        std::ostringstream message;
        message << "SOURCE ACCEPTED\n";
        message << "STRUCTURE ASSEMBLED\n";
        message << "BYTECODE EMBEDDED\n";
        message << "PE32+ WRITTEN " << output_path << "\n";
        return {true, message.str(), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}
#endif

#if defined(__linux__)
// Locates an Emscripten-targeted build tree (arco_runtime/arco_compiler/arcology_os configured
// with `emcmake cmake -S . -B <dir>` and built, see arcoflow/README.md or docs/) so the web
// capsule target can link against it. Not auto-discovered the way the same-host native path is,
// since that tree was built for the browser, not for this host -- callers must point at one via
// ARCOFISSION_WEB_TOOLCHAIN_DIR, same shape as the Windows cross-build's
// ARCOFISSION_WINDOWS_TOOLCHAIN_DIR above.
std::optional<std::filesystem::path> web_toolchain_build_dir() {
    const char* env = std::getenv("ARCOFISSION_WEB_TOOLCHAIN_DIR");
    if (!env || !*env) {
        return std::nullopt;
    }
    return std::filesystem::path(env);
}

Result build_web_bytecode(const std::string& bytecode_binary, const std::string& output_path,
                          std::optional<std::size_t> instruction_limit_override) {
    try {
        const auto web_build_dir = web_toolchain_build_dir();
        if (!web_build_dir.has_value()) {
            return {false, "",
                "Web capsule builds need an Emscripten-targeted build tree (configure one with "
                "`emcmake cmake -S . -B <dir> -DARCO_ENABLE_GUI=ON` and build the arco_compiler "
                "target), then point ARCOFISSION_WEB_TOOLCHAIN_DIR at it"};
        }
        const std::filesystem::path source_root = source_root_path();
        const std::filesystem::path compiler_lib = *web_build_dir / "libarco_compiler.a";
        const std::filesystem::path runtime_lib = *web_build_dir / "libarco_runtime.a";
        const std::filesystem::path arcology_lib = *web_build_dir / "arcology-os" / "libarcology_os.a";
        for (const auto& lib : {compiler_lib, runtime_lib, arcology_lib}) {
            if (!std::filesystem::exists(lib)) {
                return {false, "", "missing " + lib.string() +
                    " -- build the arco_compiler target in the Emscripten tree ARCOFISSION_WEB_TOOLCHAIN_DIR points at"};
            }
        }

        const std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() /
                                              ("arcofission-native-web-" + std::to_string(static_cast<long long>(::getpid())));
        std::filesystem::create_directories(tmp_dir);
        const std::filesystem::path launcher = tmp_dir / "launcher.cpp";
        {
            std::ofstream out(launcher);
            if (!out) {
                return {false, "", "could not write web launcher source"};
            }
            out << native_launcher_source(bytecode_binary, instruction_limit_override);
        }

        const char* env_cross_cxx = std::getenv("ARCOFISSION_WEB_CXX");
        const std::string compiler = env_cross_cxx && *env_cross_cxx ? env_cross_cxx : "em++";

        // em++ picks its output shape (bare .wasm+.js, or a full .html shell too) from -o's
        // extension; default to the full page if the caller asked for neither, since a lone
        // .wasm with no way to load it isn't a usable deliverable by itself.
        std::string output = output_path;
        const std::string ext = std::filesystem::path(output).extension().string();
        if (ext != ".html" && ext != ".js" && ext != ".wasm") {
            output += ".html";
        }

        std::vector<std::string> args{
            compiler,
            "-std=c++17",
            "-O2",
            "-fexceptions",
            "-sASYNCIFY",
            "-sALLOW_MEMORY_GROWTH=1",
            // Module.ccall/cwrap aren't exported by default -- src/gui/canvas_backend.cpp's DOM
            // event listeners use Module.ccall to marshal string arguments (key names, typed
            // text) back into the wasm module. Without this, `Module.ccall` is simply undefined
            // and every keyboard/text event is silently dropped the moment a listener tries to
            // call it (mouse/resize events, which pass only numbers through the low-level
            // Module._arco_gui_push_* form, are unaffected).
            "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap",
            launcher.string(),
            "-o",
            output,
            "-I" + (source_root / "include").string(),
            compiler_lib.string(),
            runtime_lib.string(),
            arcology_lib.string(),
        };

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
            return {false, "", "could not launch the Emscripten compiler (em++)"};
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            return {false, "", "Emscripten compiler failed while building the web capsule"};
        }
        const std::filesystem::path wasm_output = std::filesystem::path(output).replace_extension(".wasm");
        if (!is_wasm_file(wasm_output)) {
            return {false, "", "Emscripten compiler did not produce a valid wasm module"};
        }

        std::ostringstream message;
        message << "SOURCE ACCEPTED\n";
        message << "STRUCTURE ASSEMBLED\n";
        message << "BYTECODE EMBEDDED\n";
        message << "WEB CAPSULE WRITTEN " << output << "\n";
        return {true, message.str(), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}
#endif

// True if a program calls a real GUI.* function -- one that would actually reach the stub
// backend's `unsupported()` (see src/gui/stub_backend.cpp) if linked without the GTK/GLFW
// backend. GUI.Available and GUI.Backend are excluded since the stub answers those directly
// (false / "none") rather than throwing, which is exactly how well-behaved ArcoBASIC programs are
// meant to probe for a GUI backend before using one (see examples/gui_cube.abas). Network.* needs
// no such carve-out: every Network.* function already degrades gracefully without libcurl (see
// the #else branch of http_request in runtime.cpp), so it's never a reason to prefer the full
// runtime. Scans every instruction operand rather than only call-name positions, which is
// simpler and only risks an occasional unnecessary "full" link (e.g. a string literal that
// happens to start with "gui." for unrelated reasons), never an incorrect "lean" one.
bool bytecode_needs_full_runtime(const BytecodeModule& module) {
    for (const auto& function : module.functions) {
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto& operand : instruction.operands) {
                    if (operand.size() < 4) continue;
                    if (std::tolower(static_cast<unsigned char>(operand[0])) != 'g' ||
                        std::tolower(static_cast<unsigned char>(operand[1])) != 'u' ||
                        std::tolower(static_cast<unsigned char>(operand[2])) != 'i' || operand[3] != '.') {
                        continue;
                    }
                    const std::string key = lowered_call_key(operand);
                    if (key != "gui.available" && key != "gui.backend") {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// CMake generates every configured target's link.txt at "Generate" time, before anything is
// actually built -- so a non-empty native_core_link_dependencies() result only means the
// ArcoFissionCapsuleCoreProbe target is *known*, not that `cmake --build . --target
// ArcoFissionCapsuleCoreProbe` has ever been run. Confirm the .a files it names actually exist on
// disk before trusting it.
bool link_dependencies_resolve(const std::vector<std::string>& deps) {
    for (const auto& dep : deps) {
        if (dep.size() > 2 && dep.compare(dep.size() - 2, 2, ".a") == 0 && !std::filesystem::exists(dep)) {
            return false;
        }
    }
    return true;
}

Result build_native_bytecode(const std::string& bytecode_binary, const std::string& output_path,
                             std::optional<std::size_t> instruction_limit_override = std::nullopt,
                             bool prefer_lean_runtime = false) {
#if defined(__linux__)
    try {
        const std::filesystem::path source_root = source_root_path();
        const std::filesystem::path build_dir = current_executable_dir();
        bool used_lean_runtime = false;
        auto link_dependencies = prefer_lean_runtime ? native_core_link_dependencies(build_dir) : std::vector<std::string>();
        if (!link_dependencies.empty() && link_dependencies_resolve(link_dependencies)) {
            used_lean_runtime = true;
        } else {
            link_dependencies = native_link_dependencies(build_dir);
        }
        if (link_dependencies.empty()) {
            return {false, "", "native build needs the ArcoBASIC libraries beside ArcoFission; run from a CMake build tree"};
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
            out << native_launcher_source(bytecode_binary, instruction_limit_override);
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
        };
        args.insert(args.end(), link_dependencies.begin(), link_dependencies.end());

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
        if (prefer_lean_runtime && !used_lean_runtime) {
            message << "LEAN RUNTIME UNAVAILABLE (run `cmake --build . --target ArcoFissionCapsuleCoreProbe` "
                       "in the build tree to enable it) -- linked the full runtime instead\n";
        } else if (used_lean_runtime) {
            message << "LEAN RUNTIME LINKED (no GUI backend, no libcurl)\n";
        }
        message << "ELF64 WRITTEN " << output_path << "\n";
        return {true, message.str(), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
#else
    (void)bytecode_binary;
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

        return {true, render_amir(build_amir(statements, source_name, runtime.compile_metadata().instruction_limit)), ""};
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

        return {true, render_calling_convention(build_amir(statements, source_name, runtime.compile_metadata().instruction_limit)), ""};
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

        const AmirModule amir = build_amir(statements, source_name, runtime.compile_metadata().instruction_limit);
        if (!amir.diagnostics.empty()) return {false, "", amir.diagnostics.front()};
        const auto codegen = generate_x86_64_program(amir, entry_function);
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

        const AmirModule amir = build_amir(statements, source_name, runtime.compile_metadata().instruction_limit);
        if (!amir.diagnostics.empty()) return {false, "", amir.diagnostics.front()};
        const auto codegen = generate_x86_64_program(amir, entry_function);
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

        const AmirModule amir = build_amir(statements, source_name, runtime.compile_metadata().instruction_limit);
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

Result run_bytecode(const std::string& bytecode, std::optional<std::size_t> instruction_limit_override) {
    try {
        Runtime runtime;
        runtime.set_instruction_limit_policy(true);
        runtime.set_instruction_limit_override(instruction_limit_override);
        std::ostringstream output;
        runtime.set_output(output);
        auto module = parse_bytecode(bytecode);
        prepare_bytecode_module(module);
        const bool count_instructions = !(instruction_limit_override.has_value() && *instruction_limit_override == 0);
        (void)execute_bytecode(module, runtime, count_instructions);
        return {true, output.str(), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result run_bytecode_binary(const std::string& bytecode, std::optional<std::size_t> instruction_limit_override,
                           const std::vector<std::string>& script_args) {
    try {
        Runtime runtime;
        runtime.set_instruction_limit_policy(true);
        runtime.set_instruction_limit_override(instruction_limit_override);
        std::ostringstream output;
        runtime.set_output(output);
        Value::Array args_array;
        args_array.reserve(script_args.size());
        for (const auto& arg : script_args) args_array.emplace_back(arg);
        runtime.set_global("Args", Value(std::move(args_array)));
        auto module = parse_binary_bytecode(bytecode);
        prepare_bytecode_module(module);
        const bool count_instructions = !(instruction_limit_override.has_value() && *instruction_limit_override == 0);
        (void)execute_bytecode(module, runtime, count_instructions);
        return {true, output.str(), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result run_bytecode_file(const std::string& path, std::optional<std::size_t> instruction_limit_override) {
    try {
        return run_bytecode(read_file(path), instruction_limit_override);
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result compile_run(const std::string& source, const std::string& source_name,
                   std::optional<std::size_t> instruction_limit_override) {
    try {
        Runtime preprocess_runtime;
        const std::string processed = preprocess_runtime.preprocess_source(source);
        Lexer lexer(processed);
        auto tokens = lexer.scan_tokens();

        Parser parser(tokens, preprocess_runtime.compile_metadata().runtime_mode == "NONE");
        auto statements = parser.parse();

        Runtime runtime;
        runtime.set_instruction_limit_policy(true);
        runtime.set_instruction_limit_override(instruction_limit_override);
        std::ostringstream output;
        runtime.set_output(output);
        auto module = build_bytecode(build_amir(
            statements, source_name, preprocess_runtime.compile_metadata().instruction_limit));
        prepare_bytecode_module(module);
        const bool count_instructions = !(instruction_limit_override.has_value() && *instruction_limit_override == 0);
        (void)execute_bytecode(module, runtime, count_instructions);
        return {true, output.str(), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result compile_run_file(const std::string& path, std::optional<std::size_t> instruction_limit_override) {
    try {
        return compile_run(read_file(path), path, instruction_limit_override);
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result build_native_file(const std::string& path, const std::string& output_path,
                         std::optional<std::size_t> instruction_limit_override, const std::string& target) {
    try {
        Runtime runtime;
        const std::string processed = runtime.preprocess_source(read_file(path));
        Lexer lexer(processed);
        auto tokens = lexer.scan_tokens();

        Parser parser(tokens, runtime.compile_metadata().runtime_mode == "NONE");
        auto statements = parser.parse();

        const AmirModule amir = build_amir(statements, path, runtime.compile_metadata().instruction_limit);
        const BytecodeModule module = build_bytecode(amir);
        const std::string bytecode = render_binary_bytecode(module);
        if (target == "windows-x86_64" || target == "windows-x86-64") {
#if defined(__linux__)
            return build_native_windows_bytecode(bytecode, output_path, instruction_limit_override);
#else
            return {false, "", "Windows capsule cross-builds are only supported from a Linux host"};
#endif
        }
        if (target == "web" || target == "wasm" || target == "web-wasm32") {
#if defined(__linux__)
            return build_web_bytecode(bytecode, output_path, instruction_limit_override);
#else
            return {false, "", "Web capsule cross-builds are only supported from a Linux host"};
#endif
        }
        return build_native_bytecode(bytecode, output_path, instruction_limit_override, !bytecode_needs_full_runtime(module));
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

Result reveal_pretty(const std::string& source, const std::string& source_name) {
    try {
        Runtime runtime;
        const std::string processed = runtime.preprocess_source(source);
        Lexer lexer(processed);
        auto tokens = lexer.scan_tokens();

        Parser parser(tokens, runtime.compile_metadata().runtime_mode == "NONE");
        auto statements = parser.parse();
        (void)source_name;

        return {true, pretty_print_canonical(statements), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result reveal_pretty_file(const std::string& path) {
    try {
        return reveal_pretty(read_file(path), path);
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

} // namespace arco::fission
