#include "arco/fission.hpp"

#include "arco/calling_convention.hpp"
#include "arco/jit_x86_64.hpp"
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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sys/mman.h>
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
        // Populated before any lowering happens, top-level declarations only (matching every
        // Draw*/Button/Checkbox-shaped call this exists for -- see function_declarations_'s own
        // comment): a forward call to a FUNCTION declared later in the same file must resolve
        // its defaults exactly like one declared earlier does.
        for (const auto& root : roots_) {
            if (root && root->kind == AstKind::Function) function_declarations_[root->name] = root.get();
        }
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
            if (name == "CPU.READCR0") return "U64";
            if (name == "CPU.READMSR") return "U64";
            if (name == "CPU.READRSP") return "U64";
            if (name == "CPU.READCS") return "U64";
            if (name == "CPU.EXCEPTIONVECTORTABLEBASE") return "U64";
            if (name == "CPU.INTERRUPTPENDINGTABLEBASE") return "U64";
            if (name == "GRAPHICS.CREATESURFACE" || name == "GRAPHICS.PRIMARYSURFACE") return "SURFACE";
            if (name == "GRAPHICS.CREATEWINDOW") return "WINDOW";
            if (name == "GRAPHICS.CREATEIMAGE") return "IMAGE";
            if (name == "FILES.OPEN") return "FILE";
            if (name == "NETWORK.CONNECT") return "SOCKET";
            if (name == "LEN") return "U64";
            if (name == "MID") return "STRING";
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
            if (name == "CPU.READCR0") return "U64";
            if (name == "CPU.READMSR") return "U64";
            if (name == "CPU.READCS") return "U64";
            if (name == "CPU.EXCEPTIONVECTORTABLEBASE") return "U64";
            if (name == "CPU.INTERRUPTPENDINGTABLEBASE") return "U64";
            if (name == "UEFI.GOP.DISCOVER") return "UEFI.GraphicsOutputProtocol";
            if (name == "UEFI.GOP.MODE") return "UEFI.GraphicsOutputMode";
            if (name == "UEFI.GOP.FRAMEBUFFERBASE") return "PHYSICALPTR";
            if (name == "UEFI.GOP.FRAMEBUFFERSIZE") return "U64";
            if (name == "UEFI.GOP.WIDTH" || name == "UEFI.GOP.HEIGHT" || name == "UEFI.GOP.PIXELSPERSCANLINE" || name == "UEFI.GOP.PIXELFORMAT") return "U32";
            if (name == "UEFI.BLOCKIO.DISCOVER") return "UEFI.BlockIoProtocol";
            if (name == "UEFI.BLOCKIO.MEDIAID" || name == "UEFI.BLOCKIO.MEDIAFLAGS" || name == "UEFI.BLOCKIO.BLOCKSIZE") return "U32";
            if (name == "UEFI.BLOCKIO.LASTBLOCK") return "U64";
            if (name == "ADDRESS.PHYSICAL") return "PHYSICALPTR";
            if (name == "ADDRESS.VIRTUAL") return "VIRTUALPTR";
            if (name == "ADDRESS.MMIO") return "MMIOPTR";
            if (name == "ADDRESS.VALUE") return "U64";
            if (name == "ADDRESS.LOCAL") return "PTR";
            if (name == "ADDRESS.OFFSET" || name == "ADDRESS.ALIGNUP" || name == "ADDRESS.ALIGNDOWN") {
                return node.children.empty() ? expected : type_of_expression(*node.children.front(), expected);
            }
            if (name == "MEMORY.MAP") return "VIRTUALPTR";
            if (name == "MEMORY.MAPDEVICE") return "MMIOPTR";
            if (name == "GRAPHICS.PRIMARYSURFACE") return "SURFACE";
            if (name == "GRAPHICS.DESTROYSURFACE") return "BOOL";
            if (name == "MEMORY.READ8") return "U8";
            if (name == "MEMORY.READ16") return "U16";
            if (name == "MEMORY.READ32") return "U32";
            if (name == "MEMORY.READ64") return "U64";
            if (name == "MEMORY.ISALIGNED") return "BOOL";
            if (name == "AEX.INVOKENATIVE0" || name == "AEX.INVOKENATIVE1") return "U64";
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
            case AstKind::DynamicMethodCall: {
                // `f().Method(...)`/`arr[0].Method(...)`/any other method call whose receiver is
                // an arbitrary expression, not a plain (possibly dotted) name -- see
                // DynamicMethodCallExpr's own canonical_ast() comment in parser.cpp. Evaluate the
                // receiver expression, then lower to an ordinary CallValue targeting
                // "<receiver name>.<method>" -- instance-method dispatch (both the bytecode VM's
                // own and this project's native backend's, generate_x86_64_function's
                // Kind::CallValue case) already treats any NAMED local with a stack slot as a valid
                // receiver, so no new dispatch machinery is needed here, only a way to feed it a
                // receiver that isn't already a source-level name.
                //
                // The receiver is stored into a fresh hidden_name() local, NOT used directly as
                // the bare "%tN" temp lower_expression itself returns -- a real bug found by direct
                // testing: bytecode-prep's own operand-reference parser (ref_index/prepare_operand)
                // treats ANY call target beginning with '%' as an ADDRESSOF/CALLABLE-style "call
                // through a bare temp reference" (its OWN, separate dispatch convention, distinct
                // from this project's native backend's own slot_of()-based one) and requires
                // everything after "%t" to be pure digits -- "%t11.Hello" fails to parse there with
                // "invalid bytecode temporary reference", even though this exact backend's own
                // native codegen compiled and ran it correctly on the first try. A hidden_name()
                // local (no '%' prefix, exactly like any other user-declared variable name) routes
                // through bytecode's OTHER, name-based dispatch path instead, which already
                // generalizes to an arbitrary dotted receiver.
                const std::string object_value = lower_expression(function, *node.children[0]);
                const std::string receiver_name = hidden_name("dynrecv");
                current_block(function).instructions.push_back(amir_store(receiver_name, object_value));
                std::vector<std::string> args;
                for (std::size_t i = 1; i < node.children.size(); ++i) {
                    args.push_back(lower_expression(function, *node.children[i]));
                }
                const std::string result = temp();
                current_block(function).instructions.push_back(
                    amir_call_value(result, receiver_name + "." + node.name, std::move(args)));
                return result;
            }
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
                if (name == "CPU.INTERRUPT") {
                    if (node.children.size() != 1 || node.children.front()->kind != AstKind::Literal) {
                        report_integer_error("CPU.Interrupt requires a statically known vector in the initial x86-64 systems target");
                        return lower_fallback(current_block(function), node);
                    }
                    long long vector = 0;
                    try {
                        vector = std::stoll(node.children.front()->text, nullptr, 0);
                    } catch (...) {
                        report_integer_error("CPU.Interrupt vector must be an exact integer literal");
                        return lower_fallback(current_block(function), node);
                    }
                    if (vector < 0 || vector > 255) report_integer_error("CPU.Interrupt vector is outside the supported 0-255 range");
                    current_block(function).instructions.push_back(amir_barrier("INTERRUPT_" + std::to_string(vector)));
                    return temp();
                }
                if (name == "CPU.READCR2" || name == "CPU.READCR3" || name == "CPU.WRITECR3" || name == "CPU.INVALIDATEPAGE" || name == "CPU.LOADGDT" || name == "CPU.LOADIDT" || name == "CPU.LOADTASKREGISTER" || name == "CPU.RELOADCODESEGMENT" || name == "CPU.RELOADSTACKSEGMENT" || name == "CPU.RELOADDATASEGMENTS") {
                    const std::string op = name == "CPU.READCR2" ? "READCR2" : (name == "CPU.READCR3" ? "READCR3" : (name == "CPU.WRITECR3" ? "WRITECR3" : "INVLPG"));
                    const std::string descriptor_op = name == "CPU.LOADGDT" ? "LGDT" : (name == "CPU.LOADIDT" ? "LIDT" : (name == "CPU.LOADTASKREGISTER" ? "LTR" : (name == "CPU.RELOADCODESEGMENT" ? "RELOADCS" : (name == "CPU.RELOADSTACKSEGMENT" ? "RELOADSS" : (name == "CPU.RELOADDATASEGMENTS" ? "RELOADDS" : op)))));
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
                // CR0 and MSR access -- real prerequisites for configuring memory type ranges
                // (write-combining framebuffer performance) without any vendor-specific GPU driver;
                // MTRRs are a standard x86 feature, not Intel- or AMD-specific.
                if (name == "CPU.READCR0") {
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("READCR0", result, {}, "U64", {}));
                    return result;
                }
                if (name == "CPU.WRITECR0") {
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("WRITECR0", "", std::move(args), "", {"U64"}));
                    return result;
                }
                if (name == "CPU.READMSR") {
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("READMSR", result, std::move(args), "U64", {"U32"}));
                    return result;
                }
                if (name == "CPU.WRITEMSR") {
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("WRITEMSR", "", std::move(args), "", {"U32", "U64"}));
                    return result;
                }
                if (name == "CPU.READCS") {
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("READCS", result, {}, "U64", {}));
                    return result;
                }
                if (name == "CPU.EXCEPTIONVECTORTABLEBASE") {
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("EXCEPTIONVECTORTABLEBASE", result, {}, "U64", {}));
                    return result;
                }
                if (name == "CPU.INTERRUPTPENDINGTABLEBASE") {
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("INTERRUPTPENDINGTABLEBASE", result, {}, "U64", {}));
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
                if (name.rfind("UEFI.BLOCKIO.", 0) == 0) {
                    const std::string blockio_op = name.substr(13);
                    const bool discover = blockio_op == "DISCOVER";
                    const bool field = blockio_op == "MEDIAID" || blockio_op == "MEDIAFLAGS" || blockio_op == "BLOCKSIZE" || blockio_op == "LASTBLOCK";
                    if (node.children.size() != 1 || (discover && type_of_expression(*node.children.front()) != "UEFI.SystemTable") || (field && type_of_expression(*node.children.front()) != "UEFI.BlockIoProtocol")) {
                        report_integer_error("UEFI.BLOCKIO." + blockio_op + " received an invalid operand");
                    }
                    const std::string result = temp();
                    std::string target = discover ? "BLOCKIODISCOVER" : "BLOCKIO" + blockio_op;
                    current_block(function).instructions.push_back(amir_memory(target, result, std::move(args), type_of_expression(node), {discover ? "UEFI.SystemTable" : "UEFI.BlockIoProtocol"}));
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
                if (name == "AEX.INVOKENATIVE0" || name == "AEX.InvokeNative0") {
                    if (node.children.size() != 1 || type_of_expression(*node.children.front()) != "MMIOPTR") {
                        report_integer_error("AEX.InvokeNative0 expects one MMIOPTR entry address");
                    }
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("AEXINVOKENATIVE0", result, std::move(args), "U64", {"MMIOPTR"}));
                    return result;
                }
                if (name == "AEX.INVOKENATIVE1" || name == "AEX.InvokeNative1") {
                    if (node.children.size() != 2 || type_of_expression(*node.children[0]) != "MMIOPTR" || type_of_expression(*node.children[1], "U64") != "U64") {
                        report_integer_error("AEX.InvokeNative1 expects an MMIOPTR entry address and one U64 argument");
                    }
                    const std::string result = temp();
                    current_block(function).instructions.push_back(amir_memory("AEXINVOKENATIVE1", result, std::move(args), "U64", {"MMIOPTR", "U64"}));
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

    // True for exactly `ClassName.FieldName` where FieldName is a SHARED field of ClassName --
    // see shared_fields_'s own comment. Not meaningful for longer chains (a SHARED field is never
    // itself an object with further dotted properties in any code this compiler has seen).
    bool is_shared_field_reference(const std::vector<std::string>& parts) const {
        if (parts.size() != 2) return false;
        const auto found = shared_fields_.find(parts[0]);
        return found != shared_fields_.end() && found->second.count(parts[1]) != 0;
    }

    std::string lower_variable(AmirBlock& out, const std::string& name) {
        const auto parts = split_identifier_path(name);
        if (is_shared_field_reference(parts)) {
            // A plain per-function LOAD only ever sees this function's own call-frame-local slot
            // (confirmed with a minimal repro: a SHARED counter incremented through ordinary
            // amir_store/amir_load silently reset to its initial value on every read from a
            // different function than the one that last wrote it -- exactly the same cross-
            // function-visibility gap apply_script_global_scoping() exists to close for ordinary
            // script-scope variables, just needed here too, for a different reason). Route through
            // the same Runtime.GetGlobal/SetGlobal primitive that mechanism already uses, which is
            // backed by Runtime's actual persistent globals_ map, not any one call frame.
            const std::string key = temp();
            out.instructions.push_back(amir_const(key, "\"" + escaped(name) + "\""));
            const std::string result = temp();
            auto instruction = amir_call_value(result, "Runtime.GetGlobal", {key});
            instruction.result_type = types_.count(name) ? types_.at(name) : "U64";
            out.instructions.push_back(std::move(instruction));
            return result;
        }
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
        // Default-parameter support (RFC-0049, "full Linux support" pass -- found genuinely
        // blocking Arconaut, a real program: `Button(window, id, label, x, y, w, h)`, omitting its
        // own trailing r/g/b color parameters, which the FUNCTION declaration itself defaults to
        // 0.13/0.16/0.20 -- this call site previously reached codegen with only 7 of Button's 10
        // declared arguments and failed with a plain arity mismatch, since nothing anywhere in the
        // AMIR pipeline ever filled in an omitted trailing default). AmirFunction::params only
        // ever stores each parameter's STRINGIFIED "name = literal" descriptor (see
        // parameter_text) -- fine for declaring a function's own arity, but this call site needs
        // the default's real VALUE computed as if the caller had written it explicitly, which
        // needs the original AST sub-expression (CanonicalAstParameter::default_value), not a
        // re-parse of that descriptor string. function_declarations_ (populated once up front, see
        // its own comment) is exactly that -- looked up here, at AST-lowering time, rather than in
        // codegen, because lower_expression (the only thing that knows how to turn an arbitrary
        // default expression, not just a literal, into AMIR) only exists at this stage.
        if (node.kind != AstKind::SuperCall) {
            const auto declaration = function_declarations_.find(target);
            if (declaration != function_declarations_.end()) {
                const auto& params = declaration->second->parameters;
                for (std::size_t i = args.size(); i < params.size(); ++i) {
                    // A missing argument with no default is a real arity mismatch -- stop padding
                    // and let the existing "expects N arguments, got M" check downstream (codegen)
                    // report it normally, exactly as it already did before this fix existed.
                    if (!params[i].default_value) break;
                    args.push_back(lower_expression(function, *params[i].default_value));
                }
            }
        }
        const std::string result = temp();
        const std::string upper_target = upper_ascii(target);
        if (upper_target == "CPU.READCR2" || upper_target == "CPU.READCR3") {
            auto instruction = amir_memory(upper_target == "CPU.READCR2" ? "READCR2" : "READCR3", result, {}, "U64", {});
            current_block(function).instructions.push_back(std::move(instruction));
            return result;
        }
        if (upper_target == "CPU.EXCEPTIONVECTORTABLEBASE" || upper_target == "CPU.INTERRUPTPENDINGTABLEBASE") {
            auto instruction = amir_memory(upper_target == "CPU.EXCEPTIONVECTORTABLEBASE" ? "EXCEPTIONVECTORTABLEBASE" : "INTERRUPTPENDINGTABLEBASE", result, {}, "U64", {});
            current_block(function).instructions.push_back(std::move(instruction));
            return result;
        }
        if (upper_target == "CPU.INTERRUPT") {
            if (node.children.size() != 1 || node.children.front()->kind != AstKind::Literal) {
                report_integer_error("CPU.Interrupt requires a statically known vector in the initial x86-64 systems target");
                return result;
            }
            long long vector = 0;
            try {
                vector = std::stoll(node.children.front()->text, nullptr, 0);
            } catch (...) {
                report_integer_error("CPU.Interrupt vector must be an exact integer literal");
                return result;
            }
            if (vector < 0 || vector > 255) report_integer_error("CPU.Interrupt vector is outside the supported 0-255 range");
            current_block(function).instructions.push_back(amir_barrier("INTERRUPT_" + std::to_string(vector)));
            return result;
        }
        if (upper_target == "CPU.WRITECR3" || upper_target == "CPU.INVALIDATEPAGE" || upper_target == "CPU.LOADGDT" || upper_target == "CPU.LOADIDT" || upper_target == "CPU.LOADTASKREGISTER" || upper_target == "CPU.RELOADCODESEGMENT" || upper_target == "CPU.RELOADSTACKSEGMENT" || upper_target == "CPU.RELOADDATASEGMENTS") {
            const std::string op = upper_target == "CPU.WRITECR3" ? "WRITECR3" : (upper_target == "CPU.INVALIDATEPAGE" ? "INVLPG" : (upper_target == "CPU.LOADGDT" ? "LGDT" : (upper_target == "CPU.LOADIDT" ? "LIDT" : (upper_target == "CPU.LOADTASKREGISTER" ? "LTR" : (upper_target == "CPU.RELOADCODESEGMENT" ? "RELOADCS" : (upper_target == "CPU.RELOADSTACKSEGMENT" ? "RELOADSS" : "RELOADDS"))))));
            current_block(function).instructions.push_back(amir_memory(op, "", std::move(args), "", {"U64"}));
            return result;
        }
        AmirInstruction instruction = amir_call_value(result, target, std::move(args));
        if (upper_target == "LEN" || upper_target == "MID") {
            // Freestanding STRING has no substring/length operations at all (LEN/MID were
            // rejected outright as "undeclared function" -- see the generic CallValue error in
            // generate_x86_64_function). Both become real, hand-assembled STRING builtins here --
            // but ONLY when the first argument is confidently STRING. `LEN` in particular is
            // ALREADY legitimately used elsewhere in this same shared AMIR pipeline for arrays,
            // ranges, bitvectors, and objects (the hosted runtime's own LEN, runtime.cpp:2332,
            // and the internal array-length AMIR calls this file itself synthesizes at :1506/
            // :2121) -- those must fall through to the ordinary generic path completely
            // unchanged, not be hard-errored here just because the name matches. A real
            // regression found this way, not assumed: an early version of this special case
            // intercepted every call literally named LEN/MID regardless of argument type, which
            // broke `LEN(someRange)`/`LEN(someBitvector)`/`LEN(someObjectArray)` outright
            // (arcofission_alpha_smoke caught it on the very first full-suite run).
            //
            // type_of_expression alone cannot tell a bare string LITERAL argument from a bare
            // numeric one (a Literal node with no "expected" type hint just echoes that hint
            // back -- see its own Literal case) -- it infers a literal's type from a SIBLING
            // operand or the caller's own expected type, and LEN/MID's lone string argument has
            // neither. Disambiguated instead exactly the way the Const codegen case already
            // does for the same reason: a raw string literal's own AST text still carries its
            // opening quote.
            const auto string_typed = [&](const CanonicalAstNode& arg) -> std::string {
                if (arg.kind == AstKind::Literal && !arg.text.empty() && arg.text.front() == '"') return "STRING";
                return type_of_expression(arg);
            };
            const bool first_arg_is_string = !node.children.empty() && string_typed(*node.children.front()) == "STRING";
            if (!first_arg_is_string) {
                current_block(function).instructions.push_back(std::move(instruction));
                return result;
            }
            const std::size_t expected_args = upper_target == "LEN" ? 1 : 3;
            if (node.children.size() != expected_args) {
                report_integer_error(upper_target + " expects exactly " + std::to_string(expected_args) +
                    (expected_args == 1 ? " argument" : " arguments") + " (got " + std::to_string(node.children.size()) + ")");
                return result;
            }
            instruction.operand_types = {"STRING"};
            if (upper_target == "MID") {
                instruction.operand_types.push_back(type_of_expression(*node.children[1]));
                instruction.operand_types.push_back(type_of_expression(*node.children[2]));
            }
            instruction.result_type = upper_target == "LEN" ? "U64" : "STRING";
            current_block(function).instructions.push_back(std::move(instruction));
            return result;
        }
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
        //
        // The same problem exists for any OTHER parameter, not just SELF: has_parameter() alone
        // only asks "is the receiver some parameter of this function", with no regard for its
        // type, so an entirely ordinary `FUNCTION Foo(actual AS Vec3) ... actual.EqualsApprox(...)`
        // was ALSO wrongly routed into CALL_EXTERNAL merely because `actual` is a parameter --
        // confirmed with a minimal repro (a plain class method call on a typed, non-UEFI
        // parameter), not hypothetical.
        //
        // Narrowing has_parameter() to "is this NOT a CLASS actually declared in this module" is
        // not, by itself, the whole fix: systems_amir_primitives_smoke (a real, existing fixture)
        // deliberately calls through a `handle AS Custom.Opaque` parameter -- an intentionally
        // made-up non-UEFI type name -- and still requires CALL_EXTERNAL ("a call through a
        // parameter is external ... position/parameter-name based [classification], not
        // [dependent on whether the type is] a real bound field of anything", that fixture's own
        // comment). Separately, systems_arco_basic_multi_blockio_discovery_smoke calls through a
        // `LET blockIo AS UEFI.BlockIoProtocol = ...` -- a plain LOCAL variable, never a
        // parameter at all -- so has_parameter() alone (however it's gated) can never cover this
        // case; it needs the independent "is the type UEFI.*-prefixed" signal alongside it.
        //
        // A THIRD case, found the same way (a real repro, not hypothesized): a parameter with NO
        // type annotation at all -- `FUNCTION SpawnAt(newComponent) ... newComponent.SetPosition
        // (...)`, arco3d's own polymorphic-parameter style -- must NOT be treated as external
        // either, even though has_parameter() is still true for it. SELF is exactly this shape
        // (never given an explicit type by this compiler), which is why the ORIGINAL code needed
        // to special-case it by name at all -- generalizing "untyped parameter defaults to
        // ordinary dispatch" below covers SELF too, without a name-based special case. So the
        // has_parameter() fallback only applies when the parameter DOES have a type annotation
        // and that type is not a declared class (Custom.Opaque, UEFI.SystemTable): a completely
        // untyped parameter is presumed an ordinary polymorphic object, matching how the
        // tree-walking interpreter always treats one (it has no such distinction at all).
        //
        // All three real fixtures broke, one at a time, as this heuristic was narrowed further
        // each round -- confirmed by rerunning the full fixture suite after each attempt, not
        // assumed.
        const bool receiver_type_known = types_.count(receiver_name) != 0;
        const bool receiver_is_declared_class = receiver_type_known && module_.class_parents.count(types_.at(receiver_name)) != 0;
        const bool receiver_is_typed_non_class_parameter = receiver_type_known && !receiver_is_declared_class;
        const bool receiver_type_is_external_uefi = receiver_type_known && types_.at(receiver_name).rfind("UEFI.", 0) == 0;
        if (node.kind == AstKind::MethodCall && node.secondary_name != "SELF" &&
            ((has_parameter(function, node.secondary_name) && receiver_is_typed_non_class_parameter) || receiver_type_is_external_uefi)) {
            instruction.kind = AmirInstruction::Kind::CallExternal;
            if (!receiver_name.empty() && receiver_type_known) instruction.operand_types = {types_.at(receiver_name)};
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
        // Comment/no-op statements carry no codegen effect (see the NoOp/Comment case below), but
        // pushing a source-position marker for them unconditionally -- as every other statement
        // kind gets -- can strand that marker instruction after a *preceding* statement's terminal
        // instruction (RETURN/CpuHaltForever/...) whenever the comment or blank line is the last
        // thing in a block. A same-line trailing comment on a function's final RETURN is the
        // ordinary way to hit this: `RETURN <expr>   ' comment` parses as two sibling statements
        // (Return, then Comment, confirmed via `reveal ... at AST`), and the marker this function
        // used to push for that second, effect-free statement became the block's new last
        // instruction -- non-terminal, sitting right after a genuinely terminal RETURN -- which
        // validate_module correctly flags as "instruction after terminal operation", and which
        // then failed EFI BUILD FAILED outright, not just a diagnostic. Skipping the marker for
        // these two kinds specifically (their line-label handling above is unaffected) means a
        // block's last *emitted* instruction always still corresponds to its last statement with
        // real effect, exactly as every other statement already guarantees.
        if (node.kind == AstKind::NoOp || node.kind == AstKind::Comment) {
            return;
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
                else if (node.name == "CPU.Wbinvd") current_block(function).instructions.push_back(amir_barrier("WBINVD"));
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
        // See lower_variable()'s identical Runtime.SetGlobal/GetGlobal reasoning: a SHARED field
        // needs a real cross-function-call store, not an indexed store into ClassName (which
        // isn't a local holding an object at all) and not a plain per-frame STORE either (which
        // the very next call into a *different* function would never see).
        if (is_shared_field_reference(parts)) {
            const std::string value = lower_expression(function, *node.children.back(), declared_type);
            const std::string key = temp();
            current_block(function).instructions.push_back(amir_const(key, "\"" + escaped(node.name) + "\""));
            current_block(function).instructions.push_back(amir_call_value(temp(), "Runtime.SetGlobal", {key, value}));
            return;
        }
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

        // SHARED fields (RFC docs/classes.md) have no per-instance storage -- this class's own
        // .__new below deliberately skips them -- so back each one with Runtime's persistent
        // globals_ map (via Runtime.SetGlobal, the same primitive apply_script_global_scoping()
        // already uses for cross-function script-scope variables -- see lower_variable()'s own
        // comment for why a plain per-frame amir_store isn't enough here), keyed by
        // "ClassName.FieldName" and initialized once, right here, before any method body
        // (including this class's own) can reference it. Must run before the method-compiling
        // loop below records shared_fields_ for lower_variable()/lower_assignment() to find.
        for (const auto& field : ast_group(node, "fields")) {
            if (field->kind != AstKind::ClassField || !field->flag) continue;
            shared_fields_[node.name].insert(field->name);
            std::string field_value;
            if (!field->children.empty() && field->children[0]) {
                field_value = lower_expression(owner, *field->children[0]);
            } else {
                field_value = temp();
                current_block(owner).instructions.push_back(amir_const(field_value, "nothing"));
            }
            const std::string key = temp();
            current_block(owner).instructions.push_back(amir_const(key, "\"" + escaped(node.name + "." + field->name) + "\""));
            current_block(owner).instructions.push_back(amir_call_value(temp(), "Runtime.SetGlobal", {key, field_value}));
        }

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
            // lower_function() (plain FUNCTIONs) already does this; class methods never did,
            // which meant a typed method parameter's type was silently indistinguishable from an
            // untyped one downstream -- e.g. `FUNCTION ApplyToPoint(localPoint AS Vec3) ...
            // localPoint.ScaledBy(...)` -- confirmed with a minimal repro to be the reason a
            // perfectly ordinary method call on a class-typed method parameter was still being
            // misclassified as CALL_EXTERNAL (the has_parameter() fallback in lower_call() only
            // stops firing once a parameter's type is actually known).
            const auto saved_types = types_;
            types_.clear();
            for (const auto& param : method->parameters) {
                if (!param.type_name.empty()) types_[param.name] = param.type_name;
            }
            current_block_ = 0;
            loop_stack_.clear();
            lower_statements(function, ast_group(*method, "body"));
            ensure_terminated(function, current_block_, "VALUE", "nothing");
            current_block_ = saved_block;
            loop_stack_ = saved_loops;
            types_ = saved_types;
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
    // Class name -> its own SHARED field names (RFC docs/classes.md's "SHARED for class-level
    // members"). A SHARED field has no per-instance storage at all (lower_class's own .__new
    // deliberately skips it), so `ClassName.FieldName` is instead backed by one global bytecode
    // local named exactly that -- see lower_variable()/lower_assignment()'s shared-field checks.
    std::unordered_map<std::string, std::unordered_set<std::string>> shared_fields_;
    // Top-level FUNCTION name -> its own declaration node, populated once up front in build()
    // before any lowering happens (see its own comment) -- lets lower_call resolve a callee's
    // REAL default-value expressions (CanonicalAstParameter::default_value, a full sub-tree, not
    // just the stringified "name = literal" descriptor AmirFunction::params stores) regardless of
    // whether the callee is declared earlier or later in the source file than the call site.
    std::unordered_map<std::string, const CanonicalAstNode*> function_declarations_;
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
            else if (instruction.target == "BLOCKIODISCOVER") out << "UEFI.BLOCKIO.DISCOVER";
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

// --- Hot-numeric-loop JIT ------------------------------------------------------------------
//
// Targets exactly one shape: a canonical counting FOR loop (`FOR i = start TO end [STEP step]`)
// whose step is a compile-time-constant number and whose body is a straight-line sequence of
// BINARY_LOCAL_LOCAL arithmetic (no calls, no objects/arrays/strings, no nested control flow).
// That's a deliberately narrow slice of the language -- not a general JIT -- chosen because it's
// the case that's both mechanically tractable to detect and verify correct, and the one real
// compute-heavy ArcoBASIC code actually hits. See try_detect_jit_loop()'s own comment for exactly
// how that shape is recognized structurally (not by trusting today's block-naming convention).
//
// A detected loop is compiled once, lazily, on its first execution (see execute_function()'s own
// hook for this), to native x86-64 via arco::fission::jit::JitAssembler, operating directly on the
// SAME BytecodeSlot memory the interpreter itself uses (through the compile-time-constant
// kBytecodeSlotNumberOffset/kBytecodeSlotSize below) -- no separate native calling convention or
// argument marshalling needed, and no divergence risk from copying values in or out. Every
// execution still re-checks, at runtime, that every local the loop touches is currently
// BytecodeSlot::Kind::Number (ArcoBASIC is dynamically typed; nothing here can assume that stays
// true just because it was true when the loop was first detected) -- any mismatch falls back to
// ordinary interpretation for that one execution, never to a wrong answer.
struct JitLoopBodyOp {
    NumericOp op = NumericOp::Unknown; // always an arithmetic op (Add/Sub/Mul/Div) -- see the
                                        // detector's own rejection of anything else, including Mod
                                        // (no native SSE2 remainder instruction to translate it to)
    std::size_t dest_local = 0;
    std::size_t left_local = 0;
    // The right operand is either another local (BINARY_LOCAL_LOCAL) or a compile-time-constant
    // literal (BINARY_LOCAL_CONST, e.g. `x = x * 2`) -- right_local is only meaningful when
    // right_is_const is false.
    bool right_is_const = false;
    std::size_t right_local = 0;
    double right_constant = 0.0;
};

// mmap'd RWX-never (W^X: allocated RW, written, then mprotect'd to RX before ever running)
// executable memory holding one compiled loop's native code. shared_ptr rather than a bare
// owning pointer so JitLoopPlan (and BytecodeFunction, which holds a vector of these) stay
// freely copyable -- copies just share the same mapping, munmap'd exactly once when the last
// reference goes away.
struct JitExecutableMemory {
    std::shared_ptr<void> base;
    std::size_t size = 0;
};

struct JitLoopPlan {
    std::size_t counter_local = 0;
    std::size_t end_local = 0;
    std::size_t step_local = 0;
    double step_constant = 0.0;
    bool ascending = true; // step_constant > 0; step_constant == 0 is rejected by the detector
    std::vector<JitLoopBodyOp> body_ops;
    std::size_t cond_block = 0; // where execute_function's hook fires -- see its own comment
    std::size_t end_block = 0;  // where the interpreter resumes once the native loop returns
    // Every distinct local index the loop touches (counter/end/step plus every body op's
    // dest/left/right) -- the full set the runtime type-guard checks before taking the JIT path.
    std::vector<std::size_t> touched_locals;
    // Populated by compile_jit_loop() the first time this plan is actually run; entry stays
    // nullptr (and execute_function falls back to ordinary interpretation) until then, and again
    // forever after if compilation itself fails for any reason (out of executable memory, etc.).
    // mutable: compiled lazily, on first execution, through the const BytecodeFunction& reference
    // execute_function receives -- the same inline-cache pattern BytecodeInstruction's own
    // call_site_resolution/call_site_function already use for exactly the same reason.
    mutable JitExecutableMemory executable;
    mutable void (*entry)(BytecodeSlot*) = nullptr;
};

// Set ARCOFISSION_JIT_DIAG=1 in the environment before running a capsule to have it print one
// line to stderr every time the hot-numeric-loop JIT actually dispatches native code for a loop
// (which function, which cond_block) -- the direct answer to "is my loop actually getting
// JIT'd?", alongside ARCOFISSION_DEBUG's per-function timing and ARCOFISSION_NO_JIT's full
// disable switch. Checked once and cached, same pattern as ARCOFISSION_NO_JIT just below in
// execute_function: negligible cost even when set (one dispatch is already a whole compiled loop
// running), and no cost at all for the vastly more common case of it being unset.
bool jit_diagnostics_enabled() {
    static const bool enabled = [] {
        const char* env = std::getenv("ARCOFISSION_JIT_DIAG");
        return env != nullptr && env[0] != '\0' && std::string(env) != "0";
    }();
    return enabled;
}

struct BytecodeFunction {
    std::string name;
    std::string return_type;
    std::vector<std::string> params;
    std::vector<std::string> locals;
    std::vector<BytecodeBlock> blocks;
    std::unordered_map<std::string, BytecodeCursor> targets;
    std::unordered_map<std::string, std::string> local_refs_by_base;
    std::vector<std::string> param_local_refs;
    // Parallel to param_local_refs, holding the same local index each entry's "L<n>" text already
    // encodes -- precomputed once here instead of re-parsing that string (find_first_not_of +
    // substr + stoul, a heap allocation) on every single call's argument-binding loop, which is
    // exactly what happened before this field existed. SIZE_MAX marks "no local" (an empty
    // param_local_refs[i] entry), matching that slot's own empty-string sentinel.
    std::vector<std::size_t> param_local_indices;
    std::vector<std::optional<Value>> param_defaults;
    // Parallel to param_defaults, for a default value that ISN'T a simple literal -- e.g.
    // `CONSTRUCTOR(position AS Vec3 = Vec3(0, 0, 0))` -- which parse_constant_value can't produce
    // a Value for at prepare time (there is no live object to construct yet). Resolved once here,
    // at prepare_bytecode_module time, to the target function plus its (literal) argument values;
    // evaluated fresh via execute_function() every time the default actually fires, rather than
    // computed once and shared, so mutating one caller's default-constructed instance can never
    // alias another's. See resolve_param_default_call().
    std::vector<std::optional<std::pair<const BytecodeFunction*, std::vector<Value>>>> param_default_calls;
    std::size_t temp_count = 0;
    // Populated once by prepare_bytecode_module() via try_detect_jit_loop(); see JitLoopPlan's
    // own comment. Empty for the overwhelming majority of functions (anything with no eligible
    // FOR loop), in which case execute_function's hook is a single empty-vector check per call.
    std::vector<JitLoopPlan> jit_loops;
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
    if (as_pos == std::string::npos) return "";
    // A parameter descriptor is "name[ AS Type][ = defaultExpr]" (parameter_text's own format,
    // see its comment) -- a real bug caught by direct testing, once a real default-parameter call
    // site actually needed lowering (RFC-0049, "full Linux support" pass): a TYPED parameter that
    // ALSO has a default value (e.g. `greeting AS STRING = "Hello"`) returned "STRING = \"Hello\""
    // here instead of plain "STRING", since nothing trimmed the trailing default-value text off
    // before this point. Every caller of this function compares its result against an exact type
    // name ("STRING", "BOOL", ...), so the untrimmed string matched none of them and silently fell
    // through to the wrong load/store path -- a real segfault (`Greet("World")` on a function with
    // exactly this shape), not just a wrong answer. Safe to cut at the first " = ": a real type
    // name never contains one (parameter_text always renders the default with exactly this
    // " = " separator, including the padding spaces on both sides).
    const std::string type_part = declared_parameter.substr(as_pos + 4);
    const auto default_pos = type_part.find(" = ");
    return default_pos == std::string::npos ? type_part : type_part.substr(0, default_pos);
}

// Bounded, best-effort static type inference for one AMIR value, used only by the System V
// backend's Kind::Call("Runtime.Print") case (PRINT of anything more than a bare string literal --
// see that case's own comment) to decide which native runtime shim entry point to call. AMIR
// itself carries no type on a Call instruction's own operand (amir_call never sets one -- PRINT's
// only lowering, lower_print, just wraps whatever lower_expression produced), so this walks back to
// how that value was actually built instead.
//
// ArcoBASIC is dynamically typed and this analysis is purely static (no execution, no real Value
// tracking) -- it can only answer what a STRAIGHT-LINE, non-reassigned-with-a-different-type
// program actually does, which is what every test program in this backend's own scope is by
// construction. A local reassigned to a different type in different branches would confuse this
// (it picks whichever STORE happens to be found), a real but disclosed limitation, not attempted to
// be solved with real dynamic tracking here.
// Boxed: already an ArcoValue* -- an array, an object, or the result of indexing into either (see
// the Array/Object/Index cases below and RFC-0049 Section 4, Phase 2). Never needs constructing
// via arco_value_new_number/_string_utf16/_bool the way String/Number/Bool do; PRINT and
// array/object element insertion both just use the pointer directly.
enum class HostedValueKind { Unknown, String, Number, Bool, Boxed };

// True for the Binary/Unary operator spellings whose result is always a BOOL (0/1, stored via the
// same GPR path as the freestanding integer BOOL type -- never a double bit pattern), regardless of
// what type the operands were.
bool hosted_operator_is_boolean(const std::string& op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" || op == "!";
}

HostedValueKind infer_hosted_value_kind(const AmirModule& module, const AmirFunction& function,
                                         const std::string& name, int depth);

// A declared function's own straight-line return kind, found by taking its first RETURN
// instruction's operand and running the same inference used everywhere else. Shared by the
// Kind::Call general-call branch (to know whether the callee's result comes back in XMM0 or RAX)
// and by infer_hosted_value_kind's own Call handling just below (so a value that passed through a
// function call chases the same rule a directly returned value would).
HostedValueKind infer_function_return_kind(const AmirModule& module, const AmirFunction& callee, int depth = 0) {
    for (const auto& block : callee.blocks) {
        for (const auto& instruction : block.instructions) {
            // A literal "nothing" RETURN (a function/method with no explicit RETURN statement --
            // see ensure_terminated, and lower_class's own method-compiling loop, which every
            // non-Init method without an explicit RETURN falls through to) is NOT skipped here:
            // infer_hosted_value_kind now classifies a bare "nothing" as Boxed/null directly (its
            // own top-of-function special case), matching the Kind::Return codegen's own System-V
            // convention of writing a real null pointer to RAX for exactly this case. An earlier
            // version of this function explicitly excluded "nothing" here, treating it as "no
            // return value to classify" -- which meant a class's own Init method (whose body never
            // has an explicit RETURN, e.g. `FUNCTION Init(x, y): SELF.X = x: SELF.Y = y`) always
            // fell through to Unknown and failed to compile at its OWN call site, a real bug caught
            // by direct testing of a real constructor with arguments.
            if (instruction.kind == AmirInstruction::Kind::Return && !instruction.operands.empty()) {
                return infer_hosted_value_kind(module, callee, instruction.operands.front(), depth);
            }
        }
    }
    return HostedValueKind::Unknown;
}

// Phase 2 classes (RFC-0049 Section 4): given a class name and a method name, walks
// module.class_parents (set once per DECLARE_CLASS by lower_class -- a class's own entry maps to
// its immediate parent's name, or "" for none) starting at class_name itself, looking for the
// first ancestor with a function literally named "<ancestor>.<method_name>" declared. This is
// EXACTLY the algorithm the bytecode VM's own instance-method dispatch (BytecodeOp::CallValue,
// "Instance method dispatch" comment) already performs at runtime -- replicated here at COMPILE
// TIME instead, since the class hierarchy itself is static (module.class_parents never changes at
// runtime); only the receiver's actual runtime `"__class"` string content has to be checked at
// runtime (see the Kind::CallValue case's own instance-dispatch codegen).
std::optional<std::string> resolve_class_method(const AmirModule& module, std::string class_name, const std::string& method_name) {
    while (!class_name.empty()) {
        const std::string candidate = class_name + "." + method_name;
        for (const auto& function : module.functions) {
            if (function.name == candidate) return candidate;
        }
        const auto parent = module.class_parents.find(class_name);
        if (parent == module.class_parents.end() || parent->second.empty()) break;
        class_name = parent->second;
    }
    return std::nullopt;
}

// Classifies a bare local/parameter NAME (not a %tN temp) -- Kind::StoreIndex's own `.target`
// field is exactly this shape (lower_assignment passes the raw variable name directly, unlike
// Kind::Index's `.target`, which is always a Load-derived temp -- see the StoreIndex/Index cases'
// own comments below), and Kind::Load's own handling (below) needs the identical walk for the
// variable it loads from. Finds the LAST Store to that name and infers from its value; if never
// STOREd, falls back to the declared PARAMETER type (a parameter is spilled directly by
// generate_x86_64_function's own prologue with no AMIR Store instruction of its own, so e.g. a
// function that just forwards a parameter straight to PRINT would otherwise be unclassifiable
// even though its declared type is right there on the signature). An explicit type annotation
// this backend doesn't otherwise recognize (anything other than STRING/BOOL/empty/NUMBER -- e.g.
// `AS ARRAY`/`AS OBJECT`) is treated as Boxed: the only other parameters this backend's System V
// path ever sees are pointers of one kind or another (arrays, objects, or a freestanding type that
// never actually reaches this hosted analysis), never a second raw-double representation.
HostedValueKind infer_local_kind(const AmirModule& module, const AmirFunction& function,
                                  const std::string& local_name, int depth) {
    // SELF (Phase 2 classes) is architecturally always a class instance (a Boxed Object), never a
    // hosted-number -- checked by name, same as param_is_hosted_number's own identical special
    // case in generate_x86_64_function, since SELF genuinely never carries a type annotation to
    // check instead.
    if (local_name == "SELF") return HostedValueKind::Boxed;
    std::string last_store_source;
    for (const auto& search_block : function.blocks) {
        for (const auto& candidate : search_block.instructions) {
            if (candidate.kind == AmirInstruction::Kind::Store && candidate.target == local_name &&
                !candidate.operands.empty()) {
                last_store_source = candidate.operands.front();
            }
        }
    }
    if (!last_store_source.empty()) return infer_hosted_value_kind(module, function, last_store_source, depth);
    for (const auto& param : function.params) {
        if (bare_parameter_name(param) != local_name) continue;
        const std::string param_type = declared_parameter_type(param);
        // A STRING-typed parameter is classified Boxed, NOT String -- the CALL-SITE marshaling
        // (generate_x86_64_function's own two call-argument marshaling loops) now ALWAYS boxes a
        // STRING-typed argument via box_operand_into_rax before the call, specifically so this
        // answer can be a single, unconditional Boxed regardless of which representation the
        // caller's own argument expression started as (a raw literal vs an already-boxed
        // concatenation/host-call/field result -- see that marshaling code's own comment for the
        // real bug this fixes: a plain bit-copy left the callee treating an ArcoValueBox POINTER
        // as if it were itself a raw UTF-16 buffer address, producing Unicode mojibake). Before
        // this, a STRING-typed parameter was classified String (the raw, un-owned representation)
        // unconditionally -- correct only when EVERY call site happens to pass a provably-string
        // literal, which a dynamically-typed language can never guarantee in general.
        if (param_type == "STRING") return HostedValueKind::Boxed;
        if (param_type == "BOOL") return HostedValueKind::Bool;
        if (param_type.empty() || param_type == "NUMBER") return HostedValueKind::Number;
        return HostedValueKind::Boxed;
    }
    // TRY/CATCH's own error variable (`CATCH e`) is bound directly by Kind::TryBegin's own codegen
    // (arco_try_error), never through an ordinary Kind::Store -- see that case's own comment. Its
    // value is always a {Message, Type} object.
    for (const auto& search_block : function.blocks) {
        for (const auto& candidate : search_block.instructions) {
            if (candidate.kind == AmirInstruction::Kind::TryBegin && !candidate.operands.empty() &&
                candidate.operands.front() == local_name) {
                return HostedValueKind::Boxed;
            }
        }
    }
    // Reference-counted lifetime tracking (RFC-0049, "full Linux support" pass): `local_name` might
    // not be a Store-target/parameter/TryBegin-error-var at all -- it might be a compiler-generated
    // TEMP that is DIRECTLY the result of a Boxed-producing instruction (Object/Array/CallValue/
    // Index/AddressOf/Load/...), e.g. `%t7` in `%t7 := OBJECT ...; STORE obj, %t7`. This is exactly
    // the shape store_result's own tracks_lifetime check runs into for that temp's OWN store (the
    // construction instruction's result slot, BEFORE the separate STORE into the user-named local
    // even happens) -- infer_hosted_value_kind already knows how to classify a name via exactly that
    // "is it directly the result of instruction X" scan (see its own Kind::Object/Array/Index/
    // AddressOf/CallValue cases), so delegating to it here closes the gap rather than duplicating
    // that scan. A REAL bug this exact gap caused, found via measured linear-in-iteration-count RSS
    // growth (not assumed from reading the code): without this, `%t7`'s own construction-time
    // reference was NEVER released on the next iteration's overwrite (store_result's tracks_lifetime
    // was false, since infer_local_kind fell through to Unknown for a name that's never a Store
    // target) -- retained once by the following `STORE obj, %t7` (correctly balanced by `obj`'s own
    // release chain) but leaking the ORIGINAL construction-time reference every single iteration.
    return infer_hosted_value_kind(module, function, local_name, depth + 1);
}

HostedValueKind infer_hosted_value_kind(const AmirModule& module, const AmirFunction& function,
                                         const std::string& name, int depth = 0) {
    if (depth > 8) return HostedValueKind::Unknown; // guards against a pathological reference cycle
    // "nothing" (ArcoBASIC's null sentinel) is a bare literal, never a %tN temp reference -- e.g. a
    // synthesized `RETURN VALUE nothing` (a method/function with no explicit RETURN -- see
    // ensure_terminated) or an omitted slice bound. Represented as a plain null pointer, the same
    // storage shape as any other Boxed value (see the Const case's own comment in
    // generate_x86_64_function) -- checked here directly since there's no defining instruction to
    // scan for when the name itself IS the literal.
    if (name == "nothing") return HostedValueKind::Boxed;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.kind == AmirInstruction::Kind::Const && instruction.result == name) {
                const std::string& text_operand = instruction.operands.empty() ? std::string() : instruction.operands.front();
                if (!text_operand.empty() && text_operand.front() == '"') return HostedValueKind::String;
                // A literal TRUE/FALSE keyword (LiteralExpr's own source_text, "true"/"false"
                // lowercase, matching the source spelling -- see parser.cpp's primary()) is a BOOL,
                // never a number: this was a real bug caught by direct testing (`"y=" + TRUE`
                // tried to numerically-add "y=" to std::stod("true"), which throws, instead of
                // string-concatenating "y=" with "TRUE").
                if (text_operand == "true" || text_operand == "false") return HostedValueKind::Bool;
                // "nothing" (build_amir's own synthesized null, e.g. an uninitialized typed class
                // field with no default) and an explicit NULL/null keyword in source (parser.cpp's
                // NullKeyword, source_text "null") are both ArcoBASIC's null sentinel, represented
                // as a plain null pointer -- the same storage shape as any other Boxed value, see
                // the Const case's own comment in generate_x86_64_function.
                if (text_operand == "nothing" || text_operand == "null") return HostedValueKind::Boxed;
                return HostedValueKind::Number;
            }
            if ((instruction.kind == AmirInstruction::Kind::Binary || instruction.kind == AmirInstruction::Kind::Unary) &&
                instruction.result == name) {
                // Only ever reaches here having already been accepted by this same function's own
                // Binary/Unary hosted-number codegen (anything else fails to compile before a
                // Call/PRINT referencing its result could ever be reached) -- see is_hosted_number.
                // A comparison (or logical NOT) result is a BOOL, stored via the ordinary GPR
                // store_result path (see Binary's own is_comparison branch), never a double bit
                // pattern -- printing it through the double path would reinterpret a raw 0/1 as an
                // almost-zero denormal double, a real bug caught by direct testing before this
                // distinction existed.
                if (hosted_operator_is_boolean(instruction.target)) return HostedValueKind::Bool;
                // "&"/"|"/"^" between two Bool operands (see the Binary case's own identical
                // bool-coercing codegen and its much larger comment) still produces a NUMBER, not a
                // "real" Bool -- confirmed directly against `compile-run`'s own ground truth:
                // `PRINT TRUE AND FALSE` prints "0", never "FALSE", because eval_binary's own
                // value_to_int coerces EVERY operand (bools included) to an integer first,
                // regardless of the operands' own original kind. No special case is needed here at
                // all -- this function's own final `return HostedValueKind::Number;` below already
                // gives the right answer for this shape, this comment just documents why nothing
                // more specific is checked for it here the way "+" and the comparison operators are.
                // "+" is special: it's string concatenation (already a Boxed ArcoValue*, produced
                // by arco_value_concat -- see the Binary case's own codegen) whenever at least one
                // operand is PROVABLY a string, OR either operand is merely, AMBIGUOUSLY Boxed (an
                // array element/object field/generic host-function result whose real runtime kind
                // this static analysis simply cannot know -- see HostedValueKind::Boxed). Mirrors
                // the codegen's own identical dual gate exactly (see the Binary case's own two "+"
                // special cases just above its ordinary two-hosted-number arithmetic branch): a
                // PROVABLY-string operand routes straight to concatenation with no runtime check at
                // all, while an ambiguously-Boxed operand (not provably String on EITHER side) goes
                // through a real RUNTIME `is_string` check and boxes ITS result too regardless of
                // which way that check goes -- so in BOTH cases this instruction's actual result is
                // a real ArcoValue* pointer, never a raw double, and this function's own answer must
                // say so.
                //
                // This used to special-case Boxed OUT specifically for `SELF.Value + 1`-shaped
                // arithmetic (a Boxed NUMBER field): back when the codegen's own ambiguous-Boxed
                // path didn't exist, that expression went through the ordinary numeric-addition
                // branch and stored a raw double, so classifying it Boxed here would have made the
                // CALLER misread that raw double's own bit pattern as a pointer -- a real
                // segmentation fault, the reason this exclusion existed in the first place. Now that
                // the codegen ALWAYS boxes an ambiguous "+" result (even the purely-numeric-at-
                // runtime case, via arco_value_new_number -- see that branch's own comment on why:
                // a SINGLE static answer must stay valid under EVERY runtime path this one
                // instruction can take), that original bug can no longer occur, and excluding Boxed
                // here instead reintroduced the SAME failure class the other way around: `quoted =
                // quoted + ch` (both operands ambiguously Boxed, ALWAYS real strings at runtime) hit
                // Arconaut's own ShellQuote loop, classified this result Number, and the caller
                // (another loop iteration's own use of `quoted`) fed a real ArcoValue* pointer into
                // `arco_value_as_number` -- a clean panic ("value is not a number"), not silent
                // corruption, but still a real crash reachable from ordinary use of a real program.
                if (instruction.kind == AmirInstruction::Kind::Binary && instruction.target == "+" &&
                    instruction.operands.size() == 2) {
                    const auto plausibly_string_or_ambiguously_boxed = [&](const std::string& operand) {
                        const HostedValueKind kind = infer_hosted_value_kind(module, function, operand, depth + 1);
                        return kind == HostedValueKind::String || kind == HostedValueKind::Boxed;
                    };
                    if (plausibly_string_or_ambiguously_boxed(instruction.operands[0]) ||
                        plausibly_string_or_ambiguously_boxed(instruction.operands[1])) {
                        return HostedValueKind::Boxed;
                    }
                }
                return HostedValueKind::Number;
            }
            // ADDRESSOF's own result is always already-Boxed (a string naming the callable -- see
            // that case's own codegen comment), never a raw pointer needing construction.
            if (instruction.kind == AmirInstruction::Kind::AddressOf && instruction.result == name) {
                return HostedValueKind::Boxed;
            }
            if (instruction.kind == AmirInstruction::Kind::Load && instruction.result == name) {
                return infer_local_kind(module, function, instruction.target, depth + 1);
            }
            if ((instruction.kind == AmirInstruction::Kind::Array || instruction.kind == AmirInstruction::Kind::Object ||
                 instruction.kind == AmirInstruction::Kind::Tuple) && instruction.result == name) {
                return HostedValueKind::Boxed;
            }
            if (instruction.kind == AmirInstruction::Kind::Index && instruction.result == name) {
                // arco_value_array_get/arco_value_object_get (see the Index case below) always
                // return an already-boxed ArcoValue* -- true regardless of what element/field type
                // is actually inside at runtime, since ArcoBASIC arrays/objects are heterogeneous
                // and this is a purely static analysis.
                return HostedValueKind::Boxed;
            }
            // A general user-declared function call's result (e.g. `PRINT Foo(5)` -- lower_call
            // lowers an ordinary call through amir_call_value, i.e. Kind::CallValue, not Kind::Call
            // -- see the CallValue case's own comment below): recurse into the callee's own
            // straight-line RETURN the same way Load recurses through a Store, so a value that
            // passed through a function call is no less classifiable than one that passed through
            // a local variable.
            if (instruction.kind == AmirInstruction::Kind::CallValue && instruction.result == name) {
                const AmirFunction* callee = nullptr;
                for (const auto& candidate : module.functions) {
                    if (candidate.name == instruction.target) { callee = &candidate; break; }
                }
                if (callee) return infer_function_return_kind(module, *callee, depth + 1);
                // No user-declared function named this -- LEN (whether on a freestanding STRING or
                // on a Boxed array/object -- see the CallValue case's own two special cases) always
                // returns a hosted-number, so it's classified here directly rather than falling
                // through to Unknown just because there's no module.functions entry to recurse into.
                if (upper_ascii(instruction.target) == "LEN") return HostedValueKind::Number;
                // Runtime.Args() (RFC-0049, "full Linux support" pass) always returns a real,
                // never-null Boxed array (arco_runtime_args) -- classified here directly for the
                // same reason LEN is, just above.
                if (instruction.target == "Runtime.Args") return HostedValueKind::Boxed;
                // GUI.Window/GUI.WindowShaped both return a plain `int` window HANDLE at the C++
                // level (gui::create_window, include/arco/gui.hpp) -- but UNLIKE LEN/Runtime.Args
                // just above, neither has any DEDICATED native codegen of its own: both fall
                // through to the exact same generic host-function bridge (arco_call_host) every
                // other unrecognized host call does, which ALWAYS returns a genuine boxed
                // ArcoValue* (never a raw double), regardless of what the underlying C++ function
                // conceptually returns. An earlier version of this fix special-cased these two
                // names to return Number here (to fix `window` -- Boxed via the fallback just
                // below -- being rejected when passed to an untyped, hosted-number-assumed
                // parameter like DrawIcon's own `window`) -- WRONG, and left uncaught until
                // Arconaut's own GUI code actually RAN natively for the first time (all prior
                // testing was compile-only): claiming Number here while the actual codegen still
                // produces a Boxed pointer is exactly the "classifier and codegen disagree" bug
                // pattern this project's own memory warns about, and it manifested exactly that way
                // -- `PRINT window` printed a denormal garbage double (a pointer's own bit pattern
                // misread as one), and any later `GUI.*(window, ...)` call passed that garbage
                // straight to the real GUI backend ("unknown GUI window: 0"). Reverted to fall
                // through to the ordinary "assume Boxed" answer below (correct, matching what
                // arco_call_host actually produces); the ORIGINAL problem this was trying to solve
                // is fixed the same way SelectDevice/SelectSnapshot's own Boxed-but-provably-
                // numeric `Number(...)` argument was: an explicit `AS NUMBER` annotation on the
                // receiving parameter (see Arconaut's own Draw*/DrawIcon/Hit/FindHover signatures),
                // which correctly unboxes via load_double_operand instead of lying about the
                // producing instruction's own representation.
                // Runtime.GetGlobal (script-scope globals/SHARED class fields -- see the
                // CallValue case's own comment) always returns a Boxed ArcoValue*
                // (arco_global_get), including for a never-set name (a real null pointer, matching
                // Runtime.GetGlobal's own "has_global(name) ? get_global(name) : Value()"
                // convention) -- classified here directly for the same reason LEN is, just above.
                if (instruction.target == "Runtime.GetGlobal") return HostedValueKind::Boxed;
                // Instance method dispatch (`receiver.Method(...)`, or a chained receiver path like
                // `a.b.Method(...)` -- see the Kind::CallValue case's own much larger comment on
                // this exact pattern): no function is ever literally named "<variable>.<Method>",
                // so the direct lookup above always misses for this shape. The method name always
                // comes from the LAST dot (`a.b.c(...)` calls method `c` on receiver path `a.b`,
                // matching parser.cpp's own MethodCallExpr construction), regardless of how many
                // dots precede it -- mirrors the codegen's own resolve_class_method walk to find
                // every class that could supply this method, then uses the FIRST match's own
                // return kind -- a real, disclosed simplification if different candidates somehow
                // return different kinds for the same call site (an unusual, arguably malformed
                // override in the first place), not something this static analysis tries to
                // reconcile further.
                const auto dot = instruction.target.rfind('.');
                if (dot != std::string::npos) {
                    const std::string method_name = instruction.target.substr(dot + 1);
                    for (const auto& entry : module.class_parents) {
                        const auto resolved_name = resolve_class_method(module, entry.first, method_name);
                        if (!resolved_name) continue;
                        for (const auto& resolved_function : module.functions) {
                            if (resolved_function.name == *resolved_name) {
                                return infer_function_return_kind(module, resolved_function, depth + 1);
                            }
                        }
                    }
                }
                // ADDRESSOF/CALLABLE dispatch (see the Kind::CallValue case's own much larger
                // comment on this exact pattern): `instruction.target` is a plain (non-dotted)
                // name that isn't a declared function -- if it's ALSO something this analysis can
                // trace to an ADDRESSOF result (infer_local_kind already classifies that as Boxed,
                // the same way SELF/an untyped-with-explicit-non-hosted-type parameter/etc. all
                // are), this is almost certainly a call through a callable variable, not a host
                // function. Uses the FIRST candidate ADDRESSOF target found anywhere in the module
                // for its return kind -- a real, disclosed simplification (the same one instance
                // dispatch's own classification above already accepts) if the variable could hold
                // callables with genuinely different return kinds across different code paths. A
                // real bug this exact gap caused before it existed: a call through a callable
                // resolving to a Number-returning function was classified Boxed by the fallback
                // below, so PRINT skipped boxing entirely and handed the raw double's own bit
                // pattern to arco_value_print as if it were already a pointer -- a real
                // segmentation fault, not just a wrong answer. A SECOND real bug, found right
                // after fixing the first: with two same-arity candidates in the module (one
                // untyped, assumed-number; one `AS STRING`), this "first match" search picked
                // WHICHEVER ONE WAS DECLARED FIRST IN SOURCE ORDER regardless of which one the
                // callable variable actually held, silently misclassifying a Boxed (String)
                // result as Number when the untyped candidate happened to come first -- the exact
                // same denormal-garbage-double symptom as the first bug, just reached a different
                // way. Fixed by applying the SAME per-argument type-compatibility filter the
                // Kind::CallValue case's own dispatch codegen uses to narrow its candidate list
                // (not just arity): a candidate whose untyped parameter can't accept an argument
                // this analysis already knows is String/Bool/Boxed is skipped here too, so
                // "Square" (untyped) is correctly passed over in favor of "Shout" (AS STRING) when
                // the actual argument is provably a string.
                if (infer_local_kind(module, function, instruction.target, depth + 1) == HostedValueKind::Boxed) {
                    for (const auto& candidate_function : module.functions) {
                        for (const auto& candidate_block : candidate_function.blocks) {
                            for (const auto& candidate_instruction : candidate_block.instructions) {
                                if (candidate_instruction.kind != AmirInstruction::Kind::AddressOf) continue;
                                for (const auto& resolved_function : module.functions) {
                                    if (resolved_function.name != candidate_instruction.target ||
                                        resolved_function.params.size() != instruction.operands.size()) continue;
                                    bool candidate_could_match = true;
                                    for (std::size_t i = 0; candidate_could_match && i < instruction.operands.size(); ++i) {
                                        // An untyped parameter (and not SELF, which is never
                                        // untyped-assumed-number regardless -- see
                                        // param_is_hosted_number's own identical rule in
                                        // generate_x86_64_function) is assumed hosted-number; a
                                        // provably String/Bool/Boxed argument can never satisfy it.
                                        if (bare_parameter_name(resolved_function.params[i]) == "SELF") continue;
                                        if (!declared_parameter_type(resolved_function.params[i]).empty()) continue;
                                        const HostedValueKind argument_kind = infer_hosted_value_kind(module, function, instruction.operands[i], depth + 1);
                                        if (argument_kind == HostedValueKind::String || argument_kind == HostedValueKind::Bool ||
                                            argument_kind == HostedValueKind::Boxed) {
                                            candidate_could_match = false;
                                        }
                                    }
                                    if (!candidate_could_match) continue;
                                    return infer_function_return_kind(module, resolved_function, depth + 1);
                                }
                            }
                        }
                    }
                }
                // Generic host-function bridge fallback (see the Kind::CallValue case's own much
                // larger comment on this exact path): once nothing else above has matched,
                // codegen's own fallback assumes this is a call into arco::Runtime's host-function
                // library, which ALWAYS returns a Boxed ArcoValue* (arco_call_host) -- matched here
                // for consistency with what the codegen actually emits, not a separate guess.
                return HostedValueKind::Boxed;
            }
        }
    }
    return HostedValueKind::Unknown;
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
    // A call to a symbol defined OUTSIDE this generated program entirely -- e.g. the native ArcoSH
    // runtime shim (arco_native_print_utf16 and friends) -- as opposed to InternalCallFixup, which
    // is always another function this same compilation also generated. Unlike internal calls
    // (patched here, once every function's own offset in the combined image is known), external
    // calls are left for the assembler/linker to resolve: render_x86_64_linux_asm splices a real
    // `call <symbol>` mnemonic at this offset instead of the placeholder bytes the encoder emitted,
    // and the system assembler computes the correct rel32 itself once the symbol's real address is
    // known at link time. Only ever populated on the System V (Linux) convention today.
    struct ExternalCallFixup {
        std::size_t disp_field_offset;
        std::string symbol;
    };
    std::vector<ExternalCallFixup> external_calls;
    std::string entry_symbol;
    // The Arco native debugger tooling (ArcoFission build ... --debug): one entry per AMIR
    // instruction, populated only when generate_x86_64_function/_program's own `annotate`
    // parameter is true (never on an ordinary build -- this is pure overhead with no behavioral
    // effect otherwise, so it costs nothing when not asked for). `text_offset` is the byte offset
    // in `text` where THIS instruction's own generated code begins; `comment` is the exact same
    // rendering `reveal amir`'s own render_instruction produces, prefixed with the owning
    // function's name, so a raw crash address (from gdb/AddressSanitizer) maps directly back to
    // the AMIR instruction AND source line responsible -- see render_x86_64_linux_asm, which turns
    // these into `# ...` comment lines in the generated assembly at the matching offset. Built
    // specifically because pure address-to-source correlation, done by hand (grep the raw
    // `.byte`-encoded instruction stream, decode opcodes, count backwards from a known nearby
    // call), was the single most time-consuming part of every real bug this backend's own "full
    // Linux support" pass found by actually running generated code (RFC-0049 Entries 20-21).
    struct InstructionAnnotation {
        std::size_t text_offset;
        std::string comment;
    };
    std::vector<InstructionAnnotation> annotations;
};

// Reserved symbol name for the compiler-synthesized exception-entry table (see below). Not a
// legal ArcoBASIC identifier, so it can never collide with a user-declared function.
constexpr const char* kExceptionVectorTableSymbol = "$CPU.ExceptionVectorTable";

// Fixed low-memory address of the interrupt-pending table (RFC-0036 Requirement 6.4,
// CPU.InterruptPendingTableBase()). One U64 monotonic tick counter per IRQ line (16 entries, 8
// bytes each, index = vector - 32), incremented by the exception-entry table's shared IRQ-dispatch
// branch and read/reset by ArcoBASIC policy (Timer.Ticks(), stdlib/timer_policy.abas) via ordinary
// MEMORY.Read64/MEMORY.Write64 -- never by a Load/Store AMIR instruction, since nothing about this
// address lives inside the compiled image. It cannot live in .text or .rdata (both read-only /
// execute-only at runtime -- see pe_image.cpp's section characteristics) since the ISR must write
// it on every tick, so it is a fixed scratch address instead, exactly as
// aps-emergency-stack.md's kDoubleFaultProbeAddress technique already proved sound under QEMU/OVMF.
// Deliberately well clear of that address (0x2000000, a single 8-byte write) and, like it, kept
// well under 128 MiB -- the smallest RAM size a test harness might run this table under with no
// explicit QEMU -m flag -- so it never silently reads back as zero on unbacked memory.
constexpr std::uint64_t kInterruptPendingTableAddress = 0x2010000ULL;
constexpr std::uint32_t kInterruptPendingTableEntryStride = 8;
constexpr std::uint32_t kInterruptPendingTableEntryCount = 16;

// Fixed low-memory result buffer for the freestanding MID(text, start, length) builtin (see the
// CallValue case in generate_x86_64_function). Same reasoning and same technique as
// kInterruptPendingTableAddress just above: nothing about a NEWLY CONSTRUCTED string's buffer can
// live in .text/.rdata (read-only), and this backend has no heap allocator at all (confirmed
// directly -- every freestanding STRING value until now was either an .rdata literal or a pointer
// copied straight through from somewhere else; MID is the first freestanding builtin that must
// hand back a buffer nobody else already owns). One shared, single fixed buffer, not one per call
// site -- the same "one instance, fixed scratch state, valid only until next overwritten" idiom
// this codebase already uses pervasively (ArcFSNodeScratchAddress, ArcFSSectorScratchAddress, and
// every other *ScratchAddress in arcology-os/stdlib/*.abas): callers that need to keep more than
// one MID result alive at once must copy one out immediately, exactly like those. Capacity is a
// documented, honest scope reduction, not a silent one: up to kMidResultMaxUnits UTF-16 code units
// (a longer request is truncated to that cap, not rejected) plus one null-terminator unit.
// Deliberately well clear of both the address above and 0x2000000/0x2030000, the range every
// arcology-os stdlib file's own MMIO scratch addresses already occupy.
constexpr std::uint64_t kMidResultAddress = 0x2018000ULL;
constexpr std::uint64_t kMidResultMaxUnits = 256ULL;

// Architectural x86-64 vectors whose interrupt gate delivers a hardware-pushed error code. Every
// other vector 0-31 pushes nothing, so a common handler cannot assume a uniform stack shape
// without each entry point normalizing this first (arcology-os/.agents/reports/aps-owned-idt.md's
// "remaining entry-ABI gate": "the next work must normalize CPU error-code/no-error-code frames").
bool x86_64_vector_has_error_code(int vector) {
    switch (vector) {
        case 8: case 10: case 11: case 12: case 13: case 14: case 17: case 21: case 29: case 30:
            return true;
        default:
            return false;
    }
}

// Synthesizes the common interrupt-entry table: 48 fixed-stride (16 byte) per-vector micro-stubs
// (32 architectural CPU exceptions, vectors 0-31, plus 16 remapped hardware IRQ lines, vectors
// 32-47 -- RFC-0036 Requirement 6.3) followed by one shared handler, entirely in hand-assembled
// machine code rather than lowered from A-MIR, since it needs an ABI (interrupt entry/IRETQ) no
// ordinary ArcoBASIC FUNCTION uses. Always emitted for the x86-64 systems target, addressed from
// ArcoBASIC policy code via the CPU.ExceptionVectorTableBase() intrinsic (vector N's entry point
// is `base + N*16`; the symbol's name and scope predate RFC-0036 and are intentionally left
// unchanged -- see that RFC's Requirement 6.3 -- even though it now covers hardware interrupts
// too), so `stdlib/descriptor_table_policy.abas`'s BuildMinimalIDT can install a distinct,
// vector-aware handler for every architectural gate instead of one shared address for all 32, and
// `stdlib/timer_policy.abas`'s Timer.Initialize can do the same for vector 32 (IRQ0/PIT).
//
// Each per-vector stub normalizes the CPU's inconsistent error-code push into a uniform frame --
// pushing a placeholder 0 first when the vector has no hardware error code, which every vector
// 32-47 stub does unconditionally since a hardware IRQ never carries one -- then pushes its own
// vector number and jumps to the shared handler. The shared handler saves every general-purpose
// register and dispatches three ways: vector 3 (#BP, breakpoint/INT3) simply resumes (INT3 is
// trap-class, so the saved RIP already points past the one-byte opcode -- see the note below);
// vector >= 32 (a remapped hardware IRQ) marks the corresponding line pending in the
// interrupt-pending table and sends EOI (RFC-0036 Requirement 6.3) before resuming -- it MUST NOT
// call into arbitrary ArcoBASIC code, per RFC-0037 Requirement 6.2's top-half/bottom-half split;
// every other vector is treated as an unexpected fault and parks the processor (CLI; HLT loop)
// rather than resuming into undefined state or silently triple-faulting -- a safe, debuggable
// default until a real per-vector fault policy exists.
X86_64CodegenResult generate_exception_vector_table() {
    X86_64CodegenResult result;
    result.entry_symbol = kExceptionVectorTableSymbol;
    using Reg = systems::x86_64::Reg;

    constexpr int kVectorCount = 48;
    constexpr std::size_t kStubStride = 16;
    const std::size_t common_handler_offset = kVectorCount * kStubStride;

    for (int vector = 0; vector < kVectorCount; ++vector) {
        const std::size_t stub_start = result.text.size();
        if (!x86_64_vector_has_error_code(vector)) {
            result.text.push_imm8(0); // synthetic placeholder error code
        }
        result.text.push_imm8(static_cast<std::uint8_t>(vector));
        const std::size_t jmp_disp = result.text.jmp_rel32_placeholder();
        const std::int64_t next_instruction = static_cast<std::int64_t>(jmp_disp + 4);
        result.text.patch_i32(jmp_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(common_handler_offset) - next_instruction));
        // Pad to the fixed 16-byte stride so vector N's entry is always exactly base + N*16.
        while (result.text.size() - stub_start < kStubStride) result.text.nop();
    }

    // Order registers are saved in; restored in exact reverse. RSP itself is never saved here --
    // IRETQ restores it from the CPU-pushed frame, not from a GPR slot.
    const Reg kSavedRegisters[] = {
        Reg::RAX, Reg::RCX, Reg::RDX, Reg::RBX, Reg::RBP, Reg::RSI, Reg::RDI,
        Reg::R8, Reg::R9, Reg::R10, Reg::R11, Reg::R12, Reg::R13, Reg::R14, Reg::R15,
    };
    constexpr int kSavedRegisterCount = 15;
    for (Reg reg : kSavedRegisters) result.text.push_reg(reg);

    // Stack layout at this point, all offsets from the current RSP:
    //   +0..+119   the 15 saved GPRs (most recently pushed first)
    //   +120       vector number (this table's own push)
    //   +128       error code (real or synthetic placeholder)
    //   +136       saved RIP (long mode always pushes RSP/SS too, regardless of privilege change)
    constexpr std::uint32_t kVectorOffset = kSavedRegisterCount * 8;
    constexpr std::uint32_t kRipOffset = kVectorOffset + 16;

    result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(kVectorOffset));
    result.text.cmp_reg_imm32(Reg::RAX, 3); // #BP -- recovers by simply resuming, see below
    const std::size_t is_breakpoint_disp = result.text.jcc_rel32_placeholder(0x4); // JE -> restore
    result.text.cmp_reg_imm32(Reg::RAX, 32); // vectors >= 32 are remapped hardware IRQs (RFC-0036)
    const std::size_t is_irq_disp = result.text.jcc_rel32_placeholder(0x3); // JAE -> irq_dispatch
    result.text.cmp_reg_imm32(Reg::RAX, 0); // #DE -- IST1 stack-switch probe, see below
    const std::size_t not_double_fault_disp = result.text.jcc_rel32_placeholder(0x5); // JNE -> fault

    // IST1 stack-switch probe, TEST-ONLY: records the current RSP (if a gate's IST field is
    // nonzero and the mechanism is working, the CPU already switched to that TSS.ISTn stack
    // before this handler ever ran) to a fixed scratch address so the caller can read it back
    // after resuming and confirm it falls within an emergency stack it built, not the ordinary
    // one. Deliberately uses vector 0 (#DE, divide error) rather than vector 8 (#DF, the real
    // architectural reason IST1 exists): this probe is raised via CPU.Interrupt(n), a *software*
    // interrupt, and software INT never pushes a hardware error code -- not even for a vector
    // whose fault-raised form normally would (a genuine well-known gotcha; see
    // aps-emergency-stack.md). Using vector 8 here would silently misalign every stack offset
    // below by 8 bytes, since its stub expects a real error code already pushed. Vector 0 has no
    // architectural error code, so software INT and a genuine hardware fault produce the same
    // stack shape -- letting this probe validate the IST-switch mechanism generically (proving
    // BuildMinimalIDT's `IF vector = 8 THEN ist = 1`, the exact same construction, is correct)
    // without needing to engineer a real fault-on-fault double-fault condition, and without the
    // error-code mismatch. This is deliberately NOT a real double-fault (or divide-error) recovery
    // policy -- resuming from a genuine hardware fault has no well-defined "resume the faulting
    // code" semantics the way #BP's trap-class delivery does; production code should treat #DF as
    // terminal. Resuming here is only sound because CPU.Interrupt(n) is a deliberate trap-class
    // software interrupt, exactly like INT3. kDoubleFaultProbeAddress is a fixed low-memory
    // scratch address, safely within the first 1 GiB an APS identity map covers and never
    // otherwise allocated by these fixtures -- deliberately well under 128 MiB, the smallest RAM
    // size a test harness might run this table under (QEMU's own default with no explicit -m):
    // a scratch address at, say, 256 MiB reads back as silent zero on such a VM, not a fault --
    // the "write" and "read" both appear to succeed, and the missing backing memory looks
    // identical to the probe never having run at all. Found the hard way debugging exactly that.
    constexpr std::uint64_t kDoubleFaultProbeAddress = 0x2000000ULL;
    result.text.mov_rax_rsp();
    result.text.mov_reg_reg(Reg::RCX, Reg::RAX);
    result.text.mov_reg_imm64(Reg::RAX, kDoubleFaultProbeAddress);
    result.text.mov_store64_rax_from_rcx();
    // Falls through to `restore` in every prior version of this table; now the IRQ dispatch block
    // (below) sits physically between here and `restore`, so an explicit jump is required to keep
    // this path's behavior unchanged.
    const std::size_t probe_to_restore_disp = result.text.jmp_rel32_placeholder();

    // IRQ dispatch (RFC-0036 Requirement 6.3): entered with RAX still holding the vector number
    // (cmp does not modify its operands, so the `cmp rax, 32` above left it intact). This is the
    // ISR's *entire* payload for a hardware interrupt -- mark the line pending, send EOI, resume --
    // it MUST NOT call into arbitrary ArcoBASIC code (RFC-0037 Requirement 6.2's top-half/
    // bottom-half split; the bottom half runs later, outside interrupt context, driven by
    // Timer.Ticks() reading what this handler wrote).
    const std::size_t irq_start = result.text.size();
    {
        const std::int64_t next_instruction = static_cast<std::int64_t>(is_irq_disp + 4);
        result.text.patch_i32(is_irq_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(irq_start) - next_instruction));
    }
    // R11 holds the vector number for the rest of this block -- one of the 15 registers this table
    // already saves/restores around the whole handler, so clobbering it here is exactly as safe as
    // clobbering RAX/RCX/R10 below: the values a caller sees after IRETQ come from the stack slots
    // this table pushed at entry, never from whatever the handler body left in the registers.
    result.text.mov_reg_reg(Reg::R11, Reg::RAX);

    // pending_table[vector - 32] += 1. Address arithmetic and the load/increment/store sequence
    // use only encoder primitives this table (and aps-emergency-stack.md's probe) already exercise
    // -- no new x86-64 encoder primitive is needed for this RFC (see RFC-0036 Section 16).
    result.text.mov_reg_reg(Reg::RAX, Reg::R11);
    result.text.mov_reg_imm64(Reg::RCX, 32);
    result.text.sub_reg_reg(Reg::RAX, Reg::RCX);                      // RAX = irq_line = vector-32
    result.text.shl_reg_imm8(Reg::RAX, 3);                            // RAX = irq_line * 8
    result.text.mov_reg_imm64(Reg::RCX, kInterruptPendingTableAddress);
    result.text.add_reg_reg(Reg::RAX, Reg::RCX);                      // RAX = &pending_table[irq_line]
    result.text.mov_reg_reg(Reg::R10, Reg::RAX);                      // preserve address across the load
    result.text.mov_load64_rax();                                     // RAX = pending_table[irq_line]
    result.text.mov_reg_imm64(Reg::RCX, 1);
    result.text.add_reg_reg(Reg::RAX, Reg::RCX);                      // RAX = count + 1
    result.text.mov_reg_reg(Reg::RCX, Reg::RAX);                      // RCX = value to store
    result.text.mov_reg_reg(Reg::RAX, Reg::R10);                      // RAX = address again
    result.text.mov_store64_rax_from_rcx();                           // pending_table[irq_line] = count+1

    // EOI (RFC-0036 Requirement 6.3 step 2): send to the slave PIC (port 0xA0) first if this
    // vector is in the remapped IRQ8-15 range (>= 40 under the reference vector base 32), then
    // always to the master (port 0x20). This is structurally unconditional -- every IRQ path
    // reaches it, with no ArcoBASIC-policy opportunity to skip it -- because a missing or
    // conditionally-skipped EOI does not fail loudly; it silently stops all future ticks at that
    // priority level (RFC-0036 Section 2's own stated reason for making this the compiler's job,
    // not policy's).
    result.text.mov_reg_reg(Reg::RAX, Reg::R11);
    result.text.cmp_reg_imm32(Reg::RAX, 40);
    const std::size_t skip_slave_eoi_disp = result.text.jcc_rel32_placeholder(0x2); // JB -> skip (vector < 40)
    result.text.mov_reg_imm64(Reg::RAX, 0x20);
    result.text.mov_reg_imm64(Reg::RDX, 0xA0);
    result.text.out_dx_al();
    const std::size_t after_slave_eoi = result.text.size();
    {
        const std::int64_t next_instruction = static_cast<std::int64_t>(skip_slave_eoi_disp + 4);
        result.text.patch_i32(skip_slave_eoi_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(after_slave_eoi) - next_instruction));
    }
    result.text.mov_reg_imm64(Reg::RAX, 0x20);
    result.text.mov_reg_imm64(Reg::RDX, 0x20);
    result.text.out_dx_al();
    const std::size_t irq_to_restore_disp = result.text.jmp_rel32_placeholder();

    // Breakpoint recovery: #BP is a TRAP, not a fault -- INT3 already pushes the RIP of the
    // instruction *after* the one-byte opcode (unlike a fault, which pushes the address of the
    // faulting instruction itself for a retry). No adjustment is needed; falling straight through
    // to the restore-and-resume path below is the whole recovery. An earlier version of this code
    // added 1 here on the (wrong, fault-shaped) assumption that RIP still pointed at the INT3
    // byte, which resumed execution one byte into whatever instruction followed it -- harmless by
    // chance for some instruction shapes and silently catastrophic for others (a CALL immediately
    // after CPU.Breakpoint, resumed from CALL+1, executes garbage). Found via exactly that: a
    // deliberately minimal reproduction hung specifically whenever anything with a CALL followed
    // the breakpoint, and not otherwise.
    (void)kRipOffset;

    const std::size_t restore_start = result.text.size();
    for (const std::size_t disp : {is_breakpoint_disp, probe_to_restore_disp, irq_to_restore_disp}) {
        const std::int64_t next_instruction = static_cast<std::int64_t>(disp + 4);
        result.text.patch_i32(disp, static_cast<std::int32_t>(static_cast<std::int64_t>(restore_start) - next_instruction));
    }
    for (int i = kSavedRegisterCount - 1; i >= 0; --i) result.text.pop_reg(kSavedRegisters[i]);
    result.text.add_rsp_imm8(16); // drop this table's vector-number and error-code pushes
    result.text.iretq();

    // Unexpected fault: park the processor rather than resuming into undefined state.
    const std::size_t fault_start = result.text.size();
    {
        const std::int64_t next_instruction = static_cast<std::int64_t>(not_double_fault_disp + 4);
        result.text.patch_i32(not_double_fault_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(fault_start) - next_instruction));
    }
    const std::size_t spin_start = result.text.size();
    result.text.cli();
    result.text.hlt();
    result.text.jmp_rel8(static_cast<std::int8_t>(static_cast<std::int64_t>(spin_start) - static_cast<std::int64_t>(result.text.size() + 2)));

    return result;
}

// Generates x86-64 machine code for a single named function within `module` (Packet WP-008/WP-006,
// arcology-os/docs/systems/x86-64-codegen.md). Deliberately narrow: supports the A-MIR instruction
// kinds required by the systems fixtures, including explicit multi-block branches, with a uniform
// spill-everything strategy (Packet non-goal: "register allocator sophistication beyond correctness"
// -- every named value gets its own stack slot, always reloaded before use, never kept live in a
// register across instructions).
// Any other instruction kind, or any construct this milestone's UEFI bindings/calling convention
// do not cover, produces a clear error rather than an incorrect or silently wrong encoding.
X86_64CodegenResult generate_x86_64_function(const AmirModule& module, const std::string& function_name,
                                              systems::CallingConvention convention = systems::CallingConvention::MicrosoftX64,
                                              bool annotate = false) {
    X86_64CodegenResult result;
    result.entry_symbol = function_name;
    using Reg = systems::x86_64::Reg;
    using Xmm = systems::x86_64::Xmm;

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
    // Reference-counted lifetime tracking (RFC-0049, "full Linux support" pass): a PARAMETER's own
    // slot starts already holding the CALLER's own live reference (marshaled in below, not
    // zero-initialized the way a genuine local is) -- this function never owns that reference (the
    // by-value-copy convention every ArcoValue construction/array/object/global function already
    // documents in native_runtime_abi.h means a parameter is always "borrowed", never something
    // this function is responsible for releasing), so every retain/release/zero-init decision below
    // checks this set first and skips a parameter's own name entirely. A narrower, disclosed
    // consequence: reassigning a PARAMETER variable's own name to a new value inside a function body
    // (rare -- almost every real program treats a parameter as read-only) neither retains the new
    // value nor releases the old one, same as before this pass -- deliberately safer than guessing
    // wrong about which reference a parameter's slot currently owns and either double-freeing the
    // caller's own live object or leaking regardless.
    std::unordered_set<std::string> parameter_names;
    for (const auto& declared_parameter : target->params) {
        const std::string name = bare_parameter_name(declared_parameter);
        add_slot(name);
        parameter_names.insert(name);
    }
    for (const auto& block : target->blocks) {
        for (const auto& instruction : block.instructions) {
            add_slot(instruction.result);
            if (instruction.kind == AmirInstruction::Kind::Store || instruction.kind == AmirInstruction::Kind::Load) {
                add_slot(instruction.target);
            }
        }
    }

    const int shadow = systems::shadow_space_bytes(convention);
    const int register_count = systems::argument_register_count(convention);
    int max_outgoing_stack_args = 0;
    for (const auto& block : target->blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.kind != AmirInstruction::Kind::CallExternal && instruction.kind != AmirInstruction::Kind::CallValue &&
                instruction.kind != AmirInstruction::Kind::Call) continue;
            // The implicit UEFI `This` argument only exists for CallExternal (a UEFI protocol
            // method call); CallValue (a direct call to another compiled ArcoBASIC function) and
            // Call (PRINT's "Runtime.Print" target, System V only -- see the Call case below) have
            // no receiver, so reserving a slot for one here would silently over-allocate the frame
            // rather than being wrong, but there is no reason to since the exact instruction kind
            // is already known at this point.
            const int implicit_receiver = instruction.kind == AmirInstruction::Kind::CallExternal ? 1 : 0;
            const int total_args = static_cast<int>(instruction.operands.size()) + implicit_receiver;
            max_outgoing_stack_args = std::max(max_outgoing_stack_args, std::max(0, total_args - register_count));
        }
    }
    // Conservative (over-approximating is fine, just wastes a little stack): the largest operand
    // count of any Kind::CallValue instruction in this function, sized for the generic
    // host-function bridge's own per-call argument buffer (arco_call_host takes a plain
    // ArcoValue* array + count -- see that case's own codegen) -- most CallValue instructions never
    // reach that fallback at all (a direct declared-function call, an instance method dispatch, or
    // Runtime.Args/GetGlobal/SetGlobal all resolve earlier), but the buffer has to exist before any
    // instruction runs, so it's sized once up front like every other frame region here.
    int max_host_call_args = 0;
    for (const auto& block : target->blocks) {
        for (const auto& instruction : block.instructions) {
            // Kind::Tuple shares this same region for the identical reason: every element is
            // available up front on one instruction (unlike Kind::Array's own incremental push
            // loop), so Kind::Tuple's own codegen builds a contiguous ArcoValue* array here and
            // calls arco_value_new_tuple once, instead of pushing one at a time -- never used
            // simultaneously with a CallValue's own use of this region within one instruction's own
            // codegen, so sizing both from the same scan is safe.
            if (instruction.kind != AmirInstruction::Kind::CallValue && instruction.kind != AmirInstruction::Kind::Tuple) continue;
            max_host_call_args = std::max(max_host_call_args, static_cast<int>(instruction.operands.size()));
        }
    }
    // One dedicated jmp_buf-sized slot per Kind::TryBegin SITE in this function (not per
    // invocation) -- see arco_try_push's own comment for why generated code calls `setjmp`
    // directly. Sized to a real jmp_buf's own size on this backend's only supported target
    // (x86-64 Linux glibc, 200 bytes), rounded up generously; per-site rather than a single shared
    // slot so nested TRY blocks in the same function never clobber each other's saved context.
    // try_block_offsets is filled in below (ordinal position -> frame offset) and read back by
    // Kind::TryBegin's own codegen, keyed by each instruction's position in this same scan order.
    constexpr int kJmpBufSlotSize = 256;
    int try_block_count = 0;
    for (const auto& block : target->blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.kind == AmirInstruction::Kind::TryBegin) ++try_block_count;
        }
    }
    const int outgoing_base = shadow;
    const int slot_base = outgoing_base + 8 * max_outgoing_stack_args;
    const int scratch_base = slot_base + 8 * static_cast<int>(slot_names.size());
    const int host_args_base = scratch_base + 32;
    const int try_jmpbuf_base = host_args_base + 8 * max_host_call_args;
    int frame_size = try_jmpbuf_base + kJmpBufSlotSize * try_block_count;
    // RSP is kEntryRspMod16 (8) mod 16 at function entry; after `sub rsp, frame_size`, RSP must
    // be 0 mod 16 immediately before any CALL this function makes, which requires
    // frame_size % 16 == kEntryRspMod16.
    while (frame_size % 16 != systems::kEntryRspMod16) {
        ++frame_size;
    }
    // 4095 was this cap's original value -- not a real encoder limit (mov_store_disp32/
    // sub_rsp_imm32/etc. all take a real 32-bit displacement/immediate, addressable well past this
    // regardless of frame_size), just an early, arbitrary safety margin nothing had outgrown yet.
    // Raised here after a real program (Arconaut, a ~1000-line ArcoBASIC GUI admin tool) hit it
    // directly: every distinct AMIR name (a real local OR a compiler-generated temp) gets its own
    // permanent 8-byte slot for a function's entire lifetime with no reuse across dead ranges (a
    // real, disclosed, separate scalability gap -- see this file's own liveness/slot-reuse TODOs,
    // none of which exist yet), so a large flat top-level script accumulates slots fast. 1MB is a
    // generous, still-safe margin against the default 8MB Linux thread stack -- no other assumption
    // in this file depends on frame_size staying small.
    if (frame_size > 1000000) {
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
        // Reference-counted lifetime tracking (RFC-0049, "full Linux support" pass): "STRING" is
        // this backend's own established convention (see native_runtime_abi.h, and every ADDRESSOF/
        // instance-dispatch/array/object call site already using it) for "a boxed ArcoValue*
        // pointer", so a store of that type into a NAMED, non-parameter local this backend's own
        // classifier (infer_local_kind) already knows is Boxed is always REPLACING whatever
        // reference that slot held a moment ago -- the caller of store_result already owns a fresh
        // reference to the NEW value (a construction/call-result/lookup this instruction itself just
        // produced -- see every store_result("...", "STRING") call site, none of which alias an
        // existing local's own pointer; Kind::Store's plain local-to-local copy is handled
        // separately, with its own retain, below), so only the OLD value needs releasing here, never
        // a retain of the new one. The zero-init sweep above guarantees a local's first-ever store
        // sees a safe null "old value" rather than stack garbage. Parameters are excluded --
        // borrowed, never owned by this function, see parameter_names' own comment.
        const bool tracks_lifetime = convention == systems::CallingConvention::SystemV && type == "STRING" &&
            parameter_names.count(name) == 0 && infer_local_kind(module, *target, name, 0) == HostedValueKind::Boxed;
        // R10 survives the store below untouched (a pure register-to-memory write touches no other
        // register) and is caller-saved/otherwise unused across this narrow window, so it's safe as
        // scratch for the slot's outgoing value -- picked distinct from every register `reg` is ever
        // actually called with (always RAX at every real call site today, see the grep this comment
        // is backed by) so the two loads below never alias.
        if (tracks_lifetime) {
            if (offset <= 127) result.text.mov_load_disp8(Reg::R10, Reg::RSP, static_cast<std::uint8_t>(offset));
            else result.text.mov_load_disp32(Reg::R10, Reg::RSP, static_cast<std::uint32_t>(offset));
        }
        normalize(reg, type);
        if (offset <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(offset), reg);
        else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(offset), reg);
        if (tracks_lifetime) {
            result.text.mov_reg_reg(Reg::RDI, Reg::R10);
            const auto release_disp = result.text.call_rel32_placeholder();
            result.external_calls.push_back({release_disp, "arco_value_release"});
        }
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
    // Every ArcoBASIC number is a double -- there is no separate integer type in the language, so
    // ordinary hosted arithmetic (`x = 1 + 2`, no `AS U64`-style freestanding type annotation at
    // all) must NOT fall into the fixed-width-integer path above, which was always correct only for
    // the freestanding profile's own hardware-facing declarations. Gated on convention rather than
    // just the type string being empty: Microsoft x64 (the UEFI target) already relies on an empty
    // type defaulting to "U64" elsewhere in this function (see Load/Const's own `type.empty() ?
    // "U64" : type` fallback), and that must keep meaning exactly what it always has there --
    // System V (this backend's own Linux target, where a plain, unannotated ArcoBASIC number is the
    // overwhelmingly common case) is the only convention this ever applies to.
    const auto is_hosted_number = [&](const std::string& type) {
        return convention == systems::CallingConvention::SystemV && (type.empty() || type == "NUMBER");
    };
    // Same question, but for a whole declared PARAMETER (its raw "name" or "name AS Type" text)
    // rather than a bare type string -- SELF (Phase 2 classes, RFC-0049 Section 4) is a magic
    // parameter name the compiler itself binds (lower_class's own method-compiling loop always
    // inserts it untyped as the implicit first parameter, unless SHARED) and is architecturally
    // ALWAYS a class instance (a Boxed Object), never a hosted-number, unlike an ordinary untyped
    // parameter -- which this backend's own is_hosted_number convention otherwise assumes defaults
    // to a number, the overwhelmingly common case for everything that ISN'T SELF. Checked by name
    // rather than by any type annotation, since SELF genuinely never has one to check.
    const auto param_is_hosted_number = [&](const std::string& declared_parameter) {
        return bare_parameter_name(declared_parameter) != "SELF" && is_hosted_number(declared_parameter_type(declared_parameter));
    };
    const auto store_result_double = [&](const std::string& name, Xmm src) -> bool {
        const int offset = slot_of(name);
        if (offset < 0) {
            result.ok = false;
            result.error = "value \"" + name + "\" has no assigned stack slot";
            return false;
        }
        result.text.movsd_store_disp32(Reg::RSP, static_cast<std::uint32_t>(offset), src);
        return true;
    };
    const auto load_value_double = [&](const std::string& name, Xmm dst) -> bool {
        const int offset = slot_of(name);
        if (offset < 0) {
            result.ok = false;
            result.error = "value \"" + name + "\" has no assigned stack slot";
            return false;
        }
        result.text.movsd_load_disp32(dst, Reg::RSP, static_cast<std::uint32_t>(offset));
        return true;
    };

    // Reference-counted lifetime tracking (RFC-0049, "full Linux support" pass): set by
    // box_operand_into_rax on every call to record whether IT was the one that just constructed a
    // brand-new, owned ArcoValue* (true for the Number/String/Bool branches below) versus merely
    // loading an EXISTING local's own already-boxed pointer with no new allocation at all (the Boxed
    // branch). Every call site that copies the boxed value BY VALUE into something else
    // (arco_value_array_push/_set, arco_value_object_set, arco_value_concat, arco_global_set,
    // arco_call_host -- none of these take ownership of the ArcoValue* they're handed, see each
    // one's own header comment) must release exactly the temporaries THIS flag says it freshly
    // created, immediately after that copying call returns -- releasing an already-Boxed operand
    // here would be wrong (it's an existing local's own live reference, not a temp this call site
    // owns). Checked immediately after each box_operand_into_rax call, before any subsequent call to
    // it overwrites this flag.
    bool box_operand_freshly_boxed = false;
    // Leaves a boxed ArcoValue* for `operand` in RAX: constructs a NEW one via the matching
    // arco_value_new_* external call when `operand` is a plain hosted-number/string/bool value, or
    // just loads its ALREADY-boxed pointer directly (no call at all) when it's Boxed (an
    // array/object, or the result of indexing into either -- see HostedValueKind::Boxed). Shared by
    // Array/Object construction and StoreIndex's own value operand (Phase 2, RFC-0049 Section 4),
    // all of which need "some already-boxed ArcoValue* in a register" regardless of which path got
    // there. Only valid under System V -- callers must gate on that themselves, same as every other
    // hosted-number-aware helper in this function.
    const auto box_operand_into_rax = [&](const std::string& operand) -> bool {
        const HostedValueKind kind = infer_hosted_value_kind(module, *target, operand);
        box_operand_freshly_boxed = kind != HostedValueKind::Boxed;
        if (kind == HostedValueKind::Number) {
            if (!load_value_double(operand, Xmm::XMM0)) return false;
            const auto call_disp = result.text.call_rel32_placeholder();
            result.external_calls.push_back({call_disp, "arco_value_new_number"});
            return true;
        }
        if (kind == HostedValueKind::String) {
            if (!load_value(operand, "STRING", Reg::RDI)) return false;
            const auto call_disp = result.text.call_rel32_placeholder();
            result.external_calls.push_back({call_disp, "arco_value_new_string_utf16"});
            return true;
        }
        if (kind == HostedValueKind::Bool) {
            if (!load_value(operand, "BOOL", Reg::RDI)) return false;
            const auto call_disp = result.text.call_rel32_placeholder();
            result.external_calls.push_back({call_disp, "arco_value_new_bool"});
            return true;
        }
        if (kind == HostedValueKind::Boxed) {
            return load_value(operand, "STRING", Reg::RAX); // already an ArcoValue* -- no call needed
        }
        result.ok = false;
        result.error = "value \"" + operand + "\" is not statically classifiable as a "
            "string/number/bool/array/object (this backend's static analysis limit)";
        return false;
    };
    // Emits `call arco_value_release` on the ArcoValue* currently at [RSP+scratch_offset] -- shared
    // by every box_operand_into_rax call site below that copies a freshly-boxed temporary into
    // something else BY VALUE (see box_operand_freshly_boxed's own comment for the full list and
    // reasoning). Never called for an already-Boxed operand (box_operand_freshly_boxed is false
    // then), since that pointer is an existing local's own live reference, not a temp this call site
    // owns.
    const auto release_scratch_temp = [&](std::uint32_t scratch_offset) {
        result.text.mov_load_disp32(Reg::RDI, Reg::RSP, scratch_offset);
        const auto release_disp = result.text.call_rel32_placeholder();
        result.external_calls.push_back({release_disp, "arco_value_release"});
    };

    // Loads `operand` as a raw double into `dest`, unboxing it first (via arco_value_as_number,
    // which panics if the value genuinely isn't a number) if this backend's own, more precise
    // classification (infer_hosted_value_kind, not the frontend's own operand-type hint some
    // callers gate on before ever reaching here) says it's actually Boxed -- an array
    // element/object field pulled out via Kind::Index, never a raw double despite "looks like an
    // ordinary number" being the frontend's own best guess. Shared by Binary and Unary's hosted-
    // number arithmetic, both of which hit the identical failure mode without this: a real bug
    // caught by direct testing (`total = total + arr[i]` inside a loop) printed a denormal garbage
    // value ("2.32211e-309") -- the boxed pointer's own bit pattern, reinterpreted as a double --
    // instead of the correct sum, the same failure class as the earlier BOOL-as-double bug.
    const auto load_double_operand = [&](const std::string& operand, Xmm dest) -> bool {
        if (infer_hosted_value_kind(module, *target, operand) == HostedValueKind::Boxed) {
            if (!load_value(operand, "STRING", Reg::RDI)) return false;
            const auto call_disp = result.text.call_rel32_placeholder();
            result.external_calls.push_back({call_disp, "arco_value_as_number"});
            if (dest != Xmm::XMM0) result.text.movsd_reg_reg(dest, Xmm::XMM0);
            return true;
        }
        return load_value_double(operand, dest);
    };

    static const std::unordered_map<std::string, systems::x86_64::Reg> kRegisterByName = {
        {"RCX", systems::x86_64::Reg::RCX}, {"RDX", systems::x86_64::Reg::RDX},
        {"R8", systems::x86_64::Reg::R8}, {"R9", systems::x86_64::Reg::R9},
        // RDI/RSI: only ever selected as argument-carrying registers under System V (see
        // calling_convention.hpp's sysv_integer_argument_registers) -- Microsoft x64 never assigns
        // an argument to either, so these entries are simply unused on that convention.
        {"RDI", systems::x86_64::Reg::RDI}, {"RSI", systems::x86_64::Reg::RSI},
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
        // Kind::TryBegin's own catch-block target is a real control-flow edge -- reachable only
        // via an exceptional longjmp, never via an ordinary Jump/Branch terminator, so it needs
        // its own scan here (over every instruction in the block, not just the terminator: unlike
        // Jump/Branch, TryBegin is never itself a block's last instruction). Without this, a CATCH
        // block a TRY's own body never falls through to normally (the common case -- lower_try's
        // own try-body always ends with an explicit Jump past it) was never marked reachable at
        // all, and this function's own branch_fixups resolution below failed outright
        // ("unresolved x86-64 branch target") the first time this was tested end-to-end.
        for (const auto& instruction : instructions) {
            if (instruction.kind == AmirInstruction::Kind::TryBegin && !instruction.target.empty()) {
                pending_blocks.push_back(instruction.target);
            }
        }
        const auto& terminator = instructions.back();
        if (terminator.kind == AmirInstruction::Kind::Jump && !terminator.target.empty()) {
            pending_blocks.push_back(terminator.target);
        } else if (terminator.kind == AmirInstruction::Kind::Branch && terminator.operands.size() >= 3) {
            pending_blocks.push_back(terminator.operands[1]);
            pending_blocks.push_back(terminator.operands[2]);
        }
    }

    // sub rsp, imm8 (opcode 0x83) sign-extends its one-byte immediate: a frame_size of, say, 200
    // encoded as that raw byte is read by the CPU as -56, turning the prologue into `add rsp, 56`
    // -- growing right into the caller's own frame instead of allocating this function's. The
    // threshold below 128, not 256, is what keeps every encoded byte's sign bit clear. Found via
    // a genuinely wild jump (#UD at a bogus RIP) chasing an unrelated exception-entry-stub
    // integration bug: any function whose frame landed in [128,255] bytes silently corrupted the
    // stack on every call, not just this one.
    if (frame_size <= 127) result.text.sub_rsp_imm8(static_cast<std::uint8_t>(frame_size));
    else result.text.sub_rsp_imm32(static_cast<std::uint32_t>(frame_size));

    // Runtime.Args real support (RFC-0049, "full Linux support" pass -- found genuinely blocking a
    // real program, Arconaut, from compiling at all): the C runtime's own startup convention hands
    // this process's real argc/argv to `main` in EDI/ESI, exactly the same registers System V's own
    // calling convention would use for THIS function's first two integer parameters if it had any
    // -- since the synthesized top-level "Main" wrapper never declares real parameters, RDI/RSI
    // still hold argc/argv completely undisturbed at this exact point (the prologue's own `sub rsp`
    // just above never touches them), but the FIRST ordinary instruction below very well might.
    // Captured here, immediately, into a process-lifetime global (arco_runtime_capture_args) that
    // arco_runtime_args() later reads back to build the real boxed array `Runtime.Args()`'s own
    // codegen (Kind::CallValue) constructs -- see that case's own comment for why this replaced the
    // previous "always a null pointer" stub. Gated on this being literally the synthesized entry
    // wrapper (never a user-declared function that also happens to be named "Main", though nothing
    // else in this file resolves that ambiguity differently either) and on System V, the only
    // convention this backend's own C runtime entry point convention applies to at all.
    if (convention == systems::CallingConvention::SystemV && function_name == "Main") {
        const auto call_disp = result.text.call_rel32_placeholder();
        result.external_calls.push_back({call_disp, "arco_runtime_capture_args"});
    }

    // Reference-counted lifetime tracking (RFC-0049, "full Linux support" pass): every Boxed-kind
    // local this function genuinely declares (never a parameter -- see parameter_names above) starts
    // this frame holding uninitialized stack garbage, not a null pointer. The release logic
    // store_result/Kind::Store/Kind::Return add below all treat "the slot currently holds nullptr"
    // as the safe, no-op case for a local that hasn't been assigned yet (arco_value_release/retain
    // both already treat a null ArcoValue* as inert -- see runtime_abi.cpp) -- without this zero-init
    // sweep, a local's FIRST assignment would instead try to release whatever raw garbage bits
    // happened to be sitting in that stack slot as if they were a real ArcoValue* pointer, a real
    // crash. Only meaningful under System V (the only convention any of this ArcoValue machinery
    // exists for at all); XOR-then-store is one byte shorter than a 64-bit immediate move of zero.
    if (convention == systems::CallingConvention::SystemV) {
        bool zeroed_rax = false;
        for (const auto& name : slot_names) {
            if (parameter_names.count(name) != 0) continue;
            if (infer_local_kind(module, *target, name, 0) != HostedValueKind::Boxed) continue;
            if (!zeroed_rax) {
                result.text.xor_reg_reg(Reg::RAX, Reg::RAX);
                zeroed_rax = true;
            }
            const int offset = slot_of(name);
            if (offset <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(offset), Reg::RAX);
            else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(offset), Reg::RAX);
        }
    }

    // Spill incoming arguments. Register arguments arrive in the four Microsoft x64 integer
    // registers; later arguments are homed by the caller at entry-RSP+40, +48, ... (32 bytes of
    // shadow space plus the return address). After our prologue, entry-RSP is frame_size bytes
    // above the current RSP, so stack parameters are loaded from frame_size+stack_offset.
    //
    // System V classifies each parameter independently (a hosted-number parameter -- no `AS`
    // declaration, or `AS NUMBER` -- arrives in the next XMM register; everything else, e.g. `AS
    // STRING` or a freestanding fixed-width type, arrives in the next *integer* register), rather
    // than by raw position the way the Microsoft x64/freestanding path below still does (which
    // never sees a hosted-number parameter at all -- is_hosted_number is unconditionally false
    // under that convention). No stack-spilled parameters are attempted on this path yet (a
    // real, disclosed scope reduction, matching the Kind::Call general-call branch's own limit):
    // a function with more than 6 integer-class or 8 float-class parameters fails to compile with
    // a clear error instead of silently misreading the caller's stack.
    if (convention == systems::CallingConvention::SystemV) {
        int int_arg_index = 0;
        int float_arg_index = 0;
        const auto& int_regs = systems::sysv_integer_argument_registers();
        for (std::size_t i = 0; i < target->params.size(); ++i) {
            const std::string name = bare_parameter_name(target->params[i]);
            if (param_is_hosted_number(target->params[i])) {
                if (float_arg_index >= 8) {
                    result.ok = false;
                    result.error = "function \"" + function_name + "\" has more hosted-number "
                        "parameters than this backend's System V fast path supports (no "
                        "stack-spilled parameters yet)";
                    return result;
                }
                if (!store_result_double(name, static_cast<Xmm>(float_arg_index++))) return result;
            } else {
                if (int_arg_index >= static_cast<int>(int_regs.size())) {
                    result.ok = false;
                    result.error = "function \"" + function_name + "\" has more integer/pointer "
                        "parameters than this backend's System V fast path supports (no "
                        "stack-spilled parameters yet)";
                    return result;
                }
                const int parameter_slot = slot_of(name);
                const Reg src = kRegisterByName.at(int_regs[static_cast<std::size_t>(int_arg_index++)]);
                if (parameter_slot <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(parameter_slot), src);
                else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(parameter_slot), src);
            }
        }
    } else {
        const auto locations = systems::assign_argument_locations(convention, static_cast<int>(target->params.size()));
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

    // Incremented once per Kind::TryBegin encountered below, in the same scan order try_block_count
    // was computed in above -- gives each TRY block its own stable, non-overlapping jmp_buf slot.
    int try_block_ordinal = 0;
    for (const auto& current_block : target->blocks) {
        if (reachable_blocks.count(current_block.name) == 0) continue;
        block_offsets[current_block.name] = result.text.size();
        for (const auto& instruction : current_block.instructions) {
        // Arco native debugger tooling (see X86_64CodegenResult::InstructionAnnotation's own
        // comment) -- records where THIS instruction's own codegen starts, before any of it runs,
        // so the annotation always points at the first byte actually attributable to it (matches
        // how block_offsets above already records each block's own start the same way). Reuses
        // render_instruction verbatim (the exact function `reveal amir` itself calls) so the
        // annotation text is byte-for-byte identical to what a developer already sees there --
        // no second, drifting copy of AMIR's own text format to maintain.
        if (annotate) {
            std::ostringstream described;
            render_instruction(described, instruction, module.source_name);
            std::string comment = described.str();
            while (!comment.empty() && (comment.back() == '\n' || comment.back() == '\r')) comment.pop_back();
            result.annotations.push_back({result.text.size(), "[" + function_name + "] " + comment});
        }
        switch (instruction.kind) {
            case AmirInstruction::Kind::Source:
            case AmirInstruction::Kind::Label:
                break;

            // Pure compile-time metadata (a nested/hoisted FUNCTION declaration reaching Main's own
            // block -- see build_amir) with no runtime effect of its own; the function it names is
            // separately discovered and compiled by generate_x86_64_program's own module.functions
            // scan (see add_fragment above generate_x86_64_program), not by anything reachable from
            // here. Previously fell to the generic "unsupported A-MIR instruction kind" error below,
            // which is why every program declaring a FUNCTION at all -- not just the general
            // function-call fast path this milestone adds -- failed to build on this backend before
            // now.
            case AmirInstruction::Kind::DeclareFunction:
                break;

            // Same reasoning as DeclareFunction just above (Phase 2 classes, RFC-0049 Section 4):
            // pure compile-time metadata with no runtime effect of its own -- the class's own
            // ClassName/.Method/.__new functions (lower_class) are separately discovered and
            // compiled by generate_x86_64_program's own module.functions scan, and
            // module.class_parents (also populated by lower_class) is read directly by
            // resolve_class_method wherever an instance method call needs it, not by anything
            // reachable from here.
            case AmirInstruction::Kind::DeclareClass:
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
                // BRANCH's own result_type is ALWAYS "BOOL" at the AMIR level (a hardware-facing
                // type annotation, not a real per-value check) -- it does not actually distinguish
                // this backend's own two DIFFERENT physical representations sharing that one label:
                // an ordinary comparison result is a real 1-byte 0/1 value, but AND/OR/XOR between
                // two such comparisons (RFC-0049, "full Linux support" pass -- found genuinely
                // blocking Arconaut's own `n >= 0 AND n < LEN(parts)`-shaped conditions) produces a
                // real 8-byte DOUBLE instead (matching compile-run's own ground truth: eval_binary's
                // value_to_int coerces bools to numbers, so `TRUE AND FALSE` is a Number 0.0, not a
                // "true" Bool -- see the Binary case's own much larger comment on this exact
                // distinction). Loading THAT as a 1-byte GPR value via the ordinary path below would
                // AND the double's own raw bit pattern against 0xFF, garbage with no relationship to
                // the actual truth value (1.0's own low byte happens to be 0x00, a real bug caught by
                // direct testing: `IF n >= 0 AND n < LEN(parts)` took the wrong branch). Checked here
                // via infer_hosted_value_kind (which already knows this distinction, see its own
                // Binary-case comment) rather than trusting the AMIR's own "BOOL" label at face value.
                if (convention == systems::CallingConvention::SystemV &&
                    infer_hosted_value_kind(module, *target, instruction.operands[0]) == HostedValueKind::Number) {
                    if (!load_value_double(instruction.operands[0], Xmm::XMM0)) return result;
                    result.text.mov_reg_imm64(Reg::RAX, 0);
                    result.text.movq_xmm_reg(Xmm::XMM1, Reg::RAX);
                    result.text.ucomisd(Xmm::XMM0, Xmm::XMM1);
                    // JNE (ZF=0): true for anything != 0.0. The only value this path ever actually
                    // sees is a real AND/OR/XOR-of-bools result (always exactly 0.0 or 1.0, never
                    // NaN), so ucomisd's own unordered-case quirk (ZF=1 for NaN, same as equal-to-
                    // zero, which would make a genuine NaN condition read as false here) never
                    // actually arises for this specific value.
                    const std::size_t true_displacement = result.text.jcc_rel32_placeholder(0x5); // JNE
                    branch_fixups.push_back({true_displacement, instruction.operands[1]});
                    const std::size_t false_displacement = result.text.jmp_rel32_placeholder();
                    branch_fixups.push_back({false_displacement, instruction.operands[2]});
                    break;
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
                if (convention == systems::CallingConvention::SystemV && (text_operand == "true" || text_operand == "false")) {
                    // A literal TRUE/FALSE keyword (source_text "true"/"false" lowercase -- see
                    // parser.cpp's primary()) -- stored via the ordinary GPR/BOOL path, never
                    // parsed as a number (std::stod("true") would simply throw) and never treated
                    // as a hosted-number double the way a real numeric Const is.
                    result.text.mov_reg_imm64(Reg::RAX, text_operand == "true" ? 1 : 0);
                    if (!store_result(instruction.result, "BOOL")) return result;
                } else if (convention == systems::CallingConvention::SystemV && (text_operand == "nothing" || text_operand == "null")) {
                    // ArcoBASIC's null/"nothing" sentinel -- arco::Value's own monostate, produced
                    // e.g. by an uninitialized typed class field with no default
                    // (`Y AS Number` alone, no `= ...` -- see lower_class's own field-default
                    // handling). This backend's hosted-number fast path has no raw-double
                    // representation for null at all, so unlike an ordinary numeric Const it is
                    // represented the same way any other Boxed value is: a plain pointer, here
                    // literally the null pointer. arco_value_print already renders a null pointer
                    // as "NULL" (matching arco::Value::to_string()'s own null rendering), and
                    // arco_value_array_push/_set/object_set all accept a null `value` and store a
                    // real null arco::Value rather than panicking, so "nothing" flows through
                    // array/object storage exactly like any other Boxed value with no special
                    // casing needed at either of those call sites.
                    result.text.mov_reg_imm64(Reg::RAX, 0);
                    if (!store_result(instruction.result, "STRING")) return result;
                } else if (!text_operand.empty() && text_operand.front() == '"') {
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
                    // NEVER instruction.result_type here -- the same frontend-hint-unreliability
                    // pattern as every other fix in this file with this exact comment, but with a
                    // much sharper failure mode: a string literal used directly as a comparison
                    // operand (`app.Mode == "volumes"`, never itself STORE'd to a typed variable)
                    // reaches here with result_type set to "BOOL" (the COMPARISON's own type
                    // context, not this literal's actual representation -- type_of_expression
                    // annotates a Const from how it's CONSUMED, not what it intrinsically is).
                    // store_result's own normalize() then MASKS this raw 64-bit rip-relative
                    // pointer down to its low 8 bits (width_bits("BOOL") == 8) -- not a wrong
                    // VALUE, an actively corrupted ADDRESS: found via Arconaut, a real program,
                    // `app.Mode == "volumes" OR app.Mode == "images" OR ...` -- the SECOND and
                    // THIRD string literals in the OR-chain (both feeding a BOOL-typed comparison
                    // the same way the first one does) got masked to a 1-byte garbage "address"
                    // (0x28, 0x7e in different runs), and arco_value_new_string_utf16 tried to read
                    // UTF-16 text starting there -- a real SEGV, not a wrong answer, caught by
                    // AddressSanitizer after this file's own code review missed it (the resulting
                    // codegen is syntactically valid amd64, `AND RAX, 0xFF` is a completely
                    // ordinary instruction -- only running it under a real display against a real
                    // program surfaced this). A raw literal pointer must NEVER be masked to fewer
                    // than 64 bits by any consumer, unconditionally -- there is no code path
                    // anywhere in this backend that legitimately wants a truncated string address.
                    if (!store_result(instruction.result, "U64")) return result;
                // Same frontend-hint-unreliability pattern as the Branch/Load fixes just above (see
                // their own much larger comments) -- found via Arconaut ("full Linux support" pass):
                // an inline numeric literal used directly as a comparison operand (`count < 3`, never
                // itself STORE'd to a typed variable first) reaches here with an EMPTY
                // instruction.result_type, because type_of_expression only annotates a Const when
                // it's the immediate source of a typed assignment -- is_hosted_number("") is false, so
                // this fell into the raw-integer branch below and stored literal 3's INTEGER bit
                // pattern (0x0000000000000003) rather than its DOUBLE bit pattern (0x4008...). The
                // comparison codegen (this function's own Binary case, `load_double_operand` ->
                // `load_value_double`) then movsd-loaded those integer bits AS a double -- a real bug
                // caught by direct testing: `count < 3` with count=1 wrongly evaluated false, because
                // reinterpreting integer 3's bits as a double gives a tiny denormal (~1.5e-323),
                // smaller than count's real value 1.0. infer_hosted_value_kind's own Const case (see
                // its comment) already unconditionally classifies a plain numeric literal as Number
                // regardless of the frontend's hint -- trusted here the same way Branch/Load already
                // trust it, under the same SystemV-only gate (this backend's own hosted-number
                // convention has no meaning under Microsoft x64/freestanding, where an untyped Const
                // really is meant to be a raw integer -- see is_hosted_number's own comment).
                } else if (is_hosted_number(instruction.result_type) ||
                    (convention == systems::CallingConvention::SystemV &&
                     infer_hosted_value_kind(module, *target, instruction.result) == HostedValueKind::Number)) {
                    double value = 0.0;
                    try {
                        std::size_t consumed = 0;
                        value = std::stod(text_operand, &consumed);
                        if (consumed != text_operand.size()) throw std::invalid_argument("trailing characters");
                    } catch (const std::exception&) {
                        result.ok = false;
                        result.error = "numeric constant \"" + text_operand + "\" could not be parsed as a number";
                        return result;
                    }
                    std::uint64_t bits = 0;
                    std::memcpy(&bits, &value, sizeof(bits));
                    result.text.mov_reg_imm64(Reg::RAX, bits);
                    result.text.movq_xmm_reg(Xmm::XMM0, Reg::RAX);
                    if (!store_result_double(instruction.result, Xmm::XMM0)) return result;
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
                // Reference-counted lifetime tracking (RFC-0049, "full Linux support" pass): LOAD is
                // a plain slot-to-slot COPY exactly like Kind::Store (just named the other way --
                // `.target` is the SOURCE here, `.result` is the destination), so it has the exact
                // same aliasing hazard for a Boxed value: `%t11 := LOAD __instance` leaves BOTH
                // `__instance` and `%t11` pointing at the SAME ArcoValueBox. Without a matching
                // retain here, Kind::Return's own exit sweep (which correctly releases `__instance`
                // while excluding `%t11`, the value actually being returned) would drop the
                // refcount to zero and free the object out from under the very pointer being handed
                // back to the caller -- a real, found-by-running-it use-after-free (a class
                // constructor's own `RETURN VALUE %t11` after `%t11 := LOAD __instance`), not a
                // theoretical concern: this exact shape is how EVERY class constructor's own
                // `__new` returns its freshly built instance. Uses store_result's own "U64" default
                // type (result_type is empty for a Boxed load, unlike Kind::Store's own dedicated
                // codegen which always deals in raw 8-byte pointers already), so this can't reuse
                // store_result's "STRING"-gated tracks_lifetime check -- handled directly here
                // instead, gated on the DESTINATION's own classification (same as store_result's
                // convention), which correctly resolves via infer_local_kind's own Load-and-recurse
                // handling in infer_hosted_value_kind.
                const bool tracks_lifetime = convention == systems::CallingConvention::SystemV &&
                    parameter_names.count(instruction.result) == 0 &&
                    infer_local_kind(module, *target, instruction.result, 0) == HostedValueKind::Boxed;
                if (source_slot <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(source_slot));
                else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(source_slot));
                if (tracks_lifetime) {
                    const int dest_slot = slot_of(instruction.result);
                    if (dest_slot >= 0) {
                        // Old destination value survives in R10 across the retain call below, same
                        // scratch-register reasoning as Kind::Store's own identical pattern.
                        if (dest_slot <= 127) result.text.mov_load_disp8(Reg::R10, Reg::RSP, static_cast<std::uint8_t>(dest_slot));
                        else result.text.mov_load_disp32(Reg::R10, Reg::RSP, static_cast<std::uint32_t>(dest_slot));
                    }
                    result.text.mov_reg_reg(Reg::RDI, Reg::RAX);
                    const auto retain_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({retain_disp, "arco_value_retain"});
                    // arco_value_retain doesn't return the pointer it was given -- reload the
                    // source value fresh rather than assume RAX survived the call.
                    if (source_slot <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(source_slot));
                    else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(source_slot));
                }
                // The frontend's own static type hint (instruction.result_type) can be flatly WRONG
                // about which physical representation this backend actually uses for a given value
                // -- the identical principle Binary's own operand-type check already applies (see
                // its much larger comment on why infer_hosted_value_kind is trusted over the
                // frontend's hint whenever it has a definite answer), just not yet extended to LOAD
                // until this real bug (RFC-0049, "full Linux support" pass -- found genuinely
                // blocking Arconaut, a real program): `x = (i < n) AND (count < 3)` types `x` as
                // "BOOL" at the AMIR level (the frontend's own static-type-inference rule for AND/OR
                // expressions), but this backend's OWN "&"/"|"/"^"-between-bools codegen (see the
                // Binary case's own much larger comment) actually stores a REAL 8-byte DOUBLE there,
                // not a 1-byte BOOL -- confirmed directly against compile-run's own ground truth
                // (`PRINT x` prints "0"/"1", never "TRUE"/"FALSE"). Loading it back with a "BOOL"-
                // width normalize (AND 0xFF) masked the double's own raw bit pattern down to garbage
                // -- `i < n AND count < 3` for i=1, n=5, count=1 (genuinely true) printed "0" instead
                // of "1". Resolved by preferring infer_hosted_value_kind's own Number answer here.
                const std::string load_result_type = (convention == systems::CallingConvention::SystemV &&
                    infer_hosted_value_kind(module, *target, instruction.result) == HostedValueKind::Number)
                    ? "U64" : (instruction.result_type.empty() ? "U64" : instruction.result_type);
                normalize(Reg::RAX, load_result_type);
                if (!store_result(instruction.result, load_result_type)) return result;
                // Double-release bug (RFC-0049 native backend, found via the new --debug/--sanitize
                // tooling on a genuinely minimal FOR-EACH-loop repro, confirmed via ASan + a raw
                // objdump of the emitted call sequence): store_result's OWN tracks_lifetime check
                // (see its comment above) already releases the destination slot's OLD value whenever
                // load_result_type == "STRING" -- completely independently of this block's tracks_
                // lifetime flag, which is gated on the DESTINATION's inferred kind, not on the type
                // string passed to store_result. When both fire (a Boxed destination whose load_
                // result_type happens to be "STRING"), store_result's own release above and this
                // block's release below both release the SAME captured R10 value -- the destination
                // slot is only ever stored to once, so the second release is a genuine extra release
                // of a still-live reference. Concretely: `key := label + "|suffix"` then `FOR entry IN
                // cache: entry.Key == key`, on the loop's second iteration, over-released `key`'s own
                // box (a value the caller's `RETURN VALUE key` still needed) -- confirmed with gdb by
                // tracing every arco_value_retain/release call's pointer argument across the run and
                // finding this exact instruction release the same pointer twice with no matching
                // second retain. Only skip the explicit release here when store_result already did
                // it (load_result_type == "STRING") -- every other load_result_type (store_result's
                // own tracks_lifetime is STRING-only) still needs this block's own release, e.g. the
                // class-constructor `__instance` case this block's own comment documents, where load_
                // result_type normalizes to "U64" and store_result never touches lifetime at all.
                if (tracks_lifetime && load_result_type != "STRING") {
                    result.text.mov_reg_reg(Reg::RDI, Reg::R10);
                    const auto release_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({release_disp, "arco_value_release"});
                }
                break;
            }

            case AmirInstruction::Kind::Unary: {
                // Checked against the raw (possibly-empty) type field, before the "defaults to
                // U64" fallback just below -- same reasoning as Binary's own hosted-number check,
                // including trusting infer_hosted_value_kind's own definite answer over the
                // frontend's hint (e.g. `-LEN(arr)`, where type_of_expression's hardcoded
                // LEN-is-U64 rule would otherwise wrongly route this through the freestanding GPR
                // NEG path instead of real SSE2 negation -- see Binary's own identical fix).
                const bool operand_is_hosted_number = [&] {
                    const std::string frontend_hint = instruction.operand_types.empty() ? instruction.result_type : instruction.operand_types.front();
                    // Convention-gated for the identical reason Binary's own
                    // operand_is_hosted_number_for_binary is: infer_hosted_value_kind describes
                    // AMIR shape only, with no notion of which convention is being compiled for.
                    if (convention == systems::CallingConvention::SystemV) {
                        const HostedValueKind kind = infer_hosted_value_kind(module, *target, instruction.operands.front());
                        if (kind == HostedValueKind::String || kind == HostedValueKind::Bool) return false;
                        if (kind == HostedValueKind::Number || kind == HostedValueKind::Boxed) return true;
                    }
                    return is_hosted_number(frontend_hint);
                }();
                if (operand_is_hosted_number && instruction.target == "-") {
                    // Negate via 0.0 - x rather than an XOR sign-bit flip: reuses subsd, no new
                    // encoder primitive needed, and this codegen has no data section for a shared
                    // sign-mask constant to XOR against anyway.
                    if (!load_double_operand(instruction.operands.front(), Xmm::XMM0)) return result;
                    result.text.mov_reg_imm64(Reg::RAX, 0);
                    result.text.movq_xmm_reg(Xmm::XMM1, Reg::RAX);
                    result.text.subsd(Xmm::XMM1, Xmm::XMM0);
                    if (!store_result_double(instruction.result, Xmm::XMM1)) return result;
                    break;
                }
                if (operand_is_hosted_number && instruction.target == "~") {
                    // Matches eval_binary's own `static_cast<double>(~value_to_int(value))`:
                    // truncate to int64 (cvttsd2si, not a raw bit reinterpretation -- loading the
                    // operand via the generic GPR path below and NOT-ing its raw double bit
                    // pattern would be a real, silently wrong answer, not merely a different
                    // encoding, since `mov`/`movsd` both move the same 8 raw bytes with no
                    // conversion), NOT it, then convert back to a real double.
                    if (!load_double_operand(instruction.operands.front(), Xmm::XMM0)) return result;
                    result.text.cvttsd2si_reg_xmm(Reg::RAX, Xmm::XMM0);
                    result.text.not_reg(Reg::RAX);
                    result.text.cvtsi2sd_xmm_reg(Xmm::XMM0, Reg::RAX);
                    if (!store_result_double(instruction.result, Xmm::XMM0)) return result;
                    break;
                }
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
                // Checked against the RAW (possibly-empty) operand/result type fields, before the
                // "defaults to U64" fallback just below runs -- that fallback exists for the
                // freestanding integer path and must stay exactly as it always has for Microsoft
                // x64; is_hosted_number's own SystemV gate already makes this side a no-op there.
                // The frontend's own operand-type hint (instruction.operand_types) can be flatly
                // WRONG for an operand it has no way to see the runtime/producing shape of -- two
                // real bugs caught by direct testing, both fixed by trusting this backend's OWN
                // analysis (infer_hosted_value_kind, which walks the ACTUAL producing instruction)
                // over the frontend's hint whenever it has a definite answer, rather than the other
                // way around:
                //   1. `"Hello, " + SELF.Name` (SELF.Name a Kind::Index result): type_of_expression
                //      has no notion of "what does this object property actually hold" and falls
                //      back to "" for it, which is_hosted_number then treats as an ordinary number
                //      -- `+` compiled as SSE2 addition and `arco_value_as_number` panicked at
                //      runtime instead of erroring (or concatenating) cleanly.
                //   2. `i < LEN(arr)`: type_of_expression has a hardcoded rule that any call
                //      literally named LEN has type "U64" (a freestanding-only assumption, correct
                //      for a freestanding STRING length but not this backend's own hosted LEN,
                //      which returns a real double -- see the CallValue case's own LEN special
                //      case) -- is_hosted_number("U64") is false, so the comparison fell through to
                //      the freestanding GPR path and failed to compile ("unsupported operation
                //      \"<\"") even though both operands are provably real hosted numbers.
                // Resolved by trusting infer_hosted_value_kind's own answer whenever it is definite
                // (Number/Boxed -> hosted-number, since a Boxed value can still be unboxed via
                // load_double_operand; String/Bool -> definitely not), and falling back to the
                // frontend's own hint only when this analysis genuinely can't tell (Unknown) --
                // matching the ORIGINAL "untyped defaults to number" behavior for that case only.
                // infer_hosted_value_kind describes AMIR SHAPE only -- it has no notion of which
                // convention is being compiled for, so its "Number"/"Boxed" answers must only ever
                // be trusted under System V: a real regression caught by the full test suite (not
                // just ad hoc testing) proved this the hard way -- without the convention gate
                // below, a plain freestanding Const (classified Number by infer_hosted_value_kind's
                // own default, since it isn't quoted/"nothing"/"true"/"false") made a Microsoft
                // x64 SAR operation wrongly enter this SystemV-only hosted-number arithmetic branch
                // and fail to compile ("unsupported operation \"SAR\"") on a previously-passing
                // UEFI fixture (systems_integer_core_smoke).
                const auto operand_is_hosted_number_for_binary = [&](const std::string& operand, const std::string& frontend_hint) {
                    if (convention == systems::CallingConvention::SystemV) {
                        const HostedValueKind kind = infer_hosted_value_kind(module, *target, operand);
                        if (kind == HostedValueKind::String || kind == HostedValueKind::Bool) return false;
                        if (kind == HostedValueKind::Number || kind == HostedValueKind::Boxed) return true;
                    }
                    return is_hosted_number(frontend_hint);
                };
                const bool left_is_hosted_number = operand_is_hosted_number_for_binary(instruction.operands[0],
                    instruction.operand_types.size() > 0 ? instruction.operand_types[0] : instruction.result_type);
                const bool right_is_hosted_number = operand_is_hosted_number_for_binary(instruction.operands[1],
                    instruction.operand_types.size() > 1 ? instruction.operand_types[1] : instruction.result_type);

                const std::string type = instruction.result_type.empty() ? "U64" : instruction.result_type;
                const std::string left_type = instruction.operand_types.size() > 0 ? instruction.operand_types[0] : type;
                const std::string right_type = instruction.operand_types.size() > 1 ? instruction.operand_types[1] : type;

                // AND/OR/XOR where at least one side is a BOOL (RFC-0049, "full Linux support" pass
                // -- found genuinely blocking Arconaut, a real program: `n >= 0 AND n < LEN(parts)`
                // -shaped expressions, where BOTH sides start out as comparison results -- genuinely
                // BOOL, not hosted numbers at all, so neither the hosted-number arithmetic branch
                // just below (which explicitly excludes Bool from "hosted number", see
                // operand_is_hosted_number_for_binary's own comment) nor the freestanding integer
                // path further down -- a different convention entirely -- was ever meant for this
                // shape). ArcoBASIC's AND/OR/XOR keywords lower to the exact same "&"/"|"/"^" AMIR
                // shape ordinary bitwise operators use (ast_operator has no separate token for them);
                // the operand TYPES are what actually distinguish "combine two booleans" from
                // "bitwise-combine two numbers" at codegen time. The RESULT here is still a real
                // NUMBER, never a "true" Bool -- confirmed directly against `compile-run`'s own
                // ground truth (`PRINT TRUE AND FALSE` prints "0", never "FALSE"): eval_binary's own
                // value_to_int coerces every operand, bools included, to an integer first, matching
                // this file's own identical Number-result convention for every other numeric bitwise
                // op (see the classifier's own matching comment on why nothing forces a Bool kind for
                // this shape) -- which is exactly why a CHAINED `a AND b AND c` is a real, distinct
                // case from the simple two-Bool one: it lowers to `(a AND b) AND c`, and the inner
                // AND's own result is classified Number (per infer_hosted_value_kind's fallthrough,
                // not Bool), so the outer AND sees one Bool operand and one Number operand -- a
                // second real bug caught by direct testing (Arconaut itself has exactly this shape,
                // three-way boolean conditions), fixed by accepting EITHER operand kind here (loading
                // each according to its OWN actual representation) rather than requiring both Bool.
                const HostedValueKind left_kind_for_bool_logic = infer_hosted_value_kind(module, *target, instruction.operands[0]);
                const HostedValueKind right_kind_for_bool_logic = infer_hosted_value_kind(module, *target, instruction.operands[1]);
                const auto is_bool_or_number_for_bool_logic = [](HostedValueKind kind) {
                    return kind == HostedValueKind::Bool || kind == HostedValueKind::Number;
                };
                if (convention == systems::CallingConvention::SystemV &&
                    (instruction.target == "&" || instruction.target == "|" || instruction.target == "^") &&
                    (left_kind_for_bool_logic == HostedValueKind::Bool || right_kind_for_bool_logic == HostedValueKind::Bool) &&
                    is_bool_or_number_for_bool_logic(left_kind_for_bool_logic) &&
                    is_bool_or_number_for_bool_logic(right_kind_for_bool_logic)) {
                    // A BOOL operand is already the exact 0/1 integer value_to_int would produce, so
                    // it loads straight into a GPR; a Number operand (the chained-AND/OR case above)
                    // needs the same truncating double->int64 conversion the numbers-only bitwise
                    // path below uses (cvttsd2si, matching value_to_int exactly), since it may not
                    // actually be 0/1 at all (e.g. `flags AND 4` reaching this same shape via a mixed
                    // Bool/Number operand pair).
                    const auto load_operand_as_int = [&](const std::string& operand, HostedValueKind kind, Reg dest) -> bool {
                        if (kind == HostedValueKind::Bool) return load_value(operand, "BOOL", dest);
                        if (!load_value_double(operand, Xmm::XMM0)) return false;
                        result.text.cvttsd2si_reg_xmm(dest, Xmm::XMM0);
                        return true;
                    };
                    // Right operand loaded first: both paths route through the shared XMM0 scratch
                    // register when the operand is a Number, so loading left (into RAX) last means
                    // its own conversion, if any, can't clobber right's already-finalized RCX value.
                    if (!load_operand_as_int(instruction.operands[1], right_kind_for_bool_logic, Reg::RCX) ||
                        !load_operand_as_int(instruction.operands[0], left_kind_for_bool_logic, Reg::RAX)) return result;
                    if (instruction.target == "&") result.text.and_reg_reg(Reg::RAX, Reg::RCX);
                    else if (instruction.target == "|") result.text.or_reg_reg(Reg::RAX, Reg::RCX);
                    else result.text.xor_reg_reg(Reg::RAX, Reg::RCX);
                    result.text.cvtsi2sd_xmm_reg(Xmm::XMM0, Reg::RAX);
                    if (!store_result_double(instruction.result, Xmm::XMM0)) return result;
                    break;
                }

                // "+" with an AMBIGUOUSLY Boxed operand (an array element, object field, or
                // generic host-function result -- see HostedValueKind::Boxed -- that isn't
                // PROVABLY a string, the existing plausibly_string-gated concat case just below
                // this whole Binary block already covers that) needs a real RUNTIME check, not a
                // static guess -- found via Arconaut, a real program: `quoted = quoted + ch` inside
                // ShellQuote's own char-by-char loop (`ch = String.Slice(...)`, a generic host
                // function -- classified Boxed, not String, since this backend has no return-type
                // signature table for arbitrary host functions). Both operands are ALWAYS actually
                // strings here, but the previous code had no way to know that statically, silently
                // treated "+" between two ambiguous-Boxed values as numeric addition (the
                // `left_is_hosted_number && right_is_hosted_number` branch just below treats Boxed
                // as hosted-number-capable, unconditionally), and panicked
                // ("value is not a number") the moment `arco_value_as_number` actually ran on a
                // real string -- a crash reachable from ordinary, direct use of this real GUI
                // program, not a contrived edge case. Matches `eval_binary`'s own ground-truth
                // dispatch exactly: `left.is_string() || right.is_string()` decided at runtime,
                // string concatenation if either is, numeric addition only otherwise -- the ONLY
                // correct answer for a dynamically-typed "+", which no amount of smarter static
                // analysis over a single straight-line producing instruction can ever fully resolve
                // (a Boxed value's real runtime kind is a property of the DATA, not the AMIR shape).
                //
                // Both branches box their result (even the numeric one, via arco_value_new_number,
                // where a case like `SELF.Value + 1` used to store a raw double directly) so this
                // instruction's result has exactly ONE representation regardless of which runtime
                // path fires -- infer_hosted_value_kind's own "+" case (see its comment) is updated
                // to classify this exact shape as Boxed to match, so every downstream consumer
                // (PRINT, further arithmetic, LOAD) already knows to unbox rather than misreading a
                // raw double's bits as a pointer or vice versa -- the same "one static answer, valid
                // under every runtime path" discipline the AND/OR/XOR-of-bools fix above and the
                // Branch/Load Number-vs-BOOL fixes elsewhere in this file already established. A
                // real, measured cost (one extra heap allocation per evaluation, versus the
                // zero-allocation raw-double fast path) only for this narrow, already-ambiguous
                // case -- an ordinary `a + b` with both operands provably Number, the overwhelming
                // common case, is completely unaffected and keeps its original fast path below.
                if (convention == systems::CallingConvention::SystemV && instruction.target == "+" &&
                    (left_kind_for_bool_logic == HostedValueKind::Boxed || right_kind_for_bool_logic == HostedValueKind::Boxed) &&
                    left_kind_for_bool_logic != HostedValueKind::String && right_kind_for_bool_logic != HostedValueKind::String) {
                    if (!box_operand_into_rax(instruction.operands[1])) return result;
                    const bool right_freshly_boxed = box_operand_freshly_boxed;
                    result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);
                    if (!box_operand_into_rax(instruction.operands[0])) return result;
                    const bool left_freshly_boxed = box_operand_freshly_boxed;
                    result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8), Reg::RAX);

                    result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8));
                    { const auto call_disp = result.text.call_rel32_placeholder();
                      result.external_calls.push_back({call_disp, "arco_value_is_string"}); }
                    result.text.cmp_reg_imm32(Reg::RAX, 0);
                    const std::size_t left_is_string_disp = result.text.jcc_rel32_placeholder(0x5); // JNE -> concat
                    result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    { const auto call_disp = result.text.call_rel32_placeholder();
                      result.external_calls.push_back({call_disp, "arco_value_is_string"}); }
                    result.text.cmp_reg_imm32(Reg::RAX, 0);
                    const std::size_t right_is_string_disp = result.text.jcc_rel32_placeholder(0x5); // JNE -> concat

                    // Neither is a string: numeric addition, unboxing each via arco_value_as_number
                    // (panics cleanly if one turns out not to be number/bool either, matching
                    // eval_binary's own identical `as_number()` panic in that case).
                    result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    { const auto call_disp = result.text.call_rel32_placeholder();
                      result.external_calls.push_back({call_disp, "arco_value_as_number"}); }
                    result.text.movsd_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base + 16), Xmm::XMM0);
                    result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8));
                    { const auto call_disp = result.text.call_rel32_placeholder();
                      result.external_calls.push_back({call_disp, "arco_value_as_number"}); }
                    result.text.movsd_load_disp32(Xmm::XMM1, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 16));
                    result.text.addsd(Xmm::XMM0, Xmm::XMM1);
                    { const auto call_disp = result.text.call_rel32_placeholder();
                      result.external_calls.push_back({call_disp, "arco_value_new_number"}); }
                    if (!store_result(instruction.result, "STRING")) return result;
                    if (left_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base + 8));
                    if (right_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base));
                    const std::size_t numeric_done_disp = result.text.jmp_rel32_placeholder();

                    // At least one operand is a real string: concatenate, identical to the ordinary
                    // provably-string "+" codegen further down this function.
                    const std::size_t concat_start = result.text.size();
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(left_is_string_disp + 4);
                        result.text.patch_i32(left_is_string_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(concat_start) - next_instruction));
                    }
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(right_is_string_disp + 4);
                        result.text.patch_i32(right_is_string_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(concat_start) - next_instruction));
                    }
                    result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8));
                    result.text.mov_load_disp32(Reg::RSI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    { const auto call_disp = result.text.call_rel32_placeholder();
                      result.external_calls.push_back({call_disp, "arco_value_concat"}); }
                    if (!store_result(instruction.result, "STRING")) return result;
                    if (left_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base + 8));
                    if (right_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base));

                    const std::size_t plus_done_start = result.text.size();
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(numeric_done_disp + 4);
                        result.text.patch_i32(numeric_done_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(plus_done_start) - next_instruction));
                    }
                    break;
                }

                // "==" / "!=" with an AMBIGUOUSLY Boxed or PROVABLY String operand on either side
                // -- the exact same "the ordinary hosted-number branch just below treats Boxed as
                // hosted-number-capable" hazard the "+" case above this one exists for, just for
                // equality instead of addition. Found the SAME way, immediately after fixing "+":
                // `app.Mode == Lower(label)` (Arconaut's own tab-highlighting check) -- BOTH sides
                // Boxed (an object field, a generic host-function result), so BOTH pass
                // operand_is_hosted_number_for_binary's own Boxed-is-hosted-number-capable rule and
                // fell into the ordinary numeric-comparison branch below (`ucomisd`/`sete`), which
                // has no string awareness at all -- a clean panic ("value is not a number") the
                // moment either side turned out to actually be the string it always is here, not a
                // contrived edge case. The pre-existing equality special case further down this
                // function (`arco_value_equals`) already exists for exactly this shape but is
                // gated on `plausibly_string` (PROVABLY String on at least one side, e.g.
                // `row.Status == "active"`) -- never reached when NEITHER side is provably String,
                // only ambiguously Boxed, since the numeric branch below claims the instruction
                // first. Moved earlier and widened here to catch that case too; the later block is
                // consequently unreachable for "==" / "!=" now (every Boxed/String combination is
                // caught here first, and a case with neither is correctly still real numeric
                // comparison below) but left in place as a harmless, already-correct fallback.
                // Unlike "+", equality has only ONE possible result shape (a plain BOOL) regardless
                // of whether the compared values turn out to be strings, numbers, or anything else
                // -- arco_value_equals (mirrors arco::values_equal exactly) already handles every
                // combination correctly with no runtime type branching needed in the GENERATED
                // code itself, unlike "+"'s own genuinely different string-vs-number result shapes.
                if (convention == systems::CallingConvention::SystemV &&
                    (instruction.target == "==" || instruction.target == "!=") &&
                    (left_kind_for_bool_logic == HostedValueKind::Boxed || right_kind_for_bool_logic == HostedValueKind::Boxed ||
                     left_kind_for_bool_logic == HostedValueKind::String || right_kind_for_bool_logic == HostedValueKind::String)) {
                    if (!box_operand_into_rax(instruction.operands[1])) return result;
                    const bool right_freshly_boxed = box_operand_freshly_boxed;
                    result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);
                    if (!box_operand_into_rax(instruction.operands[0])) return result;
                    const bool left_freshly_boxed = box_operand_freshly_boxed;
                    result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8), Reg::RAX);
                    result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8));
                    result.text.mov_load_disp32(Reg::RSI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    {
                        const auto call_disp = result.text.call_rel32_placeholder();
                        result.external_calls.push_back({call_disp, "arco_value_equals"});
                    }
                    if (instruction.target == "!=") {
                        result.text.xor_reg_imm32(Reg::RAX, 1);
                    }
                    if (!store_result(instruction.result, "BOOL")) return result;
                    if (left_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base + 8));
                    if (right_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base));
                    break;
                }

                // Ordinary ArcoBASIC arithmetic on two numbers (System V only -- see
                // is_hosted_number's own comment). `/` in particular has no integer-path
                // equivalent at all below (ArcoBASIC's `/` is always real division, e.g.
                // `10 / 3` == 3.333..., never the truncating kind), and both operands being
                // untyped is the *normal* case for a plain, unannotated ArcoBASIC number -- unlike
                // the "defaults to U64" fallback the freestanding integer path below still uses.
                if (left_is_hosted_number && right_is_hosted_number) {
                    // Either operand might actually be Boxed (an array element/object field pulled
                    // out via Kind::Index -- see arco_value_array_get/object_get, which always
                    // return an ArcoValue*, never a raw double) even though the FRONTEND's own
                    // static operand-type hint (instruction.operand_types, what
                    // left_is_hosted_number/right_is_hosted_number above are actually gated on)
                    // doesn't know that and defaults it to "looks like an ordinary number". A real
                    // bug caught by direct testing: `total = total + arr[i]` inside a loop printed
                    // a denormal garbage value ("2.32211e-309") instead of the correct sum -- the
                    // boxed pointer's own bit pattern, reinterpreted as a double by addsd, the same
                    // failure class as the earlier BOOL-as-double bug. Unboxed via
                    // load_double_operand (arco_value_as_number, panics if the value genuinely
                    // isn't a number) before any arithmetic touches it. Operand 1 is loaded/unboxed
                    // BEFORE operand 0 deliberately: an unboxing call always returns through XMM0,
                    // so unboxing operand 0 last means it lands directly in its own final
                    // destination with no extra move, while operand 1's result (if unboxed) needs
                    // one movsd into XMM1 either way.
                    //
                    // A LATENT register-clobber risk in this exact ordering, found by the marshal-
                    // loop audit this comment's own bug-history entry above prompted (RFC-0049,
                    // "full Linux support" pass): if operand 0 is ALSO Boxed, unboxing it (right
                    // below, an ordinary System V call to arco_value_as_number) is free to clobber
                    // XMM1 -- caller-saved, never guaranteed to survive any call -- even though
                    // operand 1's own value was already safely finalized there. Not currently
                    // observed to misbehave with this toolchain's own simple arco_value_as_number
                    // (confirmed directly: `arr[0] + arr[1]` with both operands Boxed still computes
                    // correctly today), but relying on an external function happening not to use a
                    // specific register is not a real correctness guarantee -- spilled to memory and
                    // reloaded instead, the same discipline every other multi-value marshal in this
                    // function now follows, whenever operand 0 could actually be Boxed.
                    if (infer_hosted_value_kind(module, *target, instruction.operands[0]) == HostedValueKind::Boxed) {
                        if (!load_double_operand(instruction.operands[1], Xmm::XMM1)) return result;
                        result.text.movsd_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Xmm::XMM1);
                        if (!load_double_operand(instruction.operands[0], Xmm::XMM0)) return result;
                        result.text.movsd_load_disp32(Xmm::XMM1, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    } else if (!load_double_operand(instruction.operands[1], Xmm::XMM1) ||
                        !load_double_operand(instruction.operands[0], Xmm::XMM0)) return result;
                    const std::string op = instruction.target;
                    bool is_comparison = false;
                    if (op == "+") result.text.addsd(Xmm::XMM0, Xmm::XMM1);
                    else if (op == "-") result.text.subsd(Xmm::XMM0, Xmm::XMM1);
                    else if (op == "*") result.text.mulsd(Xmm::XMM0, Xmm::XMM1);
                    else if (op == "/") result.text.divsd(Xmm::XMM0, Xmm::XMM1);
                    else if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
                        is_comparison = true;
                        result.text.ucomisd(Xmm::XMM0, Xmm::XMM1);
                        std::uint8_t condition = 0x4;
                        if (op == "!=") condition = 0x5;
                        else if (op == "<") condition = 0x2;
                        else if (op == "<=") condition = 0x6;
                        else if (op == ">") condition = 0x7;
                        else if (op == ">=") condition = 0x3;
                        // PF=1 (parity even, condition code 0x0A) means "unordered": either operand
                        // was NaN. Matches how a real C++ double comparison (what the tree-walking
                        // interpreter and bytecode VM both actually execute) already treats it: never
                        // equal to anything including itself, always "!=" true, always
                        // "<"/"<="/">"/">=" false -- checked explicitly here rather than trusting
                        // ZF/CF alone, exactly like the loop-JIT's own ucomisd handling
                        // (include/arco/jit_x86_64.hpp) and the STRING-equality walk just below.
                        const bool nan_result = (op == "!=");
                        const std::size_t unordered_disp = result.text.jcc_rel32_placeholder(0x0A); // JP
                        result.text.setcc_al(condition);
                        result.text.movzx_eax_al();
                        const std::size_t done_disp = result.text.jmp_rel32_placeholder();
                        const std::size_t unordered_start = result.text.size();
                        {
                            const std::int64_t next_instruction = static_cast<std::int64_t>(unordered_disp + 4);
                            result.text.patch_i32(unordered_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(unordered_start) - next_instruction));
                        }
                        result.text.mov_reg_imm64(Reg::RAX, nan_result ? 1 : 0);
                        const std::size_t done_start = result.text.size();
                        {
                            const std::int64_t next_instruction = static_cast<std::int64_t>(done_disp + 4);
                            result.text.patch_i32(done_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(done_start) - next_instruction));
                        }
                    } else if (op == "MOD") {
                        // Real floating-point remainder (std::fmod), matching eval_binary's own
                        // `std::fmod(left, divisor)` -- NOT the truncating-integer modulo the
                        // freestanding "\\"/MOD path elsewhere in this function uses (that path is
                        // gated on !is_hosted_number and never reached here). fmod is called
                        // directly as an external libm symbol rather than hand-rolling x87
                        // FPREM's own multi-step partial-remainder loop; its System V signature
                        // (double, double) -> double already matches XMM0/XMM1 in, XMM0 out
                        // exactly, with no extra marshaling needed.
                        //
                        // eval_binary throws "MOD divisor cannot be zero" for an exact zero
                        // divisor rather than letting fmod's own IEEE-754 divide-by-zero-is-NaN
                        // behavior silently propagate -- matched here via an explicit check.
                        // Ordered-and-equal, not just ZF=1 (ucomisd also sets ZF for an
                        // UNORDERED result) -- a NaN divisor must NOT panic, exactly like
                        // eval_binary's own `divisor == 0.0`, false for NaN.
                        result.text.mov_reg_imm64(Reg::RAX, 0);
                        result.text.movq_xmm_reg(Xmm::XMM2, Reg::RAX);
                        result.text.ucomisd(Xmm::XMM1, Xmm::XMM2);
                        const std::size_t unordered_disp = result.text.jcc_rel32_placeholder(0x0A); // JP -> not zero
                        const std::size_t zero_disp = result.text.jcc_rel32_placeholder(0x4); // JE -> panic
                        const std::size_t not_zero_start = result.text.size();
                        {
                            const std::int64_t next_instruction = static_cast<std::int64_t>(unordered_disp + 4);
                            result.text.patch_i32(unordered_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(not_zero_start) - next_instruction));
                        }
                        {
                            const auto call_disp = result.text.call_rel32_placeholder();
                            result.external_calls.push_back({call_disp, "fmod"});
                        }
                        const std::size_t skip_panic_disp = result.text.jmp_rel32_placeholder();
                        const std::size_t panic_start = result.text.size();
                        {
                            const std::int64_t next_instruction = static_cast<std::int64_t>(zero_disp + 4);
                            result.text.patch_i32(zero_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(panic_start) - next_instruction));
                        }
                        {
                            static const char kMessage[] = "MOD divisor cannot be zero";
                            const std::size_t data_offset = result.rdata.size();
                            for (char c : std::string(kMessage)) result.rdata.push_back(static_cast<std::uint8_t>(c));
                            result.rdata.push_back(0);
                            const std::size_t disp_offset = result.text.lea_rip_relative(Reg::RDI);
                            result.relocations.push_back({disp_offset, result.text.size(), data_offset});
                        }
                        {
                            const auto call_disp = result.text.call_rel32_placeholder();
                            result.external_calls.push_back({call_disp, "arco_value_panic"});
                        }
                        const std::size_t after_panic = result.text.size();
                        {
                            const std::int64_t next_instruction = static_cast<std::int64_t>(skip_panic_disp + 4);
                            result.text.patch_i32(skip_panic_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(after_panic) - next_instruction));
                        }
                    } else if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
                        // Bitwise/shift ops have no SSE2 equivalent at all -- converted to int64
                        // (truncating toward zero, matching eval_binary's own value_to_int
                        // helper), operated on with the ordinary GPR instructions the freestanding
                        // integer path already uses, then converted back to a real double, exactly
                        // mirroring how eval_binary itself computes these
                        // (`static_cast<double>(value_to_int(left) OP value_to_int(right))`). `>>`
                        // uses SAR (arithmetic, sign-extending), not SHR: value_to_int returns a
                        // SIGNED long long, and C++'s `>>` on a negative signed value is what
                        // eval_binary actually executes -- SAR matches that, SHR would not. The
                        // shift count naturally lands in CL (RCX's low byte) since RCX already
                        // holds the right operand, exactly what SHL/SHR/SAR's by-register form
                        // requires.
                        result.text.cvttsd2si_reg_xmm(Reg::RAX, Xmm::XMM0);
                        result.text.cvttsd2si_reg_xmm(Reg::RCX, Xmm::XMM1);
                        if (op == "&") result.text.and_reg_reg(Reg::RAX, Reg::RCX);
                        else if (op == "|") result.text.or_reg_reg(Reg::RAX, Reg::RCX);
                        else if (op == "^") result.text.xor_reg_reg(Reg::RAX, Reg::RCX);
                        else if (op == "<<") result.text.shl_reg_cl(Reg::RAX);
                        else if (op == ">>") result.text.sar_reg_cl(Reg::RAX);
                        result.text.cvtsi2sd_xmm_reg(Xmm::XMM0, Reg::RAX);
                    } else {
                        result.ok = false;
                        result.error = "unsupported operation \"" + op + "\" on numbers on this backend";
                        return result;
                    }
                    if (is_comparison) {
                        if (!store_result(instruction.result, "BOOL")) return result;
                    } else {
                        if (!store_result_double(instruction.result, Xmm::XMM0)) return result;
                    }
                    break;
                }

                // Freestanding STRING values are pointers to a UTF-16, null-terminated buffer
                // (see the Const case above: a string literal is encoded once into .rdata and
                // referenced by a RIP-relative pointer; a STRING parameter/local is whatever
                // pointer its caller/assignment supplied). Falling through to the generic
                // load-and-compare path below for "==="/"!=" would compare those two POINTERS,
                // not the text they reference -- two content-identical strings from different
                // CONST sites (or a parameter holding a copy of a literal) have different
                // addresses and would silently compare as never-equal. This was a real, live bug:
                // confirmed under actual QEMU execution (not just "SOURCE ACCEPTED"), a freestanding
                // `path = "HELLO"` check returned false even when `path` genuinely held "HELLO" --
                // see .agents/reports/aps-block-storage.md, which routed around it with raw byte
                // buffers rather than fix it (out of that RFC's own scope). Fixed here by walking
                // both UTF-16 buffers unit-by-unit until either a mismatch or a shared null
                // terminator is found, using only encoder primitives this backend already
                // exercises elsewhere (mov_load16_rax's [RAX]-addressed word load; the same
                // jcc-placeholder-then-patch forward-branch idiom the exception-entry table and
                // RFC-0036's IRQ dispatch already use for exactly this kind of hand-assembled
                // control flow).
                if ((left_type == "STRING" || right_type == "STRING") &&
                    (instruction.target == "==" || instruction.target == "!=")) {
                    if (!load_value(instruction.operands[0], left_type, Reg::R8) ||
                        !load_value(instruction.operands[1], right_type, Reg::R9)) return result;

                    const std::size_t loop_start = result.text.size();
                    result.text.mov_reg_reg(Reg::RAX, Reg::R8);
                    result.text.mov_load16_rax();                 // EAX = *ptr1 (zero-extended)
                    result.text.mov_reg_reg(Reg::R10, Reg::RAX);  // R10 = char1
                    result.text.mov_reg_reg(Reg::RAX, Reg::R9);
                    result.text.mov_load16_rax();                 // EAX = *ptr2
                    result.text.cmp_reg_reg(Reg::R10, Reg::RAX);
                    const std::size_t mismatch_disp = result.text.jcc_rel32_placeholder(0x5); // JNE
                    result.text.cmp_reg_imm32(Reg::R10, 0);
                    const std::size_t end_of_string_disp = result.text.jcc_rel32_placeholder(0x4); // JE
                    result.text.mov_reg_imm64(Reg::RCX, 2);
                    result.text.add_reg_reg(Reg::R8, Reg::RCX);
                    result.text.add_reg_reg(Reg::R9, Reg::RCX);
                    result.text.jmp_rel8(static_cast<std::int8_t>(
                        static_cast<std::int64_t>(loop_start) - static_cast<std::int64_t>(result.text.size() + 2)));

                    const std::size_t mismatch_start = result.text.size();
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(mismatch_disp + 4);
                        result.text.patch_i32(mismatch_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(mismatch_start) - next_instruction));
                    }
                    result.text.mov_reg_imm64(Reg::RAX, instruction.target == "==" ? 0 : 1);
                    const std::size_t done_disp = result.text.jmp_rel32_placeholder();

                    const std::size_t equal_start = result.text.size();
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(end_of_string_disp + 4);
                        result.text.patch_i32(end_of_string_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(equal_start) - next_instruction));
                    }
                    result.text.mov_reg_imm64(Reg::RAX, instruction.target == "==" ? 1 : 0);

                    const std::size_t done_start = result.text.size();
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(done_disp + 4);
                        result.text.patch_i32(done_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(done_start) - next_instruction));
                    }
                    if (!store_result(instruction.result, "BOOL")) return result;
                    break;
                }

                // Everything below this point is the FREESTANDING integer path -- it treats both
                // operands as fixed-width integers/pointers and does plain GPR arithmetic, which is
                // correct for that profile but WRONG for System V (this backend's own hosted
                // target) once neither the hosted-number branch above nor the STRING-equality
                // branch just above fired: string concatenation (`"a" + "b"`) and any other
                // operation this backend's own static analysis has already proven involves a
                // String/Bool operand (see operand_is_hosted_number_for_binary above)
                // are real, disclosed, unattempted future work, not something that should silently
                // fall through into adding two raw pointers together as if they were integers. A
                // real bug caught by direct testing before this guard existed: `"Hello, " +
                // SELF.Name` compiled successfully and printed a denormal garbage value (the two
                // pointers' own GPR sum, reinterpreted as a double by PRINT's own Number
                // classification of a "+" result) instead of failing cleanly.
                if (convention == systems::CallingConvention::SystemV) {
                    // String concatenation: eval_binary's own trigger is "either operand is
                    // ACTUALLY a string" (`left.is_string() || right.is_string()`), which then
                    // stringifies the OTHER operand too regardless of its own type (`"x=" + 5` ->
                    // "x=5"). This backend can't know at compile time whether a Boxed operand is
                    // ACTUALLY a string at runtime (arrays/objects/numbers are Boxed too) -- gated
                    // instead on "provably String, or Boxed (so genuinely might be a string at
                    // runtime)" for at least one side, which is as close as this straight-line
                    // static analysis can get; two operands that are BOTH provably Bool (no String,
                    // no Boxed -- e.g. `TRUE + TRUE`, which eval_binary itself resolves as numeric
                    // addition via as_number()'s own bool coercion, not concatenation) fall through
                    // to the disclosed error below rather than guessing wrong.
                    const HostedValueKind left_kind = infer_hosted_value_kind(module, *target, instruction.operands[0]);
                    const HostedValueKind right_kind = infer_hosted_value_kind(module, *target, instruction.operands[1]);
                    const auto plausibly_string = [](HostedValueKind kind) {
                        return kind == HostedValueKind::String || kind == HostedValueKind::Boxed;
                    };
                    if (instruction.target == "+" && (plausibly_string(left_kind) || plausibly_string(right_kind))) {
                        // Box each operand (or use its existing boxed pointer directly) via
                        // box_operand_into_rax, spilling in reverse order for the same reason
                        // load_double_operand does: boxing operand 0 SECOND means whichever
                        // internal construct call it might need doesn't clobber operand 1's
                        // already-spilled pointer.
                        if (!box_operand_into_rax(instruction.operands[1])) return result;
                        const bool right_freshly_boxed = box_operand_freshly_boxed;
                        result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);
                        if (!box_operand_into_rax(instruction.operands[0])) return result;
                        const bool left_freshly_boxed = box_operand_freshly_boxed;
                        result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8), Reg::RAX);
                        result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8));
                        result.text.mov_load_disp32(Reg::RSI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                        {
                            const auto call_disp = result.text.call_rel32_placeholder();
                            result.external_calls.push_back({call_disp, "arco_value_concat"});
                        }
                        if (!store_result(instruction.result, "STRING")) return result;
                        // arco_value_concat (see its own header comment) reads both operands and
                        // copies their to_string() content -- it never takes ownership of either
                        // pointer, so a temporary THIS call site boxed just to satisfy that ABI (not
                        // an existing local's own reference) must be released here or it leaks on
                        // every concatenation of a fresh scalar (RFC-0049, "full Linux support" pass
                        // -- found by a real, measured, linear-in-iteration-count RSS growth on a
                        // tight loop building strings this way, not assumed from reading the code).
                        if (left_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base + 8));
                        if (right_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base));
                        break;
                    }
                    // General equality (RFC-0049, "full Linux support" pass -- found genuinely
                    // blocking Arconaut, a real program: `row.Status == "active"`-shaped comparisons,
                    // where the left side is Boxed via a field/element read and the right side is a
                    // plain string, had no codegen at all before this, only the "+" concat case just
                    // above and the frontend-STRING-typed loop above THAT -- neither fires here).
                    // Reuses arco_value_equals (mirrors arco::values_equal exactly), boxing each
                    // operand the identical "spill in reverse order" way concat does just above, for
                    // the identical reason.
                    if ((instruction.target == "==" || instruction.target == "!=") &&
                        (plausibly_string(left_kind) || plausibly_string(right_kind))) {
                        if (!box_operand_into_rax(instruction.operands[1])) return result;
                        const bool right_freshly_boxed = box_operand_freshly_boxed;
                        result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);
                        if (!box_operand_into_rax(instruction.operands[0])) return result;
                        const bool left_freshly_boxed = box_operand_freshly_boxed;
                        result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8), Reg::RAX);
                        result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8));
                        result.text.mov_load_disp32(Reg::RSI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                        {
                            const auto call_disp = result.text.call_rel32_placeholder();
                            result.external_calls.push_back({call_disp, "arco_value_equals"});
                        }
                        if (instruction.target == "!=") {
                            result.text.xor_reg_imm32(Reg::RAX, 1);
                        }
                        if (!store_result(instruction.result, "BOOL")) return result;
                        // arco_value_equals reads both operands without taking ownership of either
                        // (see its own header comment) -- a temporary THIS call site boxed just to
                        // satisfy that ABI must be released here, same reasoning as concat above.
                        if (left_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base + 8));
                        if (right_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base));
                        break;
                    }
                    result.ok = false;
                    result.error = "unsupported operation \"" + instruction.target + "\" on this backend";
                    return result;
                }
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
                    // Shift directly into R8 (mirroring the SAR case below), not RAX: the
                    // out-of-range-shift-count safety check immediately after needs RAX as scratch
                    // for its own boolean result, and computing the shift into RAX first only to
                    // have that same register clobbered before the result is ever used discards
                    // the real shifted value entirely -- silently replacing "x SHR n" with "x"
                    // whenever the shift count is a runtime value rather than a provably-safe
                    // compile-time constant. Found via CPU.ExceptionVectorTableBase()'s live IDT
                    // integration proof: every dynamic-shift-count SHR/SHL in the freestanding
                    // backend was affected, not just AND-masked nibble extraction.
                    result.text.mov_reg_reg(Reg::R8, Reg::RAX);
                    if (op == "<<") result.text.shl_reg_cl(Reg::R8);
                    else result.text.shr_reg_cl(Reg::R8);
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
                // Reference-counted lifetime tracking (RFC-0049, "full Linux support" pass): unlike
                // store_result (which always stores a FRESH reference this instruction itself just
                // produced), a plain STORE is a raw slot-to-slot COPY -- `y = x` for two Boxed
                // locals leaves both slots pointing at the SAME ArcoValueBox, real aliasing, not a
                // fresh owned reference for `y`. Retaining the source here and releasing the
                // target's own old value keeps each local's own eventual release (its next
                // overwrite, or this function's own exit sweep -- see Kind::Return below) balanced:
                // every slot that ever held a live reference releases it exactly once, whether or
                // not it shares the underlying box with another slot. Gated on the TARGET's own
                // classification only (matching store_result's own convention).
                const bool tracks_lifetime = convention == systems::CallingConvention::SystemV &&
                    parameter_names.count(instruction.target) == 0 &&
                    infer_local_kind(module, *target, instruction.target, 0) == HostedValueKind::Boxed;
                // "this compiler's classifier is self-consistent: a Boxed-kind slot is never fed a
                // non-Boxed source" no longer holds unconditionally (RFC-0049, "full Linux support"
                // pass): a loop-carried variable can have ONE defining store that's genuinely Boxed
                // (e.g. `quoted = quoted + ch`, the Binary "+" case's own ambiguous-Boxed result,
                // always boxed -- see its comment) and ANOTHER that's an ordinary raw literal (the
                // SAME variable's initial `quoted = "'"`), so infer_local_kind's own single answer
                // for the TARGET name can legitimately disagree with THIS PARTICULAR source
                // instruction's own classification. A real segfault caught by direct testing
                // (Arconaut's own ShellQuote, exactly this shape): the raw literal source was never
                // boxed at all, yet tracks_lifetime (gated on the target) called arco_value_retain
                // directly on that raw rip-relative literal ADDRESS -- never a real ArcoValueBox.
                // Boxed here via box_operand_into_rax (the same helper Array/Object/StoreIndex
                // already trust for "make me a real boxed pointer out of ANY operand, whatever its
                // own native representation") instead of the ordinary raw-copy-and-retain path
                // whenever the source doesn't already carry an existing boxed reference of its own
                // to retain.
                const HostedValueKind store_source_kind = tracks_lifetime
                    ? infer_hosted_value_kind(module, *target, instruction.operands.front())
                    : HostedValueKind::Unknown;
                if (tracks_lifetime && store_source_kind != HostedValueKind::Boxed) {
                    // Spill the target's OLD value first -- box_operand_into_rax's own construction
                    // call (arco_value_new_string_utf16/_number/_bool) is free to clobber any
                    // volatile register, so this can't simply sit in one across it the way the
                    // ordinary retain/release path below keeps it in R10 (that path's own call,
                    // arco_value_retain, is known not to need it, but box_operand_into_rax's own
                    // call sites make no such promise).
                    if (target_slot <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(target_slot));
                    else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(target_slot));
                    result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);
                    if (!box_operand_into_rax(instruction.operands.front())) return result;
                    // A freshly boxed value is already a uniquely-owned fresh reference -- no
                    // separate retain needed, matching store_result's own "STRING" convention for
                    // the identical case (this call site never reaches here with an
                    // already-Boxed/not-freshly-boxed source, since store_source_kind != Boxed is
                    // exactly this branch's own gate).
                    if (target_slot <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(target_slot), Reg::RAX);
                    else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(target_slot), Reg::RAX);
                    result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    const auto release_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({release_disp, "arco_value_release"});
                    break;
                }
                if (value_slot <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(value_slot));
                else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(value_slot));
                if (tracks_lifetime) {
                    // Old target value survives in R10 across the retain call below (a plain
                    // ArcoValue* argument call never touches R10, see store_result's own identical
                    // scratch-register reasoning) so it can be released only after the new value is
                    // safely retained and stored -- if source and target happen to be the SAME
                    // underlying box (`x = x`), retaining before releasing is what keeps its
                    // refcount from ever transiently reaching zero.
                    if (target_slot <= 127) result.text.mov_load_disp8(Reg::R10, Reg::RSP, static_cast<std::uint8_t>(target_slot));
                    else result.text.mov_load_disp32(Reg::R10, Reg::RSP, static_cast<std::uint32_t>(target_slot));
                    result.text.mov_reg_reg(Reg::RDI, Reg::RAX);
                    const auto retain_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({retain_disp, "arco_value_retain"});
                    // arco_value_retain doesn't return the pointer it was given, so RAX (the value
                    // being stored) must be reloaded from its own source slot rather than assumed
                    // to have survived the call.
                    if (value_slot <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(value_slot));
                    else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(value_slot));
                }
                if (target_slot <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(target_slot), Reg::RAX);
                else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(target_slot), Reg::RAX);
                if (tracks_lifetime) {
                    result.text.mov_reg_reg(Reg::RDI, Reg::R10);
                    const auto release_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({release_disp, "arco_value_release"});
                }
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
                else if (instruction.target == "WBINVD") result.text.wbinvd();
                else if (instruction.target.rfind("INTERRUPT_", 0) == 0) {
                    long long vector = 0;
                    try {
                        vector = std::stoll(instruction.target.substr(10));
                    } catch (...) {
                        result.ok = false; result.error = "malformed CPU.Interrupt vector"; return result;
                    }
                    if (vector < 0 || vector > 255) { result.ok = false; result.error = "CPU.Interrupt vector out of range"; return result; }
                    result.text.int_imm8(static_cast<std::uint8_t>(vector));
                }
                else { result.ok = false; result.error = "unsupported memory barrier " + instruction.target; return result; }
                break;

            case AmirInstruction::Kind::Memory: {
                const std::string op = instruction.target;
                if (op == "READRSP") {
                    result.text.mov_rax_rsp();
                    if (!store_result(instruction.result, "U64")) return result;
                    break;
                }
                if (op == "READCS") {
                    result.text.mov_rax_cs();
                    if (!store_result(instruction.result, "U64")) return result;
                    break;
                }
                if (op == "EXCEPTIONVECTORTABLEBASE") {
                    // A cross-symbol RIP-relative reference, resolved by generate_x86_64_program
                    // through the same internal_calls mechanism ordinary function calls use --
                    // the "next instruction relative disp32" math is identical for CALL rel32 and
                    // LEA reg, [rip+disp32], so no separate relocation kind is needed.
                    const auto disp_offset = result.text.lea_rip_relative(Reg::RAX);
                    result.internal_calls.push_back({disp_offset, kExceptionVectorTableSymbol});
                    if (!store_result(instruction.result, "U64")) return result;
                    break;
                }
                if (op == "INTERRUPTPENDINGTABLEBASE") {
                    // Unlike the exception-vector table, this is not a symbol living inside the PE
                    // image: .text is executable+read-only and .rdata is read-only (see
                    // arcology-os/src/compiler/pe_image.cpp's kSectionMemExecute|kSectionMemRead /
                    // kSectionMemRead section characteristics), and this table must be *written* by
                    // the shared handler on every tick. It is instead a fixed, documented low-memory
                    // scratch address -- exactly the technique RFC-0036 Requirement 6.4 calls out by
                    // name ("exactly as aps-emergency-stack.md's scratch-address technique worked"),
                    // just promoted from a test-only hack to real ABI. kInterruptPendingTableAddress
                    // is chosen well clear of kDoubleFaultProbeAddress (0x2000000, used by the #DE
                    // IST1-switch test probe) and, like it, deliberately well under 128 MiB -- the
                    // smallest RAM size a test harness might run this table under with no explicit
                    // -m -- so it never silently reads back as zero on unbacked memory.
                    result.text.mov_reg_imm64(Reg::RAX, kInterruptPendingTableAddress);
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
                if (op == "READCR0") {
                    result.text.mov_rax_cr0();
                    if (!store_result(instruction.result, "U64")) return result;
                    break;
                }
                if (op == "WRITECR0") {
                    if (instruction.operands.size() != 1 || !load_value(instruction.operands[0], "U64", Reg::RAX)) return result;
                    result.text.mov_cr0_rax();
                    break;
                }
                // RDMSR/WRMSR -- fixed-register x86 ABI (ECX selects the MSR, EDX:EAX carries the
                // 64-bit value split into two 32-bit halves), not a convention this backend invents.
                // Real prerequisite for MTRR-based write-combining framebuffer performance without
                // any vendor-specific GPU driver -- MTRRs are architectural, identical on Intel and
                // AMD, so this is the same real fix either way.
                if (op == "READMSR") {
                    if (instruction.operands.size() != 1 || !load_value(instruction.operands[0], "U32", Reg::RCX)) return result;
                    result.text.rdmsr();
                    result.text.shl_reg_imm8(Reg::RDX, 32);
                    result.text.or_reg_reg(Reg::RAX, Reg::RDX);
                    if (!store_result(instruction.result, "U64")) return result;
                    break;
                }
                if (op == "WRITEMSR") {
                    if (instruction.operands.size() != 2 || !load_value(instruction.operands[0], "U32", Reg::RCX) ||
                        !load_value(instruction.operands[1], "U64", Reg::RAX)) return result;
                    result.text.mov_reg_reg(Reg::RDX, Reg::RAX);
                    result.text.shr_reg_imm8(Reg::RDX, 32);
                    result.text.wrmsr();
                    break;
                }
                if (op == "LGDT" || op == "LIDT" || op == "LTR") {
                    if (instruction.operands.size() != 1 || !load_value(instruction.operands[0], "U64", Reg::RAX)) { result.ok = false; result.error = "malformed descriptor-table operation"; return result; }
                    if (op == "LGDT") result.text.lgdt_rax();
                    else if (op == "LIDT") result.text.lidt_rax();
                    else result.text.ltr_rax();
                    break;
                }
                if (op == "RELOADSS") {
                    if (instruction.operands.size() != 1 || !load_value(instruction.operands[0], "U64", Reg::RAX)) { result.ok = false; result.error = "malformed stack-segment reload"; return result; }
                    result.text.mov_ss_rax();
                    break;
                }
                if (op == "RELOADDS") {
                    if (instruction.operands.size() != 1 || !load_value(instruction.operands[0], "U64", Reg::RAX)) { result.ok = false; result.error = "malformed data-segment reload"; return result; }
                    result.text.mov_ds_rax();
                    result.text.mov_es_rax();
                    result.text.mov_fs_rax();
                    result.text.mov_gs_rax();
                    break;
                }
                if (op == "RELOADCS") {
                    // The standard "push CS; push RIP; far-return" trick: push the target
                    // selector, then a RIP-relative computed address of the very next instruction
                    // after the RETFQ, then far-return into it. Reloads CS without ever leaving
                    // the current function or clobbering anything the caller can observe -- this
                    // reads as an ordinary statement that "returns" immediately after.
                    if (instruction.operands.size() != 1 || !load_value(instruction.operands[0], "U64", Reg::RAX)) { result.ok = false; result.error = "malformed code-segment reload"; return result; }
                    result.text.push_reg(Reg::RAX);
                    const auto resume_disp = result.text.lea_rip_relative(Reg::RAX);
                    result.text.push_reg(Reg::RAX);
                    result.text.retfq();
                    const std::size_t resume_offset = result.text.size();
                    const std::int64_t next_instruction = static_cast<std::int64_t>(resume_disp + 4);
                    result.text.patch_i32(resume_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(resume_offset) - next_instruction));
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
                if (op == "AEXINVOKENATIVE0") {
                    if (instruction.operands.size() != 1 || !load_value(instruction.operands[0], "MMIOPTR", Reg::RAX)) return result;
                    result.text.call_reg(Reg::RAX);
                    if (!store_result(instruction.result, "U64")) return result;
                    break;
                }
                if (op == "AEXINVOKENATIVE1") {
                    if (instruction.operands.size() != 2) { result.ok = false; result.error = "malformed AEX native1 invocation"; return result; }
                    if (!load_value(instruction.operands[1], "U64", Reg::RCX)) return result;
                    if (!load_value(instruction.operands[0], "MMIOPTR", Reg::RAX)) return result;
                    result.text.call_reg(Reg::RAX);
                    if (!store_result(instruction.result, "U64")) return result;
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
                if (op == "BLOCKIODISCOVER") {
                    // EFI_BLOCK_IO_PROTOCOL_GUID discovery via LocateProtocol, mirroring
                    // GOPDISCOVER exactly (RFC-0038 Section 17.5's own instruction to extend
                    // uefi-bindings.md's conventions rather than improvise a new binding style).
                    // GUID halves verified against MdePkg/Include/Protocol/BlockIo.h and
                    // cross-checked against the already-proven GOP GUID's own conversion.
                    if (!load_value(instruction.operands[0], "UEFI.SystemTable", Reg::RAX)) return result;
                    result.text.mov_load_disp8(Reg::R11, Reg::RAX, 0x60); // SystemTable.BootServices
                    const int guid_offset = scratch_base;
                    const int output_offset = scratch_base + 16;
                    result.text.mov_reg_imm64(Reg::RDX, 0x11D26459964E5B21ULL);
                    if (guid_offset <= 127) result.text.mov_store_disp8(Reg::RSP, static_cast<std::uint8_t>(guid_offset), Reg::RDX);
                    else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(guid_offset), Reg::RDX);
                    result.text.mov_reg_imm64(Reg::RDX, 0x3B7269C9A000398EULL);
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
                    // LocateProtocol returns EFI_STATUS in RAX. A failed discovery (no attached
                    // block device on this handle set) is represented as a null typed value for
                    // source-level guards, exactly matching GOPDISCOVER's own contract.
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
                    if (!store_result(instruction.result, "UEFI.BlockIoProtocol")) return result;
                    break;
                }
                if (op.rfind("BLOCKIO", 0) == 0 && op != "BLOCKIODISCOVER") {
                    // Plain EFI_BLOCK_IO_MEDIA field reads. The generic CallExternal path only
                    // resolves chains ending in a METHOD call, so these are hand-rolled the same
                    // way GOPWIDTH/GOPHEIGHT/etc. are -- one pointer hop (BlockIoProtocol.Media,
                    // offset 0x08) then a single field load. MediaFlags returns the raw packed
                    // RemovableMedia/MediaPresent/LogicalPartition/ReadOnly BOOLEAN word at
                    // Media+0x04 rather than exposing a single byte field: the encoder has no
                    // displacement-addressed single-byte load primitive, and adding one purely
                    // for this would be new shared-infrastructure risk for no real gain -- stdlib
                    // callers extract the byte they want with an ordinary SHR/AND (both already
                    // proven ArcoBASIC operators), an honest, named, minimal-risk scope reduction.
                    if (!load_value(instruction.operands[0], "UEFI.BlockIoProtocol", Reg::RAX)) return result;
                    result.text.mov_load_disp8(Reg::RAX, Reg::RAX, 0x08); // BlockIoProtocol.Media
                    int offset = 0x00;
                    bool wide = false;
                    if (op == "BLOCKIOMEDIAID") offset = 0x00;
                    else if (op == "BLOCKIOMEDIAFLAGS") offset = 0x04;
                    else if (op == "BLOCKIOBLOCKSIZE") offset = 0x0C;
                    else if (op == "BLOCKIOLASTBLOCK") { offset = 0x18; wide = true; }
                    if (wide) {
                        if (offset <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RAX, static_cast<std::uint8_t>(offset));
                        else result.text.mov_load_disp32(Reg::RAX, Reg::RAX, static_cast<std::uint32_t>(offset));
                    } else {
                        if (offset <= 127) result.text.mov_load32_disp8(Reg::RAX, Reg::RAX, static_cast<std::uint8_t>(offset));
                        else result.text.mov_load32_disp32(Reg::RAX, Reg::RAX, static_cast<std::uint32_t>(offset));
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
                // Marshals `args` against `resolved`'s own declared parameter list -- the same
                // per-parameter classification the ordinary declared-function-call path uses --
                // then emits the internal call and stores the result per `resolved`'s own inferred
                // return kind. Shared by instance method dispatch (args[0] is the receiver, bound
                // to the resolved method's own implicit SELF parameter) and ADDRESSOF/CALLABLE
                // dispatch below (a plain function call, no receiver) -- neither the caller's own
                // argument-list shape nor anything else here assumes a receiver is present, so one
                // definition serves both call shapes.
                const auto call_resolved_method = [&](const AmirFunction& resolved,
                                                       const std::vector<std::string>& args,
                                                       std::optional<std::uint32_t> receiver_scratch_offset = std::nullopt) -> bool {
                    if (args.size() != resolved.params.size()) {
                        result.ok = false;
                        result.error = "call to \"" + resolved.name + "\" expects " +
                            std::to_string(resolved.params.size()) + " arguments, got " + std::to_string(args.size());
                        return false;
                    }
                    const auto& int_regs = systems::sysv_integer_argument_registers();
                    // Every argument but the receiver override (already sitting safely in memory
                    // at receiver_scratch_offset, never touched here) gets its own dedicated
                    // scratch slot in host_args_base -- sized for this function's own largest
                    // CallValue operand list regardless of dispatch path (max_host_call_args, see
                    // its own comment above), so it always has room for instruction.operands.size()
                    // slots, exactly matching how many non-receiver arguments a call through this
                    // lambda can ever have.
                    const auto spill_offset = [&](std::size_t i) {
                        const std::size_t operand_index = receiver_scratch_offset.has_value() ? i - 1 : i;
                        return static_cast<std::uint32_t>(host_args_base + 8 * static_cast<int>(operand_index));
                    };
                    // PASS 1: compute every argument's value and spill it to its own slot BEFORE
                    // any of them are loaded into a real calling-convention register. This matters
                    // because a LATER argument's own unboxing (Boxed -> double via
                    // arco_value_as_number, inside load_double_operand) is a real external call,
                    // free to clobber ANY caller-saved register -- including one an EARLIER
                    // argument was already finalized into, had that finalization gone directly into
                    // its real register instead of stack memory first. A real, silent WRONG-ANSWER
                    // bug found by direct testing, not assumed: `a.Combine(b.Get())`-shaped calls
                    // (a Boxed-classified, method-call-derived hosted-number argument following an
                    // already-finalized receiver/earlier argument) printed a denormal garbage
                    // double, or in the plain Kind::Call path's identical bug just below,
                    // "value is not an object", instead of the correct answer -- because the
                    // SECOND argument's own unboxing call clobbered a register an EARLIER one had
                    // already been loaded into. Spilling everything to independent memory slots
                    // first, then reloading in a second, call-free pass, is immune to this
                    // regardless of how many arguments need boxing/unboxing calls of their own.
                    // A STRING-typed parameter's own freshly-boxed argument (see the "else if
                    // param_type == STRING" branch just below) needs releasing after the call
                    // returns -- tracked per-argument-index here, same shape as the generic
                    // host-function bridge's own host_arg_freshly_boxed vector.
                    std::vector<bool> string_arg_freshly_boxed(args.size(), false);
                    for (std::size_t i = 0; i < args.size(); ++i) {
                        if (i == 0 && receiver_scratch_offset.has_value()) continue;
                        if (param_is_hosted_number(resolved.params[i])) {
                            const std::string param_type = declared_parameter_type(resolved.params[i]);
                            if (param_type.empty()) {
                                const HostedValueKind argument_kind = infer_hosted_value_kind(module, *target, args[i]);
                                if (argument_kind == HostedValueKind::String || argument_kind == HostedValueKind::Bool ||
                                    argument_kind == HostedValueKind::Boxed) {
                                    const char* kind_name = argument_kind == HostedValueKind::String ? "string" :
                                        argument_kind == HostedValueKind::Bool ? "bool" : "array/object";
                                    result.ok = false;
                                    result.error = "call to \"" + resolved.name + "\" passes a " +
                                        std::string(kind_name) + " argument to parameter \"" +
                                        bare_parameter_name(resolved.params[i]) + "\", which has no explicit "
                                        "type annotation; this backend's System V fast path assumes an "
                                        "untyped parameter is a number";
                                    return false;
                                }
                            }
                            if (!load_double_operand(args[i], Xmm::XMM0)) return false;
                            result.text.movsd_store_disp32(Reg::RSP, spill_offset(i), Xmm::XMM0);
                        } else if (declared_parameter_type(resolved.params[i]) == "STRING") {
                            // See the plain Kind::Call path's own IDENTICAL branch (just below this
                            // lambda) for the full "why box_operand_into_rax, not a plain load"
                            // reasoning -- a STRING-typed parameter's actual physical
                            // representation is genuinely ambiguous at the call site (a raw literal
                            // pointer vs a real ArcoValueBox*), and infer_local_kind treats every
                            // STRING-typed parameter as Boxed inside the callee (see its own
                            // comment), so the callee must always actually receive one.
                            if (!box_operand_into_rax(args[i])) return false;
                            string_arg_freshly_boxed[i] = box_operand_freshly_boxed;
                            result.text.mov_store_disp32(Reg::RSP, spill_offset(i), Reg::RAX);
                        } else {
                            const std::string param_type = declared_parameter_type(resolved.params[i]);
                            const std::string load_type = param_type.empty() ? "U64" : param_type;
                            if (!load_value(args[i], load_type, Reg::RAX)) return false;
                            result.text.mov_store_disp32(Reg::RSP, spill_offset(i), Reg::RAX);
                        }
                    }
                    // PASS 2: reload every spilled value into its real calling-convention register,
                    // in order -- no external calls happen in this pass (plain loads only), so
                    // nothing already finalized can be clobbered by anything still to come.
                    int int_arg_index = 0;
                    int float_arg_index = 0;
                    for (std::size_t i = 0; i < args.size(); ++i) {
                        if (i == 0 && receiver_scratch_offset.has_value()) {
                            if (int_arg_index >= static_cast<int>(int_regs.size())) {
                                result.ok = false;
                                result.error = "call to \"" + resolved.name + "\" passes more "
                                    "integer/pointer arguments than this backend's System V fast path supports";
                                return false;
                            }
                            result.text.mov_load_disp32(kRegisterByName.at(int_regs[static_cast<std::size_t>(int_arg_index++)]),
                                                         Reg::RSP, *receiver_scratch_offset);
                            continue;
                        }
                        if (param_is_hosted_number(resolved.params[i])) {
                            if (float_arg_index >= 8) {
                                result.ok = false;
                                result.error = "call to \"" + resolved.name + "\" passes more "
                                    "hosted-number arguments than this backend's System V fast path supports";
                                return false;
                            }
                            result.text.movsd_load_disp32(static_cast<Xmm>(float_arg_index++), Reg::RSP, spill_offset(i));
                        } else {
                            if (int_arg_index >= static_cast<int>(int_regs.size())) {
                                result.ok = false;
                                result.error = "call to \"" + resolved.name + "\" passes more "
                                    "integer/pointer arguments than this backend's System V fast path supports";
                                return false;
                            }
                            result.text.mov_load_disp32(kRegisterByName.at(int_regs[static_cast<std::size_t>(int_arg_index++)]),
                                                         Reg::RSP, spill_offset(i));
                        }
                    }
                    {
                        const auto call_disp = result.text.call_rel32_placeholder();
                        result.internal_calls.push_back({call_disp, resolved.name});
                    }
                    const HostedValueKind return_kind = infer_function_return_kind(module, resolved);
                    bool return_stored = false;
                    if (return_kind == HostedValueKind::Number) {
                        return_stored = store_result_double(instruction.result, Xmm::XMM0);
                    } else if (return_kind == HostedValueKind::Bool) {
                        return_stored = store_result(instruction.result, "BOOL");
                    } else if (return_kind == HostedValueKind::String || return_kind == HostedValueKind::Boxed) {
                        return_stored = store_result(instruction.result, "STRING");
                    } else {
                        result.ok = false;
                        result.error = "call to \"" + resolved.name + "\" has a return value this "
                            "backend's System V fast path can't statically classify";
                        return false;
                    }
                    if (!return_stored) return false;
                    // Release any STRING-typed argument freshly boxed just for this call (PASS 1's
                    // own comment) -- deferred until AFTER the return value is already safely
                    // stored, since arco_value_release is a real external call, free to clobber
                    // XMM0/RAX the same way any other call in this file can.
                    for (std::size_t i = 0; i < args.size(); ++i) {
                        if (i == 0 && receiver_scratch_offset.has_value()) continue;
                        if (string_arg_freshly_boxed[i]) release_scratch_temp(spill_offset(i));
                    }
                    return true;
                };

                // Every top-level ArcoBASIC program's synthesized Main wrapper unconditionally
                // starts with `args = Runtime.Args()` (seeding the Args global -- see build_amir's
                // own comment near "Runtime.Args in runtime.cpp"), so this fires even for a program
                // that never references Args itself, on every target. Real support (RFC-0049, "full
                // Linux support" pass -- found genuinely blocking Arconaut, a real ~1000-line
                // program, from compiling at all: it checks `LEN(args) > 0` near its own top level,
                // and the OLD null-pointer stub had no defined static kind at all, failing with a
                // confusing "not statically classifiable" error rather than anything naming Args):
                // arco_runtime_args() builds a real boxed array from argv[1..] (skipping argv[0],
                // the program's own path -- the closest equivalent to arco_cli's own convention,
                // which skips its own binary name AND the script-file argument; a standalone native
                // binary has no separate "script file" argument to also skip), captured once at
                // this function's own entry (see the prologue's own arco_runtime_capture_args call,
                // just above the reference-lifetime zero-init sweep). Always a real array, even when
                // empty (never null), so LEN/indexing on it never needs a null-check the way the OLD
                // stub's raw null would have required.
                if (convention == systems::CallingConvention::SystemV && instruction.target == "Runtime.Args") {
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_runtime_args"});
                    if (!store_result(instruction.result, "STRING")) return result;
                    break;
                }
                // Script-scope globals (apply_script_global_scoping's own Runtime.SetGlobal/
                // GetGlobal AMIR calls -- a top-level variable referenced from inside a FUNCTION,
                // e.g. a script's own module-level state a handler routine reads/updates) and
                // SHARED class fields (lower_class's identical mechanism). The key operand is a
                // quoted string, but NOT always via a separate Const+temp the way an ordinary
                // string literal elsewhere in this function is: lower_class routes through a real
                // Const first (`key` is a %tN temp), but apply_script_global_scoping embeds the
                // quoted text DIRECTLY as the operand string with no Const instruction at all
                // (`{"\"" + escaped(name) + "\""}`) -- a real, confirmed-by-testing shape
                // difference (`slot_of` failing on the literal `"count"` text, quotes included, was
                // the first symptom). load_key_operand handles both: a quoted-literal operand is
                // encoded to rdata and loaded via a fresh RIP-relative LEA, exactly like the Const
                // case's own string handling; anything else falls back to the ordinary
                // slot-based load_value. Only the KEY needs this -- the VALUE (SetGlobal's second
                // operand) is always a real temp, boxed via the ordinary box_operand_into_rax.
                const auto load_key_operand = [&](const std::string& operand, Reg dst) -> bool {
                    if (operand.empty() || operand.front() != '"') return load_value(operand, "STRING", dst);
                    std::vector<char16_t> encoded;
                    try {
                        encoded = systems::encode_utf16_null_terminated(unquote_constant(operand));
                    } catch (const std::exception& error) {
                        result.ok = false;
                        result.error = std::string("global name cannot be encoded as UTF-16: ") + error.what();
                        return false;
                    }
                    const std::size_t data_offset = result.rdata.size();
                    for (char16_t unit : encoded) {
                        result.rdata.push_back(static_cast<std::uint8_t>(unit & 0xFF));
                        result.rdata.push_back(static_cast<std::uint8_t>((unit >> 8) & 0xFF));
                    }
                    const std::size_t disp_offset = result.text.lea_rip_relative(dst);
                    result.relocations.push_back({disp_offset, result.text.size(), data_offset});
                    return true;
                };
                if (convention == systems::CallingConvention::SystemV && instruction.target == "Runtime.GetGlobal" &&
                    instruction.operands.size() == 1) {
                    if (!load_key_operand(instruction.operands[0], Reg::RDI)) return result;
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_global_get"});
                    if (!store_result(instruction.result, "STRING")) return result;
                    break;
                }
                if (convention == systems::CallingConvention::SystemV && instruction.target == "Runtime.SetGlobal" &&
                    instruction.operands.size() == 2) {
                    if (!box_operand_into_rax(instruction.operands[1])) return result;
                    const bool value_freshly_boxed = box_operand_freshly_boxed;
                    result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);
                    if (!load_key_operand(instruction.operands[0], Reg::RDI)) return result;
                    result.text.mov_load_disp32(Reg::RSI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    {
                        const auto call_disp = result.text.call_rel32_placeholder();
                        result.external_calls.push_back({call_disp, "arco_global_set"});
                    }
                    // arco_global_set copies `value`'s content into its own store (see its header
                    // comment) -- never takes ownership, so a freshly boxed temp (not an existing
                    // local's own reference) leaks here otherwise, same reasoning as
                    // arco_value_concat above.
                    if (value_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base));
                    // Runtime.SetGlobal's own AMIR call site always discards its result (a bare
                    // temp() throwaway -- see apply_script_global_scoping/lower_class), but every
                    // Kind::CallValue still produces one; arco_global_set returns void, so RAX is
                    // explicitly zeroed rather than storing whatever it happened to leave there.
                    result.text.mov_reg_imm64(Reg::RAX, 0);
                    if (!store_result(instruction.result, "STRING")) return result;
                    break;
                }
                // LEN(text)/MID(text, start, length) on a freestanding STRING (a pointer to a
                // null-terminated UTF-16 buffer -- see the Const case above and lower_call's own
                // header note). No runtime helper exists to call on this backend, so both are
                // hand-assembled here, mirroring the STRING ==/!= fix's own shape in the Binary
                // case above: walk the buffer unit-by-unit with the same encoder primitives
                // (mov_load16_rax, the jcc-placeholder-then-patch forward-branch idiom, jmp_rel8
                // to loop). Gated on operand_types (populated only by lower_call's own LEN/MID
                // special case) so this can never intercept the unrelated internal "LEN" AMIR
                // call target the array/FOR EACH lowering path synthesizes elsewhere -- that one
                // never sets operand_types, so it falls through unchanged to the ordinary
                // declared-function lookup below, exactly as it did before this case existed.
                const std::string upper_call_target = upper_ascii(instruction.target);
                const bool string_len_or_mid = (upper_call_target == "LEN" || upper_call_target == "MID") &&
                    !instruction.operand_types.empty() && instruction.operand_types.front() == "STRING";
                if (string_len_or_mid && upper_call_target == "LEN") {
                    if (instruction.operands.size() != 1) {
                        result.ok = false; result.error = "LEN expects exactly 1 argument"; return result;
                    }
                    if (!load_value(instruction.operands[0], "STRING", Reg::R8)) return result;
                    result.text.mov_reg_imm64(Reg::R9, 0); // R9 = running unit count

                    const std::size_t loop_start = result.text.size();
                    result.text.mov_reg_reg(Reg::RAX, Reg::R8);
                    result.text.mov_load16_rax();               // EAX = *R8 (zero-extended)
                    result.text.cmp_reg_imm32(Reg::RAX, 0);
                    const std::size_t end_disp = result.text.jcc_rel32_placeholder(0x4); // JE
                    result.text.mov_reg_imm64(Reg::RCX, 2);
                    result.text.add_reg_reg(Reg::R8, Reg::RCX); // advance pointer past this unit
                    result.text.mov_reg_imm64(Reg::RCX, 1);
                    result.text.add_reg_reg(Reg::R9, Reg::RCX); // count++
                    result.text.jmp_rel8(static_cast<std::int8_t>(
                        static_cast<std::int64_t>(loop_start) - static_cast<std::int64_t>(result.text.size() + 2)));

                    const std::size_t end_start = result.text.size();
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(end_disp + 4);
                        result.text.patch_i32(end_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(end_start) - next_instruction));
                    }
                    result.text.mov_reg_reg(Reg::RAX, Reg::R9);
                    if (!store_result(instruction.result, "U64")) return result;
                    break;
                }
                if (string_len_or_mid && upper_call_target == "MID") {
                    if (instruction.operands.size() != 3) {
                        result.ok = false; result.error = "MID expects exactly 3 arguments"; return result;
                    }
                    const std::string start_type = instruction.operand_types.size() > 1 ? instruction.operand_types[1] : "U64";
                    const std::string length_type = instruction.operand_types.size() > 2 ? instruction.operand_types[2] : "U64";
                    // R8=source ptr (advances), R9=start-then-skip-counter, R12=length-then-copy-
                    // counter, R13=dest ptr (fixed base, then advances during the copy loop),
                    // R14=constant -1 (this encoder has no SUB-by-immediate primitive; every
                    // decrement below is `add reg, R14` instead, the same trick already used for
                    // "+2 per UTF-16 unit" via a preloaded RCX in the STRING equality fix above).
                    if (!load_value(instruction.operands[0], "STRING", Reg::R8)) return result;
                    if (!load_value(instruction.operands[1], start_type, Reg::R9)) return result;
                    if (!load_value(instruction.operands[2], length_type, Reg::R12)) return result;
                    result.text.mov_reg_imm64(Reg::R13, kMidResultAddress);
                    result.text.mov_reg_imm64(Reg::R14, 0xFFFFFFFFFFFFFFFFULL); // -1

                    // 1-based start (classic MID$ convention): start==0 is out of range -> force
                    // an empty result by zeroing the copy count and skipping the skip-loop
                    // entirely, rather than underflowing "start - 1".
                    result.text.cmp_reg_imm32(Reg::R9, 0);
                    const std::size_t start_zero_disp = result.text.jcc_rel32_placeholder(0x4); // JE -> force-empty
                    result.text.add_reg_reg(Reg::R9, Reg::R14); // R9 = start - 1 (0-based skip count)

                    const std::size_t skip_loop_start = result.text.size();
                    result.text.cmp_reg_imm32(Reg::R9, 0);
                    const std::size_t skip_done_disp = result.text.jcc_rel32_placeholder(0x4); // JE -> clamp/copy, R12 untouched
                    result.text.mov_reg_reg(Reg::RAX, Reg::R8);
                    result.text.mov_load16_rax();
                    result.text.cmp_reg_imm32(Reg::RAX, 0);
                    const std::size_t skip_exhausted_disp = result.text.jcc_rel32_placeholder(0x4); // JE -> force-empty (string shorter than start)
                    result.text.mov_reg_imm64(Reg::RCX, 2);
                    result.text.add_reg_reg(Reg::R8, Reg::RCX);
                    result.text.add_reg_reg(Reg::R9, Reg::R14);
                    result.text.jmp_rel8(static_cast<std::int8_t>(
                        static_cast<std::int64_t>(skip_loop_start) - static_cast<std::int64_t>(result.text.size() + 2)));

                    // Landing point for BOTH "start==0" and "skip exhausted the source string
                    // early": force the copy count to 0 and fall straight through into
                    // clamp/copy, whose own `length==0` check then naturally produces an empty
                    // (just null-terminated) result with no separate code path needed.
                    const std::size_t force_empty_start = result.text.size();
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(start_zero_disp + 4);
                        result.text.patch_i32(start_zero_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(force_empty_start) - next_instruction));
                    }
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(skip_exhausted_disp + 4);
                        result.text.patch_i32(skip_exhausted_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(force_empty_start) - next_instruction));
                    }
                    result.text.mov_reg_imm64(Reg::R12, 0);

                    const std::size_t clamp_start = result.text.size();
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(skip_done_disp + 4);
                        result.text.patch_i32(skip_done_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(clamp_start) - next_instruction));
                    }
                    result.text.cmp_reg_imm32(Reg::R12, static_cast<std::uint32_t>(kMidResultMaxUnits));
                    const std::size_t clamp_ok_disp = result.text.jcc_rel32_placeholder(0x6); // JBE (unsigned <=)
                    result.text.mov_reg_imm64(Reg::R12, kMidResultMaxUnits);
                    const std::size_t clamp_done_start = result.text.size();
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(clamp_ok_disp + 4);
                        result.text.patch_i32(clamp_ok_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(clamp_done_start) - next_instruction));
                    }

                    const std::size_t copy_loop_start = result.text.size();
                    result.text.cmp_reg_imm32(Reg::R12, 0);
                    const std::size_t copy_done_disp1 = result.text.jcc_rel32_placeholder(0x4); // JE
                    result.text.mov_reg_reg(Reg::RAX, Reg::R8);
                    result.text.mov_load16_rax();
                    result.text.cmp_reg_imm32(Reg::RAX, 0);
                    const std::size_t copy_done_disp2 = result.text.jcc_rel32_placeholder(0x4); // JE -- source exhausted early
                    result.text.mov_reg_reg(Reg::RCX, Reg::RAX); // RCX = the char just read
                    result.text.mov_reg_reg(Reg::RAX, Reg::R13); // RAX = dest ptr
                    result.text.mov_store16_rax_from_cx();
                    result.text.mov_reg_imm64(Reg::RCX, 2);
                    result.text.add_reg_reg(Reg::R8, Reg::RCX);
                    result.text.add_reg_reg(Reg::R13, Reg::RCX);
                    result.text.add_reg_reg(Reg::R12, Reg::R14); // R12 -= 1
                    result.text.jmp_rel8(static_cast<std::int8_t>(
                        static_cast<std::int64_t>(copy_loop_start) - static_cast<std::int64_t>(result.text.size() + 2)));

                    const std::size_t copy_done_start = result.text.size();
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(copy_done_disp1 + 4);
                        result.text.patch_i32(copy_done_disp1, static_cast<std::int32_t>(static_cast<std::int64_t>(copy_done_start) - next_instruction));
                    }
                    {
                        const std::int64_t next_instruction = static_cast<std::int64_t>(copy_done_disp2 + 4);
                        result.text.patch_i32(copy_done_disp2, static_cast<std::int32_t>(static_cast<std::int64_t>(copy_done_start) - next_instruction));
                    }
                    result.text.mov_reg_imm64(Reg::RCX, 0);
                    result.text.mov_reg_reg(Reg::RAX, Reg::R13);
                    result.text.mov_store16_rax_from_cx(); // null-terminate

                    result.text.mov_reg_imm64(Reg::RAX, kMidResultAddress); // always the fixed base, never the advanced R13
                    if (!store_result(instruction.result, "STRING")) return result;
                    break;
                }
                // LEN(array/tuple/object/bit-vector/range) -- this IS the internal "LEN" call
                // target the array/FOR EACH lowering path synthesizes (see this case's own header
                // comment above), now handled instead of falling through to the declared-function-
                // lookup error below. Gated on the operand's static kind being provably Boxed
                // rather than on operand_types (which this call site never populates), matching
                // every other Phase 2 dispatch decision in this function; a target this analysis
                // can't confirm is Boxed falls through to the same declared-function lookup as
                // before -- EXCEPT a plain hosted STRING operand, handled just below instead of
                // falling all the way through to the generic host-function bridge. A real bug
                // found by direct testing, not assumed: LEN's own classification (elsewhere in this
                // file) always assumes a Number return regardless of which code path actually
                // handles a given call site -- if a String-classified operand fell through to the
                // generic bridge (which always returns Boxed), the caller would misread that Boxed
                // pointer's own bit pattern as if it were already a raw double, printing garbage
                // instead of the real length. Boxed fresh here (LEN itself never receives an
                // already-Boxed string from a plain hosted variable) and released immediately after,
                // the same "temp built only for one call" discipline as every other freshly-boxed-
                // scalar site in this function.
                if (convention == systems::CallingConvention::SystemV && upper_call_target == "LEN" &&
                    instruction.operands.size() == 1) {
                    const HostedValueKind operand_kind = infer_hosted_value_kind(module, *target, instruction.operands.front());
                    if (operand_kind == HostedValueKind::Boxed) {
                        if (!load_value(instruction.operands[0], "STRING", Reg::RDI)) return result;
                        const auto call_disp = result.text.call_rel32_placeholder();
                        result.external_calls.push_back({call_disp, "arco_value_length"});
                        if (!store_result_double(instruction.result, Xmm::XMM0)) return result;
                        break;
                    }
                    if (operand_kind == HostedValueKind::String) {
                        if (!load_value(instruction.operands[0], "STRING", Reg::RDI)) return result;
                        {
                            const auto call_disp = result.text.call_rel32_placeholder();
                            result.external_calls.push_back({call_disp, "arco_value_new_string_utf16"});
                        }
                        result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);
                        result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                        {
                            const auto call_disp = result.text.call_rel32_placeholder();
                            result.external_calls.push_back({call_disp, "arco_value_length"});
                        }
                        result.text.movsd_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8), Xmm::XMM0);
                        result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                        {
                            const auto release_disp = result.text.call_rel32_placeholder();
                            result.external_calls.push_back({release_disp, "arco_value_release"});
                        }
                        result.text.movsd_load_disp32(Xmm::XMM0, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8));
                        if (!store_result_double(instruction.result, Xmm::XMM0)) return result;
                        break;
                    }
                }

                const AmirFunction* callee = nullptr;
                for (const auto& candidate : module.functions) {
                    if (candidate.name == instruction.target) { callee = &candidate; break; }
                }
                // Instance method dispatch (Phase 2 classes, RFC-0049 Section 4):
                // `receiver.Method(...)` where `receiver` is a plain local/parameter holding a
                // class instance (an Object with "__class" -- see lower_class's own .__new). No
                // function is EVER literally named "<variable>.<Method>" (unlike a fully
                // class-qualified call, e.g. SUPER's own "ParentClass.Method", which the direct
                // module.functions lookup just above already finds -- that's not a variable name,
                // it's a real function name), so this only fires once the direct lookup has
                // already failed. ArcoBASIC is dynamically typed, so the receiver's actual runtime
                // type (and therefore which class's method body actually runs, under EXTENDS
                // polymorphism) can't be known until its own "__class" field is read -- this
                // replicates the exact algorithm the bytecode VM's own instance-method dispatch
                // (BytecodeOp::CallValue, "Instance method dispatch" comment) already uses, with
                // the class hierarchy itself (module.class_parents, static) resolved at compile
                // time via resolve_class_method and only the receiver's actual runtime "__class"
                // STRING CONTENT checked at runtime (arco_value_string_equals_utf16).
                //
                // Scope reduction, disclosed: only a SINGLE-segment receiver is attempted here
                // (`receiver.Method`, exactly one dot) -- a chained receiver path
                // (`a.b.Method`, intermediate field access before the method) is real,
                // unattempted future work, matching the bytecode VM's own more general case that
                // this backend doesn't replicate in full. The receiver must be a local
                // variable/parameter (has its own stack slot) -- a GLOBAL-only receiver (a
                // SHARED-field host, or a script-scope global never locally shadowed) is also real,
                // disclosed, unattempted future work; this backend has no Runtime.SetGlobal/
                // GetGlobal codegen at all yet.
                if (!callee && convention == systems::CallingConvention::SystemV) {
                    const auto last_dot = instruction.target.rfind('.');
                    // Chained receiver path (`a.b.Method(...)`, `a.b.c.Method(...)`, ...): the
                    // method name always comes from the LAST dot (`a.b.c(...)` calls method `c` on
                    // receiver path `a.b`, matching the parser's own MethodCallExpr construction --
                    // see parser.cpp's identical comment), so everything before it is the receiver
                    // PATH, which may itself contain further dots (intermediate field reads before
                    // the actual method call, not additional method calls). `receiver_fields` is
                    // that path's own dot-split segments after the base variable -- empty for the
                    // common `receiver.Method(...)` case (exactly one dot total), one segment for
                    // `a.b.Method(...)`, and so on. The base must still be a plain local/parameter
                    // (has its own stack slot) -- a GLOBAL-only receiver (a SHARED-field host, or a
                    // script-scope global never locally shadowed) remains real, disclosed,
                    // unattempted future work; this backend has no Runtime.SetGlobal/GetGlobal
                    // codegen reachable from a bare receiver name at all yet.
                    std::string base_name;
                    std::vector<std::string> receiver_fields;
                    std::string method_name;
                    if (last_dot != std::string::npos) {
                        const std::string receiver_path = instruction.target.substr(0, last_dot);
                        method_name = instruction.target.substr(last_dot + 1);
                        const auto first_dot = receiver_path.find('.');
                        base_name = first_dot == std::string::npos ? receiver_path : receiver_path.substr(0, first_dot);
                        if (first_dot != std::string::npos) {
                            std::size_t segment_start = first_dot + 1;
                            while (true) {
                                const auto next_dot = receiver_path.find('.', segment_start);
                                receiver_fields.push_back(receiver_path.substr(
                                    segment_start, next_dot == std::string::npos ? std::string::npos : next_dot - segment_start));
                                if (next_dot == std::string::npos) break;
                                segment_start = next_dot + 1;
                            }
                        }
                    }
                    if (last_dot != std::string::npos && slot_of(base_name) >= 0) {
                        std::vector<std::pair<std::string, std::string>> candidates; // {class_name, resolved_function_name}
                        for (const auto& entry : module.class_parents) {
                            auto resolved_name = resolve_class_method(module, entry.first, method_name);
                            if (resolved_name) candidates.emplace_back(entry.first, *resolved_name);
                        }
                        if (!candidates.empty()) {
                            // Resolve the ACTUAL receiver into a known scratch slot (scratch_base +
                            // 16, distinct from scratch_base/scratch_base+8's own use just below)
                            // uniformly, whether it's the base variable's own value directly (no
                            // intermediate fields -- the common, non-chained case) or the end of a
                            // field-read chain -- everything downstream (the __class fetch, and the
                            // eventual call itself) reads from this ONE place regardless, so no
                            // separate code path is needed for the two shapes.
                            if (!load_value(base_name, "STRING", Reg::RDI)) return result;
                            result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base + 16), Reg::RDI);
                            for (std::size_t i = 0; i < receiver_fields.size(); ++i) {
                                std::vector<char16_t> encoded;
                                try {
                                    encoded = systems::encode_utf16_null_terminated(receiver_fields[i]);
                                } catch (const std::exception& error) {
                                    result.ok = false;
                                    result.error = "receiver field \"" + receiver_fields[i] + "\" cannot be encoded as UTF-16: " + error.what();
                                    return result;
                                }
                                const std::size_t data_offset = result.rdata.size();
                                for (char16_t unit : encoded) {
                                    result.rdata.push_back(static_cast<std::uint8_t>(unit & 0xFF));
                                    result.rdata.push_back(static_cast<std::uint8_t>((unit >> 8) & 0xFF));
                                }
                                result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 16));
                                {
                                    const std::size_t disp_offset = result.text.lea_rip_relative(Reg::RSI);
                                    result.relocations.push_back({disp_offset, result.text.size(), data_offset});
                                }
                                {
                                    const auto call_disp = result.text.call_rel32_placeholder();
                                    result.external_calls.push_back({call_disp, "arco_value_object_get"});
                                }
                                // The new receiver (RAX) must survive the release call just below --
                                // an ordinary System V call is free to clobber RAX internally even
                                // though arco_value_release returns void, the same "spill before
                                // releasing" discipline Kind::StoreIndex's own descent loop follows.
                                result.text.mov_reg_reg(Reg::R10, Reg::RAX);
                                // Release the receiver this step just read PAST -- the very first
                                // one (i == 0) is the base variable's own borrowed value and must
                                // NOT be released; every one after that is a fresh reference
                                // (arco_value_object_get's own contract) this loop's own previous
                                // iteration created and is now done with.
                                if (i > 0) release_scratch_temp(static_cast<std::uint32_t>(scratch_base + 16));
                                result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base + 16), Reg::R10);
                            }
                            // Fetch the resolved receiver's own "__class" field ONCE, spilled for
                            // reuse across every candidate's comparison below (each comparison call
                            // clobbers RDI/RSI for its own arguments, so both the receiver pointer
                            // and the class-name pointer need to be reloaded from scratch, not kept
                            // live in a register, across the whole dispatch sequence).
                            result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 16));
                            {
                                std::vector<char16_t> encoded;
                                try {
                                    encoded = systems::encode_utf16_null_terminated("__class");
                                } catch (const std::exception& error) {
                                    result.ok = false;
                                    result.error = std::string("\"__class\" cannot be encoded as UTF-16: ") + error.what();
                                    return result;
                                }
                                const std::size_t data_offset = result.rdata.size();
                                for (char16_t unit : encoded) {
                                    result.rdata.push_back(static_cast<std::uint8_t>(unit & 0xFF));
                                    result.rdata.push_back(static_cast<std::uint8_t>((unit >> 8) & 0xFF));
                                }
                                const std::size_t disp_offset = result.text.lea_rip_relative(Reg::RSI);
                                result.relocations.push_back({disp_offset, result.text.size(), data_offset});
                            }
                            {
                                const auto call_disp = result.text.call_rel32_placeholder();
                                result.external_calls.push_back({call_disp, "arco_value_object_get"});
                            }
                            result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8), Reg::RAX);

                            // call_resolved_method (hoisted to the top of this whole
                            // Kind::CallValue case) marshals the resolved receiver directly from
                            // scratch_base+16 (see its own receiver_scratch_offset parameter --
                            // there is no AMIR name for a chained/resolved receiver to look up by,
                            // unlike the single-segment case's own `receiver_name`) plus the rest of
                            // `instruction.operands` exactly like an ordinary declared-function
                            // call, against `resolved`'s own declared parameter list. args[0] is an
                            // unused placeholder -- see call_resolved_method's own comment.
                            std::vector<std::string> call_args{""};
                            for (const auto& operand : instruction.operands) call_args.push_back(operand);

                            std::vector<std::size_t> done_jumps;
                            for (const auto& [class_name, resolved_name] : candidates) {
                                const AmirFunction* resolved = nullptr;
                                for (const auto& candidate_function : module.functions) {
                                    if (candidate_function.name == resolved_name) { resolved = &candidate_function; break; }
                                }
                                if (!resolved) continue; // resolve_class_method only ever returns a name it found -- defensive only

                                result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8));
                                {
                                    std::vector<char16_t> encoded;
                                    try {
                                        encoded = systems::encode_utf16_null_terminated(class_name);
                                    } catch (const std::exception& error) {
                                        result.ok = false;
                                        result.error = "class name \"" + class_name + "\" cannot be encoded as UTF-16: " + error.what();
                                        return result;
                                    }
                                    const std::size_t data_offset = result.rdata.size();
                                    for (char16_t unit : encoded) {
                                        result.rdata.push_back(static_cast<std::uint8_t>(unit & 0xFF));
                                        result.rdata.push_back(static_cast<std::uint8_t>((unit >> 8) & 0xFF));
                                    }
                                    const std::size_t disp_offset = result.text.lea_rip_relative(Reg::RSI);
                                    result.relocations.push_back({disp_offset, result.text.size(), data_offset});
                                }
                                {
                                    const auto call_disp = result.text.call_rel32_placeholder();
                                    result.external_calls.push_back({call_disp, "arco_value_string_equals_utf16"});
                                }
                                result.text.cmp_reg_imm32(Reg::RAX, 0);
                                const std::size_t no_match_disp = result.text.jcc_rel32_placeholder(0x4); // JE -> next candidate

                                // arco_value_object_get (the receiver's own "__class" field, fetched
                                // ONCE above and reused across every candidate's comparison) returns
                                // a NEW owned reference every time -- reused across comparisons, but
                                // never itself stored into a named local this compiler's own
                                // reference-lifetime tracking (Kind::Store/store_result/Kind::Return)
                                // would ever see, so it leaked on every successful dispatch until
                                // this fix (RFC-0049, "full Linux support" pass -- found by direct
                                // measurement: a persistent instance's method called in a tight loop
                                // showed steady linear RSS growth even after every other lifetime fix
                                // in this same pass). Released exactly once, here, the moment a match
                                // is confirmed -- past this point no other candidate's comparison
                                // will re-read it, so this is its last use.
                                release_scratch_temp(static_cast<std::uint32_t>(scratch_base + 8));
                                if (!call_resolved_method(*resolved, call_args, static_cast<std::uint32_t>(scratch_base + 16))) return result;
                                // The resolved receiver itself (scratch_base+16) is released the
                                // same way -- but ONLY if it's a fresh reference this dispatch's own
                                // field-read chain created (receiver_fields non-empty); the common,
                                // non-chained case leaves it holding the base variable's own
                                // borrowed value, never released here (matching the pre-existing
                                // single-segment behavior exactly).
                                if (!receiver_fields.empty()) release_scratch_temp(static_cast<std::uint32_t>(scratch_base + 16));
                                const std::size_t done_disp = result.text.jmp_rel32_placeholder();
                                done_jumps.push_back(done_disp);

                                const std::size_t no_match_start = result.text.size();
                                {
                                    const std::int64_t next_instruction = static_cast<std::int64_t>(no_match_disp + 4);
                                    result.text.patch_i32(no_match_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(no_match_start) - next_instruction));
                                }
                            }
                            // No candidate's class matched the receiver's actual runtime __class --
                            // an instance of some class not declaring (directly or via
                            // inheritance) this method at all. Panics rather than silently
                            // returning null/undefined the way a missing object property does
                            // NOT (see arco_value_object_get) -- a method call with no matching
                            // implementation is a real program error, not a "field happens to be
                            // absent" case.
                            {
                                const std::string message = "no method \"" + method_name + "\" found on this instance's class";
                                const std::size_t data_offset = result.rdata.size();
                                for (char c : message) result.rdata.push_back(static_cast<std::uint8_t>(c));
                                result.rdata.push_back(0);
                                const std::size_t disp_offset = result.text.lea_rip_relative(Reg::RDI);
                                result.relocations.push_back({disp_offset, result.text.size(), data_offset});
                            }
                            {
                                const auto call_disp = result.text.call_rel32_placeholder();
                                result.external_calls.push_back({call_disp, "arco_value_panic"});
                            }
                            const std::size_t after_dispatch = result.text.size();
                            for (const std::size_t done_disp : done_jumps) {
                                const std::int64_t next_instruction = static_cast<std::int64_t>(done_disp + 4);
                                result.text.patch_i32(done_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(after_dispatch) - next_instruction));
                            }
                            break;
                        }
                    }
                }
                // ADDRESSOF/CALLABLE dispatch (RFC-0049): `f(...)` where `f` is a plain local
                // variable/parameter (has its own stack slot) holding a value produced by
                // `Kind::AddressOf` (see that case's own comment -- boxed there as a plain string
                // naming the referenced function). A host function name is NEVER reached through a
                // variable slot (Runtime.*/LEN/host-bridge calls all name their target directly in
                // instruction.target's own text), so "does this name resolve to a local slot" is an
                // unambiguous discriminator between "this is a callable stored in a variable" and
                // "this is a plain call by name" -- matching the bytecode VM's own dispatch order
                // exactly (BytecodeOp::CallValue checks a local/global callable binding BEFORE ever
                // falling back to a host function). The candidate set is every DISTINCT function
                // name referenced via ADDRESSOF anywhere in the whole module (bounded and known at
                // compile time, exactly like resolve_class_method's own class-hierarchy walk) --
                // each compared, at runtime, against the callable's own boxed name
                // (arco_value_string_equals_utf16), with the matching candidate's own declared
                // parameter types (not the caller's) driving argument classification, reusing
                // call_resolved_method exactly as instance dispatch does. Scope reduction,
                // disclosed: only a plain function reference is supported (no bound instance
                // methods, no host-function callables -- ADDRESSOF's own codegen already rejects
                // a dotted target before a callable value like that could ever be produced here).
                if (!callee && convention == systems::CallingConvention::SystemV && slot_of(instruction.target) >= 0) {
                    std::vector<std::string> callable_candidates;
                    for (const auto& candidate_function : module.functions) {
                        for (const auto& block : candidate_function.blocks) {
                            for (const auto& candidate_instruction : block.instructions) {
                                if (candidate_instruction.kind != AmirInstruction::Kind::AddressOf) continue;
                                if (std::find(callable_candidates.begin(), callable_candidates.end(),
                                              candidate_instruction.target) != callable_candidates.end()) continue;
                                // Skip a candidate that could NEVER actually be the function
                                // `instruction.target` resolves to at runtime, REGARDLESS of what
                                // the runtime string comparison below would say -- attempting to
                                // marshal instruction.operands against it would just hard-fail this
                                // compile (call_resolved_method's own validation has no way to know
                                // a candidate it's asked to marshal against is statically
                                // unreachable at THIS call site, it just reports the mismatch as a
                                // real error). Two real bugs caught by direct testing, both the
                                // same underlying shape: a module with a 1-argument callable
                                // (`Square`) and a 0-argument one (`Hello`) failed to compile `f(5)`
                                // entirely ("call to \"Hello\" expects 0 arguments, got 1") even
                                // though `f` never held `Hello` at that call site; separately, two
                                // 1-argument callables where one's parameter is untyped (assumed
                                // hosted-number) and the other's is `AS STRING` failed
                                // ("...passes a string argument to parameter \"x\"...") the same
                                // way when the STRING candidate was tried against the untyped one.
                                // Checked here BEFORE ever attempting to marshal: arity first, then
                                // (for a same-arity candidate) each argument's own kind against
                                // that parameter's own hosted-number-vs-not classification, the
                                // identical rule call_resolved_method's own untyped-parameter safety
                                // check applies internally.
                                const AmirFunction* candidate_target = nullptr;
                                for (const auto& resolved_candidate : module.functions) {
                                    if (resolved_candidate.name == candidate_instruction.target) { candidate_target = &resolved_candidate; break; }
                                }
                                if (!candidate_target || candidate_target->params.size() != instruction.operands.size()) continue;
                                bool candidate_could_match = true;
                                for (std::size_t i = 0; candidate_could_match && i < instruction.operands.size(); ++i) {
                                    if (!param_is_hosted_number(candidate_target->params[i])) continue;
                                    if (!declared_parameter_type(candidate_target->params[i]).empty()) continue;
                                    const HostedValueKind argument_kind = infer_hosted_value_kind(module, *target, instruction.operands[i]);
                                    if (argument_kind == HostedValueKind::String || argument_kind == HostedValueKind::Bool ||
                                        argument_kind == HostedValueKind::Boxed) {
                                        candidate_could_match = false;
                                    }
                                }
                                if (!candidate_could_match) continue;
                                callable_candidates.push_back(candidate_instruction.target);
                            }
                        }
                    }
                    {
                        std::vector<std::size_t> done_jumps;
                        if (!callable_candidates.empty()) {
                        if (!load_value(instruction.target, "STRING", Reg::RDI)) return result;
                        result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RDI);

                        for (const auto& candidate_name : callable_candidates) {
                            const AmirFunction* resolved = nullptr;
                            for (const auto& candidate_function : module.functions) {
                                if (candidate_function.name == candidate_name) { resolved = &candidate_function; break; }
                            }
                            if (!resolved) continue; // defensive only -- every candidate came from module.functions itself

                            result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                            {
                                std::vector<char16_t> encoded;
                                try {
                                    encoded = systems::encode_utf16_null_terminated(candidate_name);
                                } catch (const std::exception& error) {
                                    result.ok = false;
                                    result.error = "callable name \"" + candidate_name + "\" cannot be encoded as UTF-16: " + error.what();
                                    return result;
                                }
                                const std::size_t data_offset = result.rdata.size();
                                for (char16_t unit : encoded) {
                                    result.rdata.push_back(static_cast<std::uint8_t>(unit & 0xFF));
                                    result.rdata.push_back(static_cast<std::uint8_t>((unit >> 8) & 0xFF));
                                }
                                const std::size_t disp_offset = result.text.lea_rip_relative(Reg::RSI);
                                result.relocations.push_back({disp_offset, result.text.size(), data_offset});
                            }
                            {
                                const auto call_disp = result.text.call_rel32_placeholder();
                                result.external_calls.push_back({call_disp, "arco_value_string_equals_utf16"});
                            }
                            result.text.cmp_reg_imm32(Reg::RAX, 0);
                            const std::size_t no_match_disp = result.text.jcc_rel32_placeholder(0x4); // JE -> next candidate

                            if (!call_resolved_method(*resolved, instruction.operands)) return result;
                            const std::size_t done_disp = result.text.jmp_rel32_placeholder();
                            done_jumps.push_back(done_disp);

                            const std::size_t no_match_start = result.text.size();
                            {
                                const std::int64_t next_instruction = static_cast<std::int64_t>(no_match_disp + 4);
                                result.text.patch_i32(no_match_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(no_match_start) - next_instruction));
                            }
                        }
                        }
                        // No candidate matched -- either `f` holds something that isn't a callable
                        // this analysis could enumerate (e.g. a stale/uninitialized value), or
                        // callable_candidates was empty from the start (no ADDRESSOF anywhere in the
                        // module could ever match this call site's own arity/argument types, or no
                        // ADDRESSOF exists at all). Panics UNCONDITIONALLY in either case, rather
                        // than falling through to the generic host-function bridge below: a bare
                        // identifier that resolves to a local variable's own stack slot (this whole
                        // block's own gating condition) is never a host-function name -- see this
                        // block's opening comment -- so there is no correct fallback interpretation
                        // left to try. A real bug found by direct testing: when callable_candidates
                        // ended up empty, this whole block previously did nothing at all and control
                        // fell through to the host bridge below, which misreported the failure as
                        // "unknown host function: f" instead of this diagnostic.
                        {
                            static const char kMessage[] = "value is not a callable this backend can resolve";
                            const std::size_t data_offset = result.rdata.size();
                            for (char c : std::string(kMessage)) result.rdata.push_back(static_cast<std::uint8_t>(c));
                            result.rdata.push_back(0);
                            const std::size_t disp_offset = result.text.lea_rip_relative(Reg::RDI);
                            result.relocations.push_back({disp_offset, result.text.size(), data_offset});
                        }
                        {
                            const auto call_disp = result.text.call_rel32_placeholder();
                            result.external_calls.push_back({call_disp, "arco_value_panic"});
                        }
                        const std::size_t after_dispatch = result.text.size();
                        for (const std::size_t done_disp : done_jumps) {
                            const std::int64_t next_instruction = static_cast<std::int64_t>(done_disp + 4);
                            result.text.patch_i32(done_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(after_dispatch) - next_instruction));
                        }
                        break;
                    }
                }
                // Generic host-function bridge fallback (RFC-0049, "finish off Linux support"):
                // reached once a direct declared-function lookup AND instance-method dispatch have
                // both missed. `instruction.target` names one of arco::Runtime's own ~244 host
                // functions (UPPER, String.Split, Array.Join, Format, and everything else this
                // backend doesn't hand-roll dedicated native codegen for) far more often than it
                // names a genuinely undeclared call -- so rather than failing to compile outright,
                // this boxes every argument into the per-function scratch buffer sized above
                // (host_args_base/max_host_call_args) and calls arco_call_host, which does the
                // actual dispatch against a real arco::Runtime. An UNRECOGNIZED name still fails,
                // just at RUNTIME instead of compile time now (arco_call_host's own "unknown host
                // function" panic) -- matching how the interpreter/bytecode VM themselves only ever
                // discover an unknown host function at runtime too, never statically.
                if (!callee && convention == systems::CallingConvention::SystemV) {
                    std::vector<bool> host_arg_freshly_boxed(instruction.operands.size(), false);
                    for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                        if (!box_operand_into_rax(instruction.operands[i])) return result;
                        host_arg_freshly_boxed[i] = box_operand_freshly_boxed;
                        const std::uint32_t slot_offset = static_cast<std::uint32_t>(host_args_base + 8 * static_cast<int>(i));
                        result.text.mov_store_disp32(Reg::RSP, slot_offset, Reg::RAX);
                    }
                    std::vector<char16_t> encoded;
                    try {
                        encoded = systems::encode_utf16_null_terminated(instruction.target);
                    } catch (const std::exception& error) {
                        result.ok = false;
                        result.error = std::string("host function name cannot be encoded as UTF-16: ") + error.what();
                        return result;
                    }
                    const std::size_t data_offset = result.rdata.size();
                    for (char16_t unit : encoded) {
                        result.rdata.push_back(static_cast<std::uint8_t>(unit & 0xFF));
                        result.rdata.push_back(static_cast<std::uint8_t>((unit >> 8) & 0xFF));
                    }
                    {
                        const std::size_t disp_offset = result.text.lea_rip_relative(Reg::RDI);
                        result.relocations.push_back({disp_offset, result.text.size(), data_offset});
                    }
                    if (instruction.operands.empty()) {
                        result.text.mov_reg_imm64(Reg::RSI, 0);
                    } else {
                        result.text.lea_rsp_disp32(Reg::RSI, static_cast<std::uint32_t>(host_args_base));
                    }
                    result.text.mov_reg_imm64(Reg::RDX, static_cast<std::uint64_t>(instruction.operands.size()));
                    {
                        const auto call_disp = result.text.call_rel32_placeholder();
                        result.external_calls.push_back({call_disp, "arco_call_host"});
                    }
                    if (!store_result(instruction.result, "STRING")) return result;
                    // arco_call_host copies every argument's own content into its internal
                    // std::vector<arco::Value> (see host_bridge.cpp) -- it never takes ownership of
                    // any ArcoValue* in the array it's handed, so every argument THIS call site
                    // freshly boxed just to satisfy that ABI (an existing local's own reference is
                    // left alone) must be released here, the single highest-traffic leak site this
                    // pass closes: any host function call at all with a plain scalar argument (e.g.
                    // `UPPER(name)` in a loop) went through this path.
                    for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                        if (!host_arg_freshly_boxed[i]) continue;
                        release_scratch_temp(static_cast<std::uint32_t>(host_args_base + 8 * static_cast<int>(i)));
                    }
                    break;
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
                // System V: a call to a genuinely hosted (non-freestanding) ArcoBASIC function --
                // this backend's own real general-function-call support, distinct from the
                // freestanding-only positional/GPR-only path just below it (still exactly as it
                // was before this milestone, unchanged for Microsoft x64). Each argument is
                // classified by the CALLEE's own declared parameter type (declared_parameter_type +
                // is_hosted_number, the exact same rule generate_x86_64_function's own prologue
                // uses for that function's parameters) so caller and callee always agree on which
                // register class -- GPR or XMM -- each argument travels in, without needing real
                // static type-checking anywhere else in this compiler. Arguments/return values
                // needing boxing (arrays, objects, or anything this straight-line analysis can't
                // resolve -- see infer_hosted_value_kind's own comment) are real, disclosed,
                // unattempted future work; see RFC-0049 and .agents/ARCO_NATIVE_RUNTIME_PROGRESS.md.
                if (convention == systems::CallingConvention::SystemV) {
                    // No stack-spilled call arguments on this path yet -- a real, disclosed scope
                    // reduction matching the prologue's own limit: a call passing more than 6
                    // integer-class or 8 float-class arguments fails to compile with a clear error
                    // rather than mishandling stack layout.
                    const auto& int_regs = systems::sysv_integer_argument_registers();
                    // Every argument gets its own dedicated scratch slot in host_args_base (sized
                    // for this function's own largest CallValue operand list regardless of dispatch
                    // path -- max_host_call_args, see its own comment above) before any of them are
                    // loaded into a real calling-convention register -- see call_resolved_method's
                    // own identical two-pass comment (RFC-0049, "full Linux support" pass) for the
                    // full reasoning: a LATER argument's own unboxing (a real external call) is free
                    // to clobber an EARLIER argument's already-finalized register otherwise. The
                    // exact same bug, found the exact same way, in this SEPARATE marshaling loop --
                    // `Combine(a.Get(), b.Get())`-shaped plain calls (two Boxed-classified, method-
                    // call-derived arguments) printed a denormal garbage double instead of the
                    // correct sum.
                    // Tracks which STRING-typed arguments (see the "param_type == STRING" branch
                    // below) were freshly boxed just for this call, so they can be released after
                    // the call returns (same shape as call_resolved_method's own identical vector).
                    std::vector<bool> string_arg_freshly_boxed(instruction.operands.size(), false);
                    for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                        const std::string param_type = declared_parameter_type(callee->params[i]);
                        const std::uint32_t slot_offset = static_cast<std::uint32_t>(host_args_base + 8 * static_cast<int>(i));
                        if (param_is_hosted_number(callee->params[i])) {
                            // An UNTYPED parameter (param_type.empty()) is assumed to be a
                            // hosted-number -- the common case (see is_hosted_number's own
                            // comment) -- but ArcoBASIC itself has no static parameter typing, so
                            // that assumption is only actually safe when the argument being passed
                            // at THIS call site can't be proven otherwise. A real bug caught by
                            // direct testing: `FUNCTION Shout(s): PRINT s` compiled fine (s is
                            // untyped, defaults to hosted-number) but calling `Shout("hi")`
                            // silently reinterpreted the passed string's raw pointer bits as a
                            // double instead of erroring ("4.64584e-310" printed instead of "hi") --
                            // both `mov` and `movsd` move the same 8 raw bytes with no conversion,
                            // so the value crossed the call correctly, only to be misclassified on
                            // the far side. Caught here instead: a caller-side argument this
                            // analysis can positively prove is String/Bool is rejected with a clear
                            // error rather than silently misrouted through the XMM/number path.
                            // Real support for a parameter whose type varies by call site needs
                            // boxing (Phase 2, ArcoValue) and is real, disclosed, unattempted future
                            // work -- see RFC-0049. An EXPLICITLY "AS NUMBER"-typed parameter skips
                            // this check (its type is already pinned, not "assumed"), but still
                            // needs load_double_operand below rather than a raw load: a real,
                            // separate bug found the same way as the register-clobber one above --
                            // a Boxed argument passed to an explicitly-typed hosted-number parameter
                            // was never actually unboxed at all, just read as if its own pointer
                            // bits were already a double.
                            if (param_type.empty()) {
                                const HostedValueKind argument_kind = infer_hosted_value_kind(module, *target, instruction.operands[i]);
                                // Boxed (an array/object -- see Phase 2, RFC-0049 Section 4) is
                                // rejected here for the identical reason: an untyped parameter
                                // assumes hosted-number, and a Boxed argument is a pointer, not a
                                // raw double, that would otherwise be silently misrouted through
                                // the XMM/number path the same way a string once was.
                                if (argument_kind == HostedValueKind::String || argument_kind == HostedValueKind::Bool ||
                                    argument_kind == HostedValueKind::Boxed) {
                                    const char* kind_name = argument_kind == HostedValueKind::String ? "string" :
                                        argument_kind == HostedValueKind::Bool ? "bool" : "array/object";
                                    const char* suggested_type = argument_kind == HostedValueKind::String ? "STRING" :
                                        argument_kind == HostedValueKind::Bool ? "BOOL" : "ARRAY";
                                    result.ok = false;
                                    result.error = "call to \"" + instruction.target + "\" passes a " + kind_name +
                                        " argument to parameter \"" + bare_parameter_name(callee->params[i]) + "\", which has "
                                        "no explicit type annotation; this backend's System V fast path assumes an "
                                        "untyped parameter is a number -- declare it \"AS " + suggested_type + "\" explicitly "
                                        "to pass a non-numeric argument";
                                    return result;
                                }
                            }
                            if (!load_double_operand(instruction.operands[i], Xmm::XMM0)) return result;
                            result.text.movsd_store_disp32(Reg::RSP, slot_offset, Xmm::XMM0);
                        } else if (param_type == "STRING") {
                            // A STRING-typed parameter's ACTUAL physical representation is
                            // genuinely ambiguous at this call site: Kind::Const's own
                            // string-literal codegen produces a raw, un-owned rip-relative UTF-16
                            // pointer (HostedValueKind::String), while everything else that
                            // produces a string -- concatenation, a generic host-function result,
                            // an object field -- produces a real, refcounted ArcoValueBox*
                            // (HostedValueKind::Boxed). A plain bit-copy here would leave the
                            // CALLEE unable to tell which one it received, and infer_local_kind's
                            // own answer for a STRING-typed parameter is Boxed unconditionally (see
                            // its comment) -- a real bug found by direct testing: Arconaut's own
                            // three-level-deep string pass-through (`Tab(..., "tab-" +
                            // Lower(label), ...)` -> `Button` -> `RegisterRegion`) fed a genuine
                            // boxed concatenation result into a STRING parameter, and the callee's
                            // own box_operand_into_rax then re-boxed that ArcoValueBox POINTER as
                            // if it were itself a raw UTF-16 buffer address -- Unicode mojibake,
                            // not a crash, since the garbage address usually happened to be
                            // readable memory. Boxed here via box_operand_into_rax (the same
                            // "make me a real boxed pointer out of ANY operand" helper
                            // Array/Object/StoreIndex/Kind::Store's own ambiguous-source fix
                            // already trust) so every STRING-typed parameter ALWAYS receives a
                            // genuine ArcoValueBox*, matching what the callee now assumes.
                            if (!box_operand_into_rax(instruction.operands[i])) return result;
                            string_arg_freshly_boxed[i] = box_operand_freshly_boxed;
                            result.text.mov_store_disp32(Reg::RSP, slot_offset, Reg::RAX);
                        } else {
                            const std::string load_type = param_type.empty() ? "U64" : param_type;
                            if (!load_value(instruction.operands[i], load_type, Reg::RAX)) return result;
                            result.text.mov_store_disp32(Reg::RSP, slot_offset, Reg::RAX);
                        }
                    }
                    int int_arg_index = 0;
                    int float_arg_index = 0;
                    for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                        const std::uint32_t slot_offset = static_cast<std::uint32_t>(host_args_base + 8 * static_cast<int>(i));
                        if (param_is_hosted_number(callee->params[i])) {
                            if (float_arg_index >= 8) {
                                result.ok = false;
                                result.error = "call to \"" + instruction.target + "\" passes more "
                                    "hosted-number arguments than this backend's System V fast path supports";
                                return result;
                            }
                            result.text.movsd_load_disp32(static_cast<Xmm>(float_arg_index++), Reg::RSP, slot_offset);
                        } else {
                            if (int_arg_index >= static_cast<int>(int_regs.size())) {
                                result.ok = false;
                                result.error = "call to \"" + instruction.target + "\" passes more "
                                    "integer/pointer arguments than this backend's System V fast path supports";
                                return result;
                            }
                            result.text.mov_load_disp32(kRegisterByName.at(int_regs[static_cast<std::size_t>(int_arg_index++)]),
                                                         Reg::RSP, slot_offset);
                        }
                    }
                    {
                        const auto call_disp = result.text.call_rel32_placeholder();
                        result.internal_calls.push_back({call_disp, instruction.target});
                    }
                    // The callee's own AmirFunction::return_type is a generic "VALUE" placeholder
                    // (ArcoBASIC has no static return-type annotation -- see the Return case's own
                    // comment), so which register the result comes back in is resolved the same way
                    // PRINT decides how to box a printed value: static straight-line inference over
                    // the callee's own RETURN (infer_function_return_kind).
                    const HostedValueKind return_kind = infer_function_return_kind(module, *callee);
                    if (return_kind == HostedValueKind::Number) {
                        if (!store_result_double(instruction.result, Xmm::XMM0)) return result;
                    } else if (return_kind == HostedValueKind::Bool) {
                        if (!store_result(instruction.result, "BOOL")) return result;
                    } else if (return_kind == HostedValueKind::String || return_kind == HostedValueKind::Boxed) {
                        // Boxed (an array/object) is already a pointer, exactly like STRING's own
                        // raw pointer -- both are a plain 64-bit passthrough in RAX, no different
                        // handling needed at the call site itself.
                        if (!store_result(instruction.result, "STRING")) return result;
                    } else {
                        result.ok = false;
                        result.error = "call to \"" + instruction.target + "\" has a return value this "
                            "backend's System V fast path can't statically classify (needs a directly "
                            "returned string/number/bool expression, the same limit PRINT's own static "
                            "analysis has)";
                        return result;
                    }
                    // Release any STRING-typed argument freshly boxed just for this call (PASS 1's
                    // own comment) -- deferred until AFTER the return value is already safely
                    // stored, since arco_value_release is a real external call, free to clobber
                    // XMM0/RAX the same way any other call in this file can.
                    for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                        const std::uint32_t slot_offset = static_cast<std::uint32_t>(host_args_base + 8 * static_cast<int>(i));
                        if (string_arg_freshly_boxed[i]) release_scratch_temp(slot_offset);
                    }
                    break;
                }
                const auto call_locations = systems::assign_argument_locations(convention, static_cast<int>(instruction.operands.size()));
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
                        const std::uint32_t outgoing_offset = static_cast<std::uint32_t>(outgoing_base + 8 * (static_cast<int>(i) - register_count));
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

            // PRINT (amir_call("Runtime.Print", ...), lower_print's own only call site -- see
            // amir_call's single use above) is the ONLY producer of Kind::Call. An ordinary
            // user-declared call like `Foo()` lowers through lower_call's amir_call_value instead,
            // i.e. Kind::CallValue (see that case below) -- the two print identically as `CALL ...`
            // in `ArcoFission reveal`'s pretty-printer (both AmirInstruction::Kind cases share the
            // same "CALL " prefix), which is genuinely easy to misread as one AMIR kind covering
            // both; an earlier version of this comment did exactly that and was corrected after
            // checking each case's own real dispatch, not the reveal text, against module.functions.
            // The UEFI target never reaches this case at all: its own console output goes through
            // CallExternal against a real UEFI protocol method (RFC-0007's own fixture), never
            // PRINT/Runtime.Print, so gating on System V specifically leaves Microsoft x64 behavior
            // completely unchanged either way.
            case AmirInstruction::Kind::Call: {
                if (convention != systems::CallingConvention::SystemV || instruction.target != "Runtime.Print" ||
                    instruction.operands.size() != 1) {
                    result.ok = false;
                    result.error = "this milestone's code generator does not support this A-MIR instruction kind";
                    return result;
                }
                // A string literal, a number literal, a variable holding either, or a computed
                // arithmetic expression are all supported -- see infer_hosted_value_kind's own
                // comment for exactly how far this backend's static analysis reaches and where it
                // gives up (arrays/objects/anything not resolvable to a straight-line string-or-
                // number-or-bool origin), which is a real, disclosed limit, not a silent wrong
                // answer: anything it can't classify falls through to the clear error below.
                //
                // Boxed through the native runtime ABI (include/arco/native_runtime_abi.h) rather
                // than three separate hand-rolled per-type print functions (an earlier version of
                // this case, before this backend had ArcoValue at all): arco_value_print calls the
                // one real formatting authority, arco::Value::to_string(), so this can never drift
                // out of sync with how the interpreter/bytecode VM format the same value. The boxed
                // pointer is spilled to this frame's own scratch region (scratch_base -- shared with
                // a couple of Microsoft-x64-only CallExternal cases elsewhere in this function, but
                // never contended: those only ever run under that other convention) between the
                // three calls since nothing survives a CALL in an ordinary register on this backend.
                const HostedValueKind printed_kind = infer_hosted_value_kind(module, *target, instruction.operands.front());
                if (printed_kind == HostedValueKind::Boxed) {
                    // Already an ArcoValue* (an array, an object, or the result of indexing into
                    // either) -- print it directly, no construct/release: this isn't a new
                    // reference PRINT itself created, it's the existing local's own value, so
                    // there is nothing here for PRINT to release.
                    if (!load_value(instruction.operands[0], "STRING", Reg::RDI)) return result;
                    const auto print_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({print_disp, "arco_value_print"});
                    break;
                }
                const char* constructor = nullptr;
                if (printed_kind == HostedValueKind::String) {
                    if (!load_value(instruction.operands[0], "STRING", Reg::RDI)) return result;
                    constructor = "arco_value_new_string_utf16";
                } else if (printed_kind == HostedValueKind::Number) {
                    // System V passes the first floating-point argument in XMM0, not a GPR.
                    if (!load_value_double(instruction.operands[0], Xmm::XMM0)) return result;
                    constructor = "arco_value_new_number";
                } else if (printed_kind == HostedValueKind::Bool) {
                    if (!load_value(instruction.operands[0], "BOOL", Reg::RDI)) return result;
                    constructor = "arco_value_new_bool";
                } else {
                    result.ok = false;
                    result.error = "PRINT on this backend currently supports only a directly printed "
                        "string literal, number literal, or straightforward variable/expression of "
                        "either type";
                    return result;
                }
                {
                    const auto new_value_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({new_value_disp, constructor});
                }
                result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);
                result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                {
                    const auto print_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({print_disp, "arco_value_print"});
                }
                result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                {
                    const auto release_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({release_disp, "arco_value_release"});
                }
                break;
            }

            case AmirInstruction::Kind::Return: {
                const std::string& value_ref = instruction.operands.front();
                bool returns_via_xmm0 = false;
                if (value_ref == "nothing" && convention == systems::CallingConvention::SystemV) {
                    // Under System V, a "nothing" return still writes a real value to RAX: every
                    // Kind::CallValue call site unconditionally classifies and stores a return
                    // value for its own discard temp regardless of whether anything ever reads it
                    // (e.g. a class constructor's own `Init` call -- see lower_class), and
                    // infer_function_return_kind now treats a literal "nothing" RETURN as
                    // Boxed/null (matching the Const case's own "nothing" convention), so the
                    // caller expects a null pointer in RAX, not whatever RAX happened to hold from
                    // an earlier instruction. Microsoft x64/freestanding has no such expectation
                    // (its own call sites never classify a return kind this way), so this is
                    // deliberately System V-only -- that convention's existing "write nothing at
                    // all for a void return" behavior below is completely untouched.
                    result.text.mov_reg_imm64(Reg::RAX, 0);
                } else if (value_ref != "nothing") {
                    // A hosted-number return value comes back in XMM0, not RAX (matching the
                    // Kind::Call general-call branch's own expectation -- see
                    // infer_function_return_kind). Every function's own declared return_type is a
                    // generic "VALUE" placeholder (ArcoBASIC has no static return-type annotation),
                    // so this can't be gated on that string the way parameters are gated on their
                    // own `AS Type` -- it reuses the same straight-line value-flow inference PRINT's
                    // Call case already relies on instead.
                    if (convention == systems::CallingConvention::SystemV &&
                        infer_hosted_value_kind(module, *target, value_ref) == HostedValueKind::Number) {
                        if (!load_value_double(value_ref, Xmm::XMM0)) return result;
                        returns_via_xmm0 = true;
                    } else {
                    const int value_slot = slot_of(value_ref);
                    if (value_slot >= 0) {
                        if (value_slot <= 127) result.text.mov_load_disp8(Reg::RAX, Reg::RSP, static_cast<std::uint8_t>(value_slot));
                        else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(value_slot));
                    } else {
                        // Not a slot reference -- the synthesized top-level Main wrapper's own
                        // final RETURN (build_amir's ensure_terminated(main, ..., "I32", "0")) is a
                        // bare integer literal, never a temp, unlike every user-written RETURN
                        // (which always goes through a temp -- see the Const case's own handling of
                        // exactly this literal-vs-slot distinction).
                        std::uint64_t literal_value = 0;
                        if (!parse_integer(value_ref, literal_value)) {
                            result.ok = false;
                            result.error = "RETURN of \"" + value_ref + "\" has no assigned stack slot";
                            return result;
                        }
                        result.text.mov_reg_imm64(Reg::RAX, literal_value);
                    }
                    }
                }
                // Reference-counted lifetime tracking (RFC-0049, "full Linux support" pass): every
                // Boxed-kind local this function declares -- except the one, if any, whose value is
                // being returned THIS INSTANT (ownership of that one reference transfers to the
                // caller, so it's deliberately excluded here, not released) -- still holds a live
                // reference this function owns at the moment it returns. This runs independently at
                // EVERY return site: an earlier `IF cond THEN RETURN x` and a later `RETURN nothing`
                // in the same function release `x` differently (the first transfers it to the
                // caller, the second must free it, since it's no longer reachable once this function
                // is gone). The return value itself is spilled to this frame's own scratch region
                // first and reloaded afterward, since an arco_value_release call clobbers RAX/XMM0
                // like any ordinary System V call would.
                if (convention == systems::CallingConvention::SystemV) {
                    bool swept_any = false;
                    for (const auto& name : slot_names) {
                        if (name == value_ref) continue;
                        if (parameter_names.count(name) != 0) continue;
                        if (infer_local_kind(module, *target, name, 0) != HostedValueKind::Boxed) continue;
                        if (!swept_any) {
                            if (returns_via_xmm0) result.text.movsd_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Xmm::XMM0);
                            else result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);
                            swept_any = true;
                        }
                        const int offset = slot_of(name);
                        if (offset <= 127) result.text.mov_load_disp8(Reg::RDI, Reg::RSP, static_cast<std::uint8_t>(offset));
                        else result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(offset));
                        const auto release_disp = result.text.call_rel32_placeholder();
                        result.external_calls.push_back({release_disp, "arco_value_release"});
                    }
                    if (swept_any) {
                        if (returns_via_xmm0) result.text.movsd_load_disp32(Xmm::XMM0, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                        else result.text.mov_load_disp32(Reg::RAX, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    }
                }
                // Same sign-extension hazard as the prologue's sub rsp, imm8 above.
                if (frame_size <= 127) result.text.add_rsp_imm8(static_cast<std::uint8_t>(frame_size));
                else result.text.add_rsp_imm32(static_cast<std::uint32_t>(frame_size));
                result.text.ret();
                break;
            }

            // Phase 2 (RFC-0049 Section 4): arrays and objects, both always boxed through
            // ArcoValue -- there is no unboxed representation for a dynamic, heterogeneous
            // collection the way a provably-numeric scalar has one, so this isn't a fallback path
            // the way boxing a printed value is, it's simply the only representation. See
            // .agents/ARCO_NATIVE_RUNTIME_PROGRESS.md for the entry documenting this milestone.
            // Reference-counted lifetime tracking ("full Linux support" pass, later): a NAMED local
            // holding this array/object IS now released on reassignment/function exit (see
            // Kind::Store/store_result/Kind::Return's own comments) -- what remains, and is handled
            // right here in each construction loop below, is releasing the per-ELEMENT/per-FIELD
            // marshaling temporary itself when it's a freshly boxed scalar (see
            // box_operand_freshly_boxed's own comment), a completely separate reference from the
            // array/object's own.
            case AmirInstruction::Kind::Array: {
                if (convention != systems::CallingConvention::SystemV) {
                    result.ok = false;
                    result.error = "this milestone's code generator does not support this A-MIR instruction kind";
                    return result;
                }
                {
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_value_new_array_empty"});
                }
                // Spilled to the result's own final slot immediately -- also doubles as the source
                // reloaded before every element-push call below (push never changes the pointer
                // itself, only mutates the array it points to in place).
                if (!store_result(instruction.result, "STRING")) return result;
                for (const auto& element : instruction.operands) {
                    if (!box_operand_into_rax(element)) return result;
                    const bool element_freshly_boxed = box_operand_freshly_boxed;
                    result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);
                    if (!load_value(instruction.result, "STRING", Reg::RDI)) return result;
                    result.text.mov_load_disp32(Reg::RSI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_value_array_push"});
                    // arco_value_array_push copies `value`'s current content (see its own header
                    // comment) -- never takes ownership, so a freshly boxed element leaks here
                    // otherwise, same reasoning as arco_value_concat above (and the exact leak
                    // originally found and fixed via measured RSS growth in this pass -- see
                    // Kind::Object's own field-set loop just below, the sibling site this same bug
                    // was first caught in).
                    if (element_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base));
                }
                break;
            }

            // Tuples (RFC-0049, "full Linux support" pass): every element is available up front
            // on this ONE instruction (unlike Kind::Array's own incremental push loop, since AMIR's
            // Kind::Tuple never grows one element at a time), so this boxes each element into its
            // own slot in host_args_base (shared with the generic host-function bridge's own
            // per-call argument buffer -- see max_host_call_args's own comment above for why
            // that's safe) then calls arco_value_new_tuple once with the whole contiguous array,
            // instead of pushing one at a time. Tuples are immutable (matching the interpreter/
            // bytecode VM's own "value is not index-assignable" rejection of `t[0] = x`) -- there
            // is deliberately no Kind::StoreIndex support for a tuple target.
            case AmirInstruction::Kind::Tuple: {
                if (convention != systems::CallingConvention::SystemV) {
                    result.ok = false;
                    result.error = "this milestone's code generator does not support this A-MIR instruction kind";
                    return result;
                }
                std::vector<bool> element_freshly_boxed(instruction.operands.size(), false);
                for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                    if (!box_operand_into_rax(instruction.operands[i])) return result;
                    element_freshly_boxed[i] = box_operand_freshly_boxed;
                    const std::uint32_t slot_offset = static_cast<std::uint32_t>(host_args_base + 8 * static_cast<int>(i));
                    result.text.mov_store_disp32(Reg::RSP, slot_offset, Reg::RAX);
                }
                // arco_value_new_tuple(elements, element_count) -- elements in RDI, count in RSI
                // (a 2-argument ABI function, unlike arco_call_host's 3-argument
                // name/args/arg_count shape the empty-array special case above is modeled after).
                if (instruction.operands.empty()) {
                    result.text.mov_reg_imm64(Reg::RDI, 0);
                } else {
                    result.text.lea_rsp_disp32(Reg::RDI, static_cast<std::uint32_t>(host_args_base));
                }
                result.text.mov_reg_imm64(Reg::RSI, static_cast<std::uint64_t>(instruction.operands.size()));
                {
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_value_new_tuple"});
                }
                if (!store_result(instruction.result, "STRING")) return result;
                // arco_value_new_tuple copies each element's own content (see its own header
                // comment) -- never takes ownership, so a freshly boxed element leaks here
                // otherwise, the same reasoning as Kind::Array's own element-push loop above.
                for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                    if (!element_freshly_boxed[i]) continue;
                    release_scratch_temp(static_cast<std::uint32_t>(host_args_base + 8 * static_cast<int>(i)));
                }
                break;
            }

            case AmirInstruction::Kind::Object: {
                if (convention != systems::CallingConvention::SystemV) {
                    result.ok = false;
                    result.error = "this milestone's code generator does not support this A-MIR instruction kind";
                    return result;
                }
                {
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_value_new_object"});
                }
                if (!store_result(instruction.result, "STRING")) return result;
                for (const auto& field : instruction.operands) {
                    const auto split = field.find(':');
                    if (split == std::string::npos) {
                        result.ok = false;
                        result.error = "invalid OBJECT field operand: " + field;
                        return result;
                    }
                    std::string key = field.substr(0, split);
                    if (key.size() >= 2 && key.front() == '"' && key.back() == '"') key = unquote_constant(key);
                    const std::string value_ref = field.substr(split + 1);

                    // Box the value FIRST (may itself use RDI/XMM0/an external call internally) --
                    // spilling it before touching RDI/RSI for the field-set call below, the same
                    // ordering StoreIndex's object branch uses and for the identical reason.
                    if (!box_operand_into_rax(value_ref)) return result;
                    const bool field_value_freshly_boxed = box_operand_freshly_boxed;
                    result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);

                    // Encode the field name as a UTF-16 rdata blob -- the same raw pointer shape a
                    // string literal already produces (Const's own String branch), so it can be
                    // passed directly to arco_value_object_set with no re-encoding.
                    std::vector<char16_t> encoded;
                    try {
                        encoded = systems::encode_utf16_null_terminated(key);
                    } catch (const std::exception& error) {
                        result.ok = false;
                        result.error = std::string("object field name cannot be encoded as UTF-16: ") + error.what();
                        return result;
                    }
                    const std::size_t data_offset = result.rdata.size();
                    for (char16_t unit : encoded) {
                        result.rdata.push_back(static_cast<std::uint8_t>(unit & 0xFF));
                        result.rdata.push_back(static_cast<std::uint8_t>((unit >> 8) & 0xFF));
                    }
                    {
                        const std::size_t disp_offset = result.text.lea_rip_relative(Reg::RSI);
                        result.relocations.push_back({disp_offset, result.text.size(), data_offset});
                    }
                    if (!load_value(instruction.result, "STRING", Reg::RDI)) return result;
                    result.text.mov_load_disp32(Reg::RDX, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_value_object_set"});
                    // arco_value_object_set copies `value`'s current content (see its own header
                    // comment) -- never takes ownership. This is the exact site that first surfaced
                    // this pass's own real, measured leak (an OBJECT literal with a plain number/
                    // string field, reassigned in a tight loop, showed linear-in-iteration-count RSS
                    // growth even after the Kind::Store/store_result/Kind::Return lifetime fixes
                    // elsewhere in this same pass -- those releases only ever see the OBJECT's own
                    // slot, never this per-field marshaling temporary, which is a completely
                    // separate reference this call site itself owns and must release).
                    if (field_value_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base));
                }
                break;
            }

            case AmirInstruction::Kind::Index: {
                if (convention != systems::CallingConvention::SystemV || instruction.operands.size() != 1) {
                    result.ok = false;
                    result.error = "this milestone's code generator does not support this A-MIR instruction kind";
                    return result;
                }
                const std::string& index_operand = instruction.operands.front();
                const HostedValueKind target_kind = infer_hosted_value_kind(module, *target, instruction.target);
                // A plain STRING target (this backend's own unboxed, raw-UTF16-pointer
                // representation -- see box_operand_into_rax's own comment) needs codepoint
                // indexing (RFC-0049, "full Linux support" pass): boxed fresh just to satisfy
                // arco_value_index_get's own ABI (it only ever receives an ArcoValue*), then
                // released immediately after, the same "temp built only for one call" discipline
                // as every other freshly-boxed-scalar site in this function (box_operand_into_rax's
                // own freshness flag isn't reused here since this is a plain STRING, never
                // Boxed/Number/Bool -- always a fresh construction).
                if (target_kind != HostedValueKind::Boxed && target_kind != HostedValueKind::String) {
                    result.ok = false;
                    result.error = "indexing/property access on this backend's System V fast path "
                        "requires a provably array/object/string target (this straight-line static "
                        "analysis could not confirm \"" + instruction.target + "\" is one)";
                    return result;
                }
                const HostedValueKind index_kind = infer_hosted_value_kind(module, *target, index_operand);
                if (target_kind == HostedValueKind::String) {
                    if (index_kind != HostedValueKind::Number) {
                        result.ok = false;
                        result.error = "indexing a string on this backend's System V fast path "
                            "requires a provably number (codepoint) index";
                        return result;
                    }
                    if (!load_value(instruction.target, "STRING", Reg::RDI)) return result;
                    {
                        const auto call_disp = result.text.call_rel32_placeholder();
                        result.external_calls.push_back({call_disp, "arco_value_new_string_utf16"});
                    }
                    result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);
                    result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    if (!load_value_double(index_operand, Xmm::XMM0)) return result;
                    {
                        const auto call_disp = result.text.call_rel32_placeholder();
                        result.external_calls.push_back({call_disp, "arco_value_index_get"});
                    }
                    result.text.mov_reg_reg(Reg::R10, Reg::RAX); // survive the release call below
                    result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    {
                        const auto release_disp = result.text.call_rel32_placeholder();
                        result.external_calls.push_back({release_disp, "arco_value_release"});
                    }
                    result.text.mov_reg_reg(Reg::RAX, Reg::R10);
                } else if (index_kind == HostedValueKind::String) {
                    // Dotted property access (obj.field) lowers to exactly this shape -- a
                    // STRING-constant index -- see lower_variable's own comment.
                    if (!load_value(instruction.target, "STRING", Reg::RDI)) return result;
                    if (!load_value(index_operand, "STRING", Reg::RSI)) return result;
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_value_object_get"});
                } else if (index_kind == HostedValueKind::Number || index_kind == HostedValueKind::Boxed) {
                    // arco_value_index_get (not the narrower arco_value_array_get) -- a general
                    // index-by-number covering array/tuple/bit-vector/range uniformly, mirroring
                    // the bytecode VM/interpreter's own index_value() dispatch (see its own header
                    // comment). Every one of those classifies Boxed here regardless of which it
                    // actually is at runtime, so this call site can't and doesn't need to know
                    // which -- the ABI function itself dispatches on the real runtime type.
                    //
                    // A Boxed index (not just a provably-Number one) needs the SAME unboxing
                    // load_double_operand already gives Binary arithmetic's own Boxed operands --
                    // found via Arconaut, a real program: `devices[app.DeviceScroll + row]`-shaped
                    // indexing is pervasive (every scrolling list -- Volumes/Plugins/Snapshots/the
                    // console), and `app.DeviceScroll + row` (app.DeviceScroll a Boxed object-field
                    // read) is now itself classified Boxed by the Binary "+" case's own broadened,
                    // correctness-motivated rule (see its comment) -- always a real number at
                    // runtime, but no longer PROVABLY Number to this static analysis. Rejecting it
                    // outright here would make ordinary scrolling in a real, direct port of this
                    // exact bug's own fix regress a working feature; load_double_operand already
                    // does exactly the right thing (arco_value_as_number, panicking cleanly if this
                    // Boxed value genuinely isn't numeric -- matching how the interpreter's own
                    // index_value() would fail on a non-numeric array index too).
                    //
                    // Index computed BEFORE the target is loaded into RDI (reversed from this
                    // branch's own pre-existing order): unlike load_value_double (a plain stack
                    // read, never a call), load_double_operand's own Boxed path makes a real
                    // external call (arco_value_as_number), free to clobber RDI -- loading the
                    // target into RDI first would have that call stomp on it before
                    // arco_value_index_get ever ran, the exact same register-clobber-across-a-call
                    // hazard this file's marshaling code documents extensively elsewhere.
                    if (!load_double_operand(index_operand, Xmm::XMM0)) return result;
                    if (!load_value(instruction.target, "STRING", Reg::RDI)) return result;
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_value_index_get"});
                } else {
                    result.ok = false;
                    result.error = "indexing on this backend's System V fast path requires a "
                        "provably number (array index) or string (object field name) index; "
                        "received an index this static analysis could not classify";
                    return result;
                }
                if (!store_result(instruction.result, "STRING")) return result;
                break;
            }

            case AmirInstruction::Kind::StoreIndex: {
                // lower_assignment always passes the BASE variable's raw name as `.target` (never a
                // temp -- unlike Kind::Index's `.target`, which is always Load-derived), so this
                // needs infer_local_kind directly rather than infer_hosted_value_kind.
                //
                // Chained indexed assignment (`a.b.c = x`, `arr[i].field = x`, `arr[i][j] = x`, any
                // mix): `instruction.operands` is `[key1, key2, ..., keyN, value]` -- one or more
                // keys (N >= 1) followed by the value, exactly matching how the interpreter/
                // bytecode VM's own assign_indexed already recurses through an arbitrary-length key
                // list (confirmed identical on both: `a.b.c = x` already worked on compile-run
                // before this backend could compile it at all -- a real, disclosed native-backend-
                // only gap, not a parser/interpreter one). DESCENDS through every key but the last
                // via an ordinary INDEX read (arco_value_object_get/array_get, exactly like
                // Kind::Index's own codegen), landing on the second-to-last level's own object/
                // array, then SETS the final key on it -- the same shape assign_indexed's own
                // recursion produces, just unrolled into straight-line native code since the key
                // count is fixed at compile time (this is not a dynamically-sized chain).
                if (convention != systems::CallingConvention::SystemV || instruction.operands.size() < 2) {
                    result.ok = false;
                    result.error = "this milestone's code generator does not support this A-MIR instruction kind";
                    return result;
                }
                const std::string& value_operand = instruction.operands.back();
                const std::size_t key_count = instruction.operands.size() - 1;
                const HostedValueKind target_kind = infer_local_kind(module, *target, instruction.target, 0);
                if (target_kind != HostedValueKind::Boxed) {
                    result.ok = false;
                    result.error = "indexed assignment on this backend's System V fast path "
                        "requires a provably array/object target (this straight-line static "
                        "analysis could not confirm \"" + instruction.target + "\" is one)";
                    return result;
                }
                // Box the VALUE first -- exactly like Object construction above, and for the same
                // reason: the box call may itself use RDI/XMM0, so it must happen before RDI/RSI
                // are loaded with the receiver/key for the descent/set calls below. Kept alive in
                // its own scratch slot for the whole descent, which may itself use scratch_base+8
                // (the current receiver) and further external calls in between.
                if (!box_operand_into_rax(value_operand)) return result;
                const bool store_value_freshly_boxed = box_operand_freshly_boxed;
                result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base), Reg::RAX);
                // The current receiver starts as the base target's own (borrowed -- never released
                // here) value; each descent step below replaces it with a FRESH, owned reference
                // (arco_value_object_get/array_get's own contract) that must be released once its
                // own one-level-deeper read is done with it, same reasoning as instance-method
                // dispatch's own "__class" field release.
                if (!load_value(instruction.target, "STRING", Reg::RDI)) return result;
                result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8), Reg::RDI);
                for (std::size_t i = 0; i + 1 < key_count; ++i) {
                    const std::string& key_operand = instruction.operands[i];
                    const HostedValueKind key_kind = infer_hosted_value_kind(module, *target, key_operand);
                    result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8));
                    if (key_kind == HostedValueKind::String) {
                        if (!load_value(key_operand, "STRING", Reg::RSI)) return result;
                        const auto call_disp = result.text.call_rel32_placeholder();
                        result.external_calls.push_back({call_disp, "arco_value_object_get"});
                    } else if (key_kind == HostedValueKind::Number) {
                        if (!load_value_double(key_operand, Xmm::XMM0)) return result;
                        const auto call_disp = result.text.call_rel32_placeholder();
                        result.external_calls.push_back({call_disp, "arco_value_array_get"});
                    } else {
                        result.ok = false;
                        result.error = "indexed assignment on this backend's System V fast path requires "
                            "a provably number (array index) or string (object field name) index at "
                            "every level of a chained assignment; received an index this static "
                            "analysis could not classify";
                        return result;
                    }
                    // The new receiver (RAX) must survive the release call just below -- an
                    // ordinary System V call is free to clobber RAX internally even though
                    // arco_value_release returns void, the same "spill before releasing" discipline
                    // store_result's own tracks_lifetime logic already follows.
                    result.text.mov_reg_reg(Reg::R10, Reg::RAX);
                    // Release the receiver this step just descended PAST -- the very first one
                    // (i == 0) is the base target's own borrowed value and must NOT be released;
                    // every one after that is a fresh reference this loop's own previous iteration
                    // created and is now done with.
                    if (i > 0) release_scratch_temp(static_cast<std::uint32_t>(scratch_base + 8));
                    result.text.mov_store_disp32(Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8), Reg::R10);
                }
                const std::string& last_key_operand = instruction.operands[key_count - 1];
                const HostedValueKind last_key_kind = infer_hosted_value_kind(module, *target, last_key_operand);
                if (last_key_kind == HostedValueKind::String) {
                    result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8));
                    if (!load_value(last_key_operand, "STRING", Reg::RSI)) return result;
                    result.text.mov_load_disp32(Reg::RDX, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_value_object_set"});
                } else if (last_key_kind == HostedValueKind::Number) {
                    result.text.mov_load_disp32(Reg::RDI, Reg::RSP, static_cast<std::uint32_t>(scratch_base + 8));
                    if (!load_value_double(last_key_operand, Xmm::XMM0)) return result;
                    result.text.mov_load_disp32(Reg::RSI, Reg::RSP, static_cast<std::uint32_t>(scratch_base));
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_value_array_set"});
                } else {
                    result.ok = false;
                    result.error = "indexed assignment on this backend's System V fast path requires "
                        "a provably number (array index) or string (object field name) index at "
                        "every level of a chained assignment; received an index this static "
                        "analysis could not classify";
                    return result;
                }
                // The final receiver is released the same way every intermediate one was, UNLESS
                // it's still the base target's own borrowed value (key_count == 1, the common,
                // non-chained case -- no descent ever ran, so scratch_base+8 was never replaced).
                if (key_count > 1) release_scratch_temp(static_cast<std::uint32_t>(scratch_base + 8));
                // arco_value_array_set/arco_value_object_set both copy `value`'s current content
                // (see their own header comments) -- neither takes ownership, so a freshly boxed
                // scalar value (e.g. `arr[i] = 5`) leaks here otherwise, the same StoreIndex-shaped
                // leak Kind::Object's own field-set loop above was first caught with.
                if (store_value_freshly_boxed) release_scratch_temp(static_cast<std::uint32_t>(scratch_base));
                break;
            }

            // ADDRESSOF (RFC-0049): produces a CALLABLE value for `instruction.target` (a bare
            // function name -- `amir_address_of` never sets any operands). Boxed as a plain string
            // NAMING the function (reusing the ordinary string-construction path, no new ArcoValue
            // representation needed) rather than a raw code-pointer/thunk: the call site
            // (Kind::CallValue's own "ADDRESSOF/CALLABLE dispatch" case above) resolves it via the
            // SAME runtime string-comparison-against-a-compile-time-enumerable-candidate-set
            // pattern instance method dispatch already uses, which sidesteps needing a uniform
            // calling-convention thunk per callable target entirely. Scope reduction, disclosed:
            // only a plain function name is supported -- a dotted target (SUPER-style
            // class-qualified, or a bound instance method, e.g. `ADDRESSOF instance.Method`) is
            // real, unattempted future work, matching the call site's own identical restriction.
            case AmirInstruction::Kind::AddressOf: {
                if (convention != systems::CallingConvention::SystemV) {
                    result.ok = false;
                    result.error = "this milestone's code generator does not support this A-MIR instruction kind";
                    return result;
                }
                if (instruction.target.find('.') != std::string::npos) {
                    result.ok = false;
                    result.error = "ADDRESSOF of \"" + instruction.target + "\" (a class-qualified or bound "
                        "instance method) is real, disclosed, unattempted future work on this backend's "
                        "System V fast path -- only a plain function name is supported";
                    return result;
                }
                const AmirFunction* addressed_function = nullptr;
                for (const auto& candidate : module.functions) {
                    if (candidate.name == instruction.target) { addressed_function = &candidate; break; }
                }
                if (!addressed_function) {
                    result.ok = false;
                    result.error = "ADDRESSOF cannot resolve callable: \"" + instruction.target + "\" is not a declared function";
                    return result;
                }
                std::vector<char16_t> encoded;
                try {
                    encoded = systems::encode_utf16_null_terminated(instruction.target);
                } catch (const std::exception& error) {
                    result.ok = false;
                    result.error = std::string("callable name cannot be encoded as UTF-16: ") + error.what();
                    return result;
                }
                const std::size_t data_offset = result.rdata.size();
                for (char16_t unit : encoded) {
                    result.rdata.push_back(static_cast<std::uint8_t>(unit & 0xFF));
                    result.rdata.push_back(static_cast<std::uint8_t>((unit >> 8) & 0xFF));
                }
                {
                    const std::size_t disp_offset = result.text.lea_rip_relative(Reg::RDI);
                    result.relocations.push_back({disp_offset, result.text.size(), data_offset});
                }
                {
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_value_new_string_utf16"});
                }
                if (!store_result(instruction.result, "STRING")) return result;
                break;
            }

            // TRY/CATCH (RFC-0049): generated code has no unwind tables, so this calls the real
            // libc `setjmp` DIRECTLY (its own comment in native_runtime_abi.h explains why it
            // can't be wrapped in an ABI function) into this TRY block's own dedicated jmp_buf slot
            // (try_jmpbuf_base/try_block_ordinal, sized above), registers it as the active handler
            // via arco_try_push on the initial (0) return, and falls through into the ordinary TRY
            // body. A nonzero return means a longjmp landed here instead -- generated FROM
            // arco_value_panic/arco_throw, arbitrarily many native calls deep, exactly like the
            // bytecode VM's own per-frame try/catch chain reaching the same observable "innermost
            // active handler catches first" result. That landing path reads back the caught
            // {Message, Type} object (arco_try_error) and binds it to the declared CATCH variable
            // (instruction.operands.front(), skipped entirely if empty/unused -- `CATCH` with no
            // name, or one the catch body never references and so was never given a stack slot by
            // this function's own slot-collection scan) before jumping to the real catch block
            // (instruction.target).
            case AmirInstruction::Kind::TryBegin: {
                if (convention != systems::CallingConvention::SystemV) {
                    result.ok = false;
                    result.error = "this milestone's code generator does not support this A-MIR instruction kind";
                    return result;
                }
                const std::uint32_t jmpbuf_offset = static_cast<std::uint32_t>(try_jmpbuf_base + kJmpBufSlotSize * try_block_ordinal);
                ++try_block_ordinal;
                result.text.lea_rsp_disp32(Reg::RDI, jmpbuf_offset);
                {
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "setjmp"});
                }
                result.text.cmp_reg_imm32(Reg::RAX, 0);
                const std::size_t caught_disp = result.text.jcc_rel32_placeholder(0x5); // JNE -> caught
                // Initial (0) return: register the handler and fall through into the TRY body.
                result.text.lea_rsp_disp32(Reg::RDI, jmpbuf_offset);
                {
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_try_push"});
                }
                const std::size_t skip_caught_disp = result.text.jmp_rel32_placeholder();
                // Caught (nonzero) return: bind the error variable, if any, then jump to the real
                // catch block.
                const std::size_t caught_start = result.text.size();
                {
                    const std::int64_t next_instruction = static_cast<std::int64_t>(caught_disp + 4);
                    result.text.patch_i32(caught_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(caught_start) - next_instruction));
                }
                const std::string error_name = instruction.operands.empty() ? std::string() : instruction.operands.front();
                if (!error_name.empty() && slot_of(error_name) >= 0) {
                    const auto call_disp = result.text.call_rel32_placeholder();
                    result.external_calls.push_back({call_disp, "arco_try_error"});
                    if (!store_result(error_name, "STRING")) return result;
                }
                {
                    const std::size_t displacement = result.text.jmp_rel32_placeholder();
                    branch_fixups.push_back({displacement, instruction.target});
                }
                const std::size_t skip_caught_start = result.text.size();
                {
                    const std::int64_t next_instruction = static_cast<std::int64_t>(skip_caught_disp + 4);
                    result.text.patch_i32(skip_caught_disp, static_cast<std::int32_t>(static_cast<std::int64_t>(skip_caught_start) - next_instruction));
                }
                break;
            }

            case AmirInstruction::Kind::TryEnd: {
                if (convention != systems::CallingConvention::SystemV) {
                    result.ok = false;
                    result.error = "this milestone's code generator does not support this A-MIR instruction kind";
                    return result;
                }
                const auto call_disp = result.text.call_rel32_placeholder();
                result.external_calls.push_back({call_disp, "arco_try_pop"});
                break;
            }

            case AmirInstruction::Kind::Throw: {
                if (convention != systems::CallingConvention::SystemV) {
                    result.ok = false;
                    result.error = "this milestone's code generator does not support this A-MIR instruction kind";
                    return result;
                }
                if (instruction.operands.empty()) {
                    result.ok = false;
                    result.error = "THROW without a value";
                    return result;
                }
                if (!box_operand_into_rax(instruction.operands.front())) return result;
                result.text.mov_reg_reg(Reg::RDI, Reg::RAX);
                const auto call_disp = result.text.call_rel32_placeholder();
                result.external_calls.push_back({call_disp, "arco_throw"});
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
X86_64CodegenResult generate_x86_64_program(const AmirModule& module, const std::string& entry_function,
                                             systems::CallingConvention convention = systems::CallingConvention::MicrosoftX64,
                                             bool annotate = false) {
    X86_64CodegenResult combined;
    combined.entry_symbol = entry_function;
    std::vector<std::pair<std::string, X86_64CodegenResult>> fragments;
    std::unordered_set<std::string> emitted;
    auto add_fragment = [&](const std::string& name) -> bool {
        if (!emitted.insert(name).second) return true;
        auto fragment = generate_x86_64_function(module, name, convention, annotate);
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

    // Only appended when actually referenced: unlike the fixed CPU.* opcodes (CLI, HLT, LGDT, ...)
    // this is ~600 bytes of synthesized code, and most programs never call
    // CPU.ExceptionVectorTableBase(). Appending it unconditionally would silently grow every
    // compiled UEFI binary, freestanding or not.
    bool references_exception_table = false;
    for (const auto& fragment : fragments) {
        for (const auto& call : fragment.second.internal_calls) {
            if (call.target == kExceptionVectorTableSymbol) { references_exception_table = true; break; }
        }
        if (references_exception_table) break;
    }
    if (references_exception_table) {
        fragments.emplace_back(kExceptionVectorTableSymbol, generate_exception_vector_table());
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
        for (const auto& call : fragment.second.external_calls) {
            combined.external_calls.push_back({text_base + call.disp_field_offset, call.symbol});
        }
        for (const auto& note : fragment.second.annotations) {
            combined.annotations.push_back({text_base + note.text_offset, note.comment});
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

// 64-bit GAS register name for x86-64 encoding index 0-15 (REX.R/B-extended already folded in by
// the caller), Intel-syntax spelling.
const char* gas_register_name_64(int index) {
    static const char* const names[16] = {
        "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    };
    return (index >= 0 && index < 16) ? names[index] : "rax";
}

// Renders `codegen` (produced with CallingConvention::SystemV) as a real GNU-assembler source file,
// rather than either render_x86_64's human-readable text listing or write_pe32plus_efi_image's
// hand-written PE image (needed there only because bare UEFI has no linker to hand off to). Leans
// on the system assembler/linker -- already how ArcoFission's own bytecode-VM-embedding capsules
// reach a final ELF64, see native_launcher_source's own `std::system("c++ ...")` invocation -- to do
// two things this backend deliberately does not reimplement: resolving calls into the native ArcoSH
// runtime shim (a real symbol defined in another object entirely, not known at codegen time -- see
// X86_64CodegenResult::ExternalCallFixup's own comment) and laying out .text/.rodata into a final
// ELF image, including whatever section alignment/padding `as`/`ld` insert between them (which this
// backend has no reliable way to predict, unlike write_pe32plus_efi_image's fully-owned layout).
//
// Every byte this backend already encoded is preserved exactly via `.byte` -- only the specific
// spans recorded in `external_calls` (a CALL whose target isn't part of this same compilation) and
// `relocations` (a LEA whose target is this program's own .rodata, emitted with a real GAS `lea`
// mnemonic instead so the assembler computes that particular RIP-relative displacement rather than
// this backend assuming .text and .rodata end up a predictable distance apart) are replaced with
// real GAS mnemonics; every other instruction's bytes are untouched, still assembled exactly as
// this backend encoded them.
std::string render_x86_64_linux_asm(const X86_64CodegenResult& codegen) {
    struct Span {
        std::size_t start = 0;
        std::size_t length = 0;
        std::string mnemonic;
    };
    std::map<std::size_t, Span> spans;
    const auto& bytes = codegen.text.bytes();

    for (const auto& call : codegen.external_calls) {
        Span span;
        span.start = call.disp_field_offset - 1; // the E8 opcode byte precedes the 4-byte displacement
        span.length = 5;
        span.mnemonic = "    call " + call.symbol + "\n";
        spans[span.start] = span;
    }
    for (const auto& relocation : codegen.relocations) {
        // lea_rip_relative's own encoding: REX(1) 8D(1) ModRM(1) disp32(4) = 7 bytes, ending at
        // instruction_end_offset. Decode the destination register back out of the REX/ModRM bytes
        // rather than assuming one (today's only caller always uses RAX, but this stays correct if
        // that ever changes) -- see lea_rip_relative's own comment in x86_64_encoder.hpp for the
        // exact bit layout being reversed here.
        Span span;
        span.start = relocation.instruction_end_offset - 7;
        span.length = 7;
        if (span.start + 1 >= bytes.size() || relocation.instruction_end_offset - 5 >= bytes.size()) {
            continue; // malformed -- leave as raw bytes, the resulting asm simply won't assemble
        }
        const std::uint8_t rex_byte = bytes[span.start];
        const std::uint8_t modrm_byte = bytes[relocation.instruction_end_offset - 5];
        const int reg_low3 = (modrm_byte >> 3) & 0x7;
        const bool rex_r = (rex_byte & 0x04) != 0;
        const int reg_index = rex_r ? reg_low3 + 8 : reg_low3;
        span.mnemonic = std::string("    lea ") + gas_register_name_64(reg_index) +
            ", [rip + .Lrodata + " + std::to_string(relocation.rdata_offset) + "]\n";
        spans[span.start] = span;
    }

    // Arco native debugger tooling (see X86_64CodegenResult::InstructionAnnotation's own comment)
    // -- a multimap since Kind::Source markers carry no bytes of their own, so a real instruction
    // and the source-line marker immediately preceding it can legitimately share one offset, and
    // both comments are worth keeping. Empty (`codegen.annotations` was never populated) whenever
    // the caller didn't ask for annotated output, so this whole mechanism is a pure no-op then --
    // the lookup below always misses immediately.
    std::multimap<std::size_t, std::string> annotations_by_offset;
    for (const auto& note : codegen.annotations) {
        annotations_by_offset.emplace(note.text_offset, note.comment);
    }

    std::ostringstream out;
    out << ".intel_syntax noprefix\n";
    for (const auto& call : codegen.external_calls) {
        out << ".extern " << call.symbol << "\n";
    }
    out << ".text\n";
    out << ".global " << codegen.entry_symbol << "\n";
    // The entry function's own fragment is always emitted first by generate_x86_64_program
    // (add_fragment(entry_function) runs before any other function's), so its bytes always start
    // at offset 0 in codegen.text -- the label just needs to precede the byte-emission loop below.
    out << codegen.entry_symbol << ":\n";

    std::size_t i = 0;
    bool in_byte_run = false;
    const auto end_byte_run = [&]() {
        if (in_byte_run) {
            out << "\n";
            in_byte_run = false;
        }
    };
    while (i < bytes.size()) {
        const auto annotation_range = annotations_by_offset.equal_range(i);
        if (annotation_range.first != annotation_range.second) {
            end_byte_run();
            // The hex TEXT+offset is the actual point of this: every byte this backend emits is
            // preserved verbatim by this whole function (external_calls/relocations substitute
            // same-length real mnemonics for same-length placeholder bytes -- see this function's
            // own header comment), so a linked binary's `main + text_offset` address is EXACTLY
            // this instruction's own first byte. A crash's raw PC/return-address (from gdb, ASan,
            // or a core dump) needs only `nm`'s own `main` base subtracted to become directly
            // greppable here -- no DWARF line-table trust required at all, which matters because
            // this generated code has no CFI/frame-pointer chain of its own (see this comment's
            // own note in the debugger-tooling writeup): a `bt` more than one frame into it is a
            // heuristic guess, but a raw address is exact.
            for (auto it = annotation_range.first; it != annotation_range.second; ++it) {
                out << "    # TEXT+0x" << std::hex << i << std::dec << " " << it->second << "\n";
            }
        }
        const auto found = spans.find(i);
        if (found != spans.end()) {
            end_byte_run();
            out << found->second.mnemonic;
            i += found->second.length;
            continue;
        }
        if (!in_byte_run) {
            out << "    .byte ";
            in_byte_run = true;
        } else {
            out << ", ";
        }
        out << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]) << std::dec;
        ++i;
    }
    end_byte_run();

    out << "\n.section .rodata\n";
    out << ".Lrodata:\n";
    if (!codegen.rdata.empty()) {
        out << "    .byte ";
        for (std::size_t j = 0; j < codegen.rdata.size(); ++j) {
            if (j != 0) out << ", ";
            out << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(codegen.rdata[j]) << std::dec;
        }
        out << "\n";
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

// A default parameter value that isn't a simple literal -- e.g. `Vec3(0, 0, 0)` for
// `CONSTRUCTOR(position AS Vec3 = Vec3(0, 0, 0))` -- has no Value parse_constant_value() could
// produce at prepare time (there's no live object to construct). Recognizes exactly the shape
// this compiler actually renders such a default as, `Identifier(literal, literal, ...)`
// (render_ast_expression's own output for a Call/constructor expression), resolving it against
// already-compiled functions in `module` so BytecodeFunction::param_default_calls can evaluate it
// fresh (via execute_function) every time the default actually fires. Returns nullopt for
// anything else (nested non-literal arguments, arbitrary expressions) -- callers degrade to no
// default at all for those rather than guessing.
std::optional<std::pair<const BytecodeFunction*, std::vector<Value>>> resolve_param_default_call(
        const BytecodeModule& module, const std::string& default_text) {
    const auto open_paren = default_text.find('(');
    if (open_paren == std::string::npos || default_text.empty() || default_text.back() != ')') {
        return std::nullopt;
    }
    const std::string callee_name = default_text.substr(0, open_paren);
    if (callee_name.empty()) {
        return std::nullopt;
    }
    for (const char c : callee_name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.') {
            return std::nullopt;
        }
    }
    const auto found = module.function_indices.find(callee_name);
    if (found == module.function_indices.end()) {
        return std::nullopt;
    }

    std::vector<Value> args;
    const std::string inner = default_text.substr(open_paren + 1, default_text.size() - open_paren - 2);
    if (!inner.empty()) {
        int depth = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= inner.size(); ++i) {
            const bool at_end = i == inner.size();
            const char c = at_end ? ',' : inner[i];
            if (!at_end && (c == '(' || c == '[')) depth++;
            else if (!at_end && (c == ')' || c == ']')) depth--;
            if (c == ',' && depth == 0) {
                std::string piece = inner.substr(start, i - start);
                while (!piece.empty() && std::isspace(static_cast<unsigned char>(piece.front()))) piece.erase(piece.begin());
                while (!piece.empty() && std::isspace(static_cast<unsigned char>(piece.back()))) piece.pop_back();
                try {
                    args.push_back(parse_constant_value(piece));
                } catch (const std::exception&) {
                    // A nested non-literal argument (e.g. another constructor call) -- outside
                    // this fix's scope; let the caller treat the whole default as unresolved.
                    return std::nullopt;
                }
                start = i + 1;
            }
        }
    }
    return std::make_pair(&module.functions[found->second], std::move(args));
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

// Resolves a temp back to a compile-time-constant number, if it was computed by a simple chain of
// CONST (optionally wrapped in one UNARY negation) -- exactly the shape `STEP -1` lowers to
// (`CONST %ta K<1>; UNARY %tb - %ta; STORE step %tb`), which a plain STORE_CONST check alone
// misses since the stored value isn't a direct constant reference. Straight-line bytecode assigns
// each temp exactly once, so scanning backward from `upto_instruction` for whichever instruction
// writes `temp_index` finds its one and only definition.
std::optional<double> resolve_constant_number_for_temp(const BytecodeModule& module, const BytecodeBlock& block,
                                                        std::size_t upto_instruction, std::size_t temp_index) {
    for (std::size_t i = upto_instruction; i-- > 0;) {
        const auto& instr = block.instructions[i];
        if (instr.op == BytecodeOp::Const && instr.operands.size() == 2 &&
            !instr.operands[0].empty() && instr.operands[0][0] == '%' &&
            ref_index(instr.operands[0], '%', "temporary") == temp_index) {
            if (instr.operands[1].empty() || instr.operands[1][0] != 'K') return std::nullopt;
            const auto const_index = ref_index(instr.operands[1], 'K', "constant");
            if (const_index >= module.constant_values.size() || !module.constant_values[const_index].is_number()) return std::nullopt;
            return module.constant_values[const_index].as_number();
        }
        if (instr.op == BytecodeOp::Unary && instr.operands.size() == 3 &&
            !instr.operands[0].empty() && instr.operands[0][0] == '%' &&
            ref_index(instr.operands[0], '%', "temporary") == temp_index) {
            if (instr.operands[1] != "-") return std::nullopt;
            if (instr.operands[2].empty() || instr.operands[2][0] != '%') return std::nullopt;
            const auto inner = resolve_constant_number_for_temp(module, block, i, ref_index(instr.operands[2], '%', "temporary"));
            if (!inner) return std::nullopt;
            return -*inner;
        }
    }
    return std::nullopt;
}

// Forward-declared: defined later in this file (find_function needs BytecodeModule fully defined
// and is used all over execute_function, which comes after this), but the loop-body inliner just
// below needs it too. Same shape as the ref_index/local_index_from_ref ordering gap already noted
// elsewhere in this file -- a plain forward declaration is simpler here than reordering a function
// this many other things also depend on.
const BytecodeFunction* find_function(const BytecodeModule& module, const std::string& name);

// A "leaf" function the loop JIT can inline directly into a caller's compiled loop body instead of
// falling back to interpretation for the whole loop just because its body contains a call --
// exactly one parameter, one block, computing RETURN of a single arithmetic expression over that
// parameter and/or one constant. Deliberately NOT the same BINARY_LOCAL_LOCAL/BINARY_LOCAL_CONST
// shape the loop-body walker below already recognizes for direct "x = x OP y" assignment
// statements -- a RETURN expression (or any non-top-level-assignment expression) compiles through
// plain LOAD/CONST/BINARY-on-temps instead, confirmed via `ArcoFission reveal ... --stage
// BYTECODE` on exactly this shape (see fission.cpp's own JIT section history for the trace). Only
// ever called at prepare time, once per JIT-eligible loop's own call-site detection -- never on
// the hot per-execution path -- so this doesn't need to be fast, only conservative: any shape it
// doesn't recognize returns std::nullopt and that call site simply isn't inlined (the loop as a
// whole then also fails detection, same as any other unsupported body-op shape), never a wrong
// answer.
struct InlinableLeaf {
    NumericOp op = NumericOp::Unknown;
    bool right_is_const = false;
    double right_constant = 0.0;
};

std::optional<InlinableLeaf> try_resolve_inlinable_leaf(const BytecodeModule& module, const BytecodeFunction& callee) {
    if (callee.blocks.size() != 1) return std::nullopt;
    if (callee.params.size() != 1) return std::nullopt;
    // Populated by prepare_bytecode_module's own per-function pass -- empty (or the wrong size)
    // means that pass hasn't reached this callee yet (e.g. it's declared textually AFTER the
    // caller in source order), not that it has no parameter local. Rejecting here rather than
    // assuming is what keeps forward-declared callees a safe missed-optimization instead of a
    // correctness risk.
    if (callee.param_local_indices.size() != 1) return std::nullopt;
    if (callee.param_local_indices[0] == static_cast<std::size_t>(-1)) return std::nullopt;
    const std::size_t param_local = callee.param_local_indices[0];

    std::vector<const BytecodeInstruction*> real;
    for (const auto& instr : callee.blocks[0].instructions) {
        if (instr.op == BytecodeOp::Source) continue;
        real.push_back(&instr);
    }
    // LOAD %ta L<param>, optional CONST %tb K<n>, BINARY %tc OP %tx %ty, RETURN VALUE %tc -- 3
    // instructions for a self-referencing body like `RETURN x + x`, 4 when a constant is involved.
    if (real.size() < 3 || real.size() > 4) return std::nullopt;

    std::size_t idx = 0;
    const auto& load_instr = *real[idx];
    if (load_instr.op != BytecodeOp::Load || load_instr.operands.size() != 2) return std::nullopt;
    if (load_instr.operands[1].empty() || load_instr.operands[1][0] != 'L' ||
        ref_index(load_instr.operands[1], 'L', "local") != param_local) {
        return std::nullopt;
    }
    const std::string param_temp = load_instr.operands[0];
    ++idx;

    bool has_const = false;
    std::string const_temp;
    double const_value = 0.0;
    // A self-referencing expression like `RETURN x + x` re-LOADs the same local a second time
    // (confirmed via `ArcoFission reveal ... --stage BYTECODE`: the compiler does not reuse the
    // first LOAD's temp for a second reference to the same local) rather than reusing param_temp
    // directly -- so real.size()==4 covers TWO distinct shapes: LOAD/CONST/BINARY/RETURN (a
    // literal operand) and LOAD/LOAD/BINARY/RETURN (the same parameter on both sides), told apart
    // by the second instruction's own opcode.
    std::string second_param_temp;
    bool has_second_param_load = false;
    if (real.size() == 4) {
        const auto& second_instr = *real[idx];
        if (second_instr.op == BytecodeOp::Const) {
            if (second_instr.operands.size() != 2) return std::nullopt;
            if (second_instr.operands[1].empty() || second_instr.operands[1][0] != 'K') return std::nullopt;
            const auto const_index = ref_index(second_instr.operands[1], 'K', "constant");
            if (const_index >= module.constant_values.size() || !module.constant_values[const_index].is_number()) return std::nullopt;
            const_temp = second_instr.operands[0];
            const_value = module.constant_values[const_index].as_number();
            has_const = true;
        } else if (second_instr.op == BytecodeOp::Load) {
            if (second_instr.operands.size() != 2) return std::nullopt;
            if (second_instr.operands[1].empty() || second_instr.operands[1][0] != 'L' ||
                ref_index(second_instr.operands[1], 'L', "local") != param_local) {
                return std::nullopt;
            }
            second_param_temp = second_instr.operands[0];
            has_second_param_load = true;
        } else {
            return std::nullopt;
        }
        ++idx;
    }

    const auto& binary_instr = *real[idx];
    if (binary_instr.op != BytecodeOp::Binary || binary_instr.operands.size() != 4) return std::nullopt;
    const NumericOp op = binary_instr.prepared_numeric_op;
    if (op == NumericOp::Unknown || numeric_op_is_comparison(op) || op == NumericOp::Mod) return std::nullopt;
    // The left operand must be the loaded parameter -- JitLoopBodyOp always treats the local
    // operand as "left" (see its own comment), so a const-first expression like `RETURN 1 - x`
    // is deliberately not recognized here, same as every other shape this doesn't attempt.
    if (binary_instr.operands[2] != param_temp) return std::nullopt;
    const std::string& right_operand = binary_instr.operands[3];
    const std::string result_temp = binary_instr.operands[0];
    ++idx;
    if (has_const) {
        if (right_operand != const_temp) return std::nullopt;
    } else if (has_second_param_load) {
        if (right_operand != second_param_temp) return std::nullopt;
    } else if (right_operand != param_temp) {
        return std::nullopt; // the only other local in a 1-param leaf is that same parameter
    }

    if (idx >= real.size()) return std::nullopt;
    const auto& return_instr = *real[idx];
    if (return_instr.op != BytecodeOp::Return || return_instr.operands.size() != 2) return std::nullopt;
    if (return_instr.operands[1] != result_temp) return std::nullopt;

    InlinableLeaf leaf;
    leaf.op = op;
    leaf.right_is_const = has_const;
    leaf.right_constant = const_value;
    return leaf;
}

// Recognizes the exact 3-instruction shape ArcoBASIC compiles `dest = Callee(arg)` to inside a
// loop body -- `LOAD %ta L<arg>; CALL_VALUE %tr Callee %ta; STORE L<dest> %tr` (confirmed via
// `ArcoFission reveal ... --stage BYTECODE` on this exact statement) -- and, if `Callee` resolves
// to an try_resolve_inlinable_leaf-eligible function, returns a JitLoopBodyOp equivalent to
// inlining that call: no different from any other body_op compile_jit_loop already knows how to
// generate code for, so inlining a call needs no new codegen at all, only this detection.
std::optional<JitLoopBodyOp> try_inline_leaf_call(const BytecodeModule& module, const BytecodeInstruction& load_instr,
                                                   const BytecodeInstruction& call_instr, const BytecodeInstruction& store_instr) {
    if (load_instr.operands.size() != 2) return std::nullopt;
    if (load_instr.operands[0].empty() || load_instr.operands[0][0] != '%') return std::nullopt;
    if (load_instr.operands[1].empty() || load_instr.operands[1][0] != 'L') return std::nullopt;
    const std::size_t arg_local = ref_index(load_instr.operands[1], 'L', "local");

    // CALL_VALUE %result Name %arg -- exactly one argument, and it must be the value just loaded
    // (not some other temp that happens to be live), so this only ever fires for the specific
    // shape it was verified against.
    if (call_instr.operands.size() != 3) return std::nullopt;
    if (call_instr.operands[0].empty() || call_instr.operands[0][0] != '%') return std::nullopt;
    if (call_instr.operands[2] != load_instr.operands[0]) return std::nullopt;

    if (store_instr.operands.size() != 2) return std::nullopt;
    if (store_instr.operands[0].empty() || store_instr.operands[0][0] != 'L') return std::nullopt;
    if (store_instr.operands[1] != call_instr.operands[0]) return std::nullopt;
    const std::size_t dest_local = ref_index(store_instr.operands[0], 'L', "local");

    const BytecodeFunction* callee = find_function(module, call_instr.operands[1]);
    if (callee == nullptr) return std::nullopt;
    const auto leaf = try_resolve_inlinable_leaf(module, *callee);
    if (!leaf) return std::nullopt;

    JitLoopBodyOp body_op;
    body_op.op = leaf->op;
    body_op.dest_local = dest_local;
    body_op.left_local = arg_local;
    body_op.right_is_const = leaf->right_is_const;
    body_op.right_constant = leaf->right_constant;
    body_op.right_local = arg_local; // only meaningful when right_is_const is false -- see above
    return body_op;
}

// Returns std::nullopt if `function` isn't shaped like the one canonical counting-FOR-loop pattern
// this JIT targets (rejecting is always safe -- the interpreter just runs it normally either way),
// or a fully-populated JitLoopPlan ready for compile_jit_loop() if it is.
//
// Recognizes the shape structurally, by following the actual jump/branch targets prepare's own
// earlier pass has already resolved (function.targets), rather than trusting today's block-naming
// convention (ForCond0, ForBody3, ...) to still look the same in some future compiler change --
// the __fission_for_step naming is only used to find CANDIDATE loops worth examining in the first
// place, never to validate their shape. Every step from here on is a concrete structural check
// that either matches exactly or the whole function bails out to std::nullopt.
std::optional<JitLoopPlan> try_detect_jit_loop(const BytecodeModule& module, const BytecodeFunction& function,
                                                std::size_t step_local_index) {
    // 1. Find where the step local is initialized to a compile-time-constant number, in a block
    //    that ends with a plain JUMP -- that jump's target is the loop's condition block. Two
    //    shapes: a direct STORE_CONST (the implicit-step and positive-literal-step case), or a
    //    plain STORE from a temp that resolve_constant_number_for_temp can fold back to a
    //    constant (the `STEP -1` case, which lowers to CONST+UNARY(-)+STORE since the stored
    //    value comes from negating a constant, not referencing one directly).
    std::size_t cond_block = std::numeric_limits<std::size_t>::max();
    double step_constant = 0.0;
    for (std::size_t block_index = 0; block_index < function.blocks.size() && cond_block == std::numeric_limits<std::size_t>::max(); ++block_index) {
        const auto& block = function.blocks[block_index];
        for (std::size_t instr_index = 0; instr_index < block.instructions.size(); ++instr_index) {
            const auto& instr = block.instructions[instr_index];
            std::optional<double> resolved;
            if (instr.op == BytecodeOp::StoreConst && instr.operands.size() == 2 &&
                !instr.operands[0].empty() && instr.operands[0][0] == 'L' &&
                ref_index(instr.operands[0], 'L', "local") == step_local_index) {
                if (instr.operands[1].empty() || instr.operands[1][0] != 'K') return std::nullopt;
                const auto const_index = ref_index(instr.operands[1], 'K', "constant");
                if (const_index >= module.constant_values.size() || !module.constant_values[const_index].is_number()) return std::nullopt;
                resolved = module.constant_values[const_index].as_number();
            } else if (instr.op == BytecodeOp::Store && instr.operands.size() == 2 &&
                       !instr.operands[0].empty() && instr.operands[0][0] == 'L' &&
                       ref_index(instr.operands[0], 'L', "local") == step_local_index) {
                if (instr.operands[1].empty() || instr.operands[1][0] != '%') return std::nullopt;
                resolved = resolve_constant_number_for_temp(module, block, instr_index, ref_index(instr.operands[1], '%', "temporary"));
                if (!resolved) return std::nullopt; // step is genuinely dynamic -- not this JIT's problem
            } else {
                continue;
            }
            step_constant = *resolved;
            if (block.instructions.empty() || block.instructions.back().op != BytecodeOp::Jump ||
                block.instructions.back().prepared_targets.empty()) {
                return std::nullopt;
            }
            cond_block = block.instructions.back().prepared_targets.front().block;
            break;
        }
    }
    if (cond_block == std::numeric_limits<std::size_t>::max()) return std::nullopt;
    if (step_constant == 0.0) return std::nullopt; // direction undefined -- not this JIT's problem
    const bool ascending = step_constant > 0.0;

    // 2. cond_block must end in a plain two-way BRANCH (the step-sign check that picks which of
    //    the two comparisons below actually applies).
    if (cond_block >= function.blocks.size()) return std::nullopt;
    const auto& cond_instrs = function.blocks[cond_block].instructions;
    if (cond_instrs.empty() || cond_instrs.back().op != BytecodeOp::Branch || cond_instrs.back().prepared_targets.size() != 2) {
        return std::nullopt;
    }
    const std::size_t pos_block = cond_instrs.back().prepared_targets[0].block;
    const std::size_t neg_block = cond_instrs.back().prepared_targets[1].block;
    if (pos_block >= function.blocks.size() || neg_block >= function.blocks.size()) return std::nullopt;

    // 3. Both branches must be a single BRANCH_LOCAL_LOCAL comparing the same two locals (the
    //    loop counter and its end bound) and agreeing on where "continue"/"stop" lead -- only the
    //    one matching the statically-known step direction is the one actually taken at runtime,
    //    but requiring its sibling to match too is a real structural sanity check, not a
    //    formality: it confirms this is really the ascending/descending pair for ONE loop.
    const auto& pos_instrs = function.blocks[pos_block].instructions;
    const auto& neg_instrs = function.blocks[neg_block].instructions;
    if (pos_instrs.size() != 1 || pos_instrs[0].op != BytecodeOp::BranchLocalLocal || pos_instrs[0].operands.size() != 5) return std::nullopt;
    if (neg_instrs.size() != 1 || neg_instrs[0].op != BytecodeOp::BranchLocalLocal || neg_instrs[0].operands.size() != 5) return std::nullopt;
    const auto& taken_instr = ascending ? pos_instrs[0] : neg_instrs[0];
    const auto& other_instr = ascending ? neg_instrs[0] : pos_instrs[0];
    if (taken_instr.prepared_numeric_op != (ascending ? NumericOp::Le : NumericOp::Ge)) return std::nullopt;
    if (taken_instr.operands[1].empty() || taken_instr.operands[1][0] != 'L') return std::nullopt;
    if (taken_instr.operands[2].empty() || taken_instr.operands[2][0] != 'L') return std::nullopt;
    const std::size_t counter_local = ref_index(taken_instr.operands[1], 'L', "local");
    const std::size_t end_local = ref_index(taken_instr.operands[2], 'L', "local");
    if (other_instr.operands[1].empty() || other_instr.operands[1][0] != 'L' || ref_index(other_instr.operands[1], 'L', "local") != counter_local) return std::nullopt;
    if (other_instr.operands[2].empty() || other_instr.operands[2][0] != 'L' || ref_index(other_instr.operands[2], 'L', "local") != end_local) return std::nullopt;
    if (taken_instr.prepared_targets.size() != 2 || other_instr.prepared_targets.size() != 2) return std::nullopt;
    if (taken_instr.prepared_targets[0].block != other_instr.prepared_targets[0].block ||
        taken_instr.prepared_targets[1].block != other_instr.prepared_targets[1].block) {
        return std::nullopt;
    }
    const std::size_t body_block = taken_instr.prepared_targets[0].block;
    const std::size_t end_block = taken_instr.prepared_targets[1].block;
    if (body_block >= function.blocks.size()) return std::nullopt;

    // 4. The body must be a SINGLE block (no internal branching -- a real, deliberate v1
    //    restriction, not an oversight: nested control flow inside the loop body is simply not
    //    attempted by this JIT and falls back to interpretation) of straight-line arithmetic
    //    BINARY_LOCAL_LOCAL (`x = y + z`) or BINARY_LOCAL_CONST (`x = y + 2`) statements, OR a
    //    call to a try_resolve_inlinable_leaf-eligible function (`x = SimpleFn(y)`, inlined by
    //    try_inline_leaf_call rather than making the whole loop JIT-ineligible just because its
    //    body contains a call) (SOURCE instructions -- pure line-number bookkeeping, no runtime
    //    effect -- are skipped), ending in a plain JUMP to the increment block.
    const auto& body_instrs = function.blocks[body_block].instructions;
    if (body_instrs.empty() || body_instrs.back().op != BytecodeOp::Jump || body_instrs.back().prepared_targets.empty()) return std::nullopt;
    const std::size_t inc_block = body_instrs.back().prepared_targets.front().block;
    std::vector<JitLoopBodyOp> body_ops;
    std::vector<std::size_t> touched = {counter_local, end_local, step_local_index};
    const auto touch = [&](std::size_t index) {
        if (std::find(touched.begin(), touched.end(), index) == touched.end()) touched.push_back(index);
    };
    for (std::size_t i = 0; i + 1 < body_instrs.size();) { // excludes the trailing JUMP
        const auto& instr = body_instrs[i];
        if (instr.op == BytecodeOp::Source) { ++i; continue; }
        if (instr.op == BytecodeOp::Load && i + 3 < body_instrs.size() &&
            body_instrs[i + 1].op == BytecodeOp::CallValue && body_instrs[i + 2].op == BytecodeOp::Store) {
            auto inlined = try_inline_leaf_call(module, instr, body_instrs[i + 1], body_instrs[i + 2]);
            if (!inlined) return std::nullopt;
            touch(inlined->dest_local);
            touch(inlined->left_local);
            body_ops.push_back(*inlined);
            i += 3;
            continue;
        }
        const bool right_is_local = instr.op == BytecodeOp::BinaryLocalLocal;
        const bool right_is_const = instr.op == BytecodeOp::BinaryLocalConst;
        if ((!right_is_local && !right_is_const) || instr.operands.size() != 4) return std::nullopt;
        if (instr.prepared_numeric_op == NumericOp::Unknown || numeric_op_is_comparison(instr.prepared_numeric_op)) return std::nullopt;
        if (instr.prepared_numeric_op == NumericOp::Mod) return std::nullopt; // no native SSE2 remainder to translate this to
        if (instr.operands[0].empty() || instr.operands[0][0] != 'L') return std::nullopt;
        if (instr.operands[2].empty() || instr.operands[2][0] != 'L') return std::nullopt;
        JitLoopBodyOp body_op;
        body_op.op = instr.prepared_numeric_op;
        body_op.dest_local = ref_index(instr.operands[0], 'L', "local");
        body_op.left_local = ref_index(instr.operands[2], 'L', "local");
        touch(body_op.dest_local);
        touch(body_op.left_local);
        if (right_is_local) {
            if (instr.operands[3].empty() || instr.operands[3][0] != 'L') return std::nullopt;
            body_op.right_local = ref_index(instr.operands[3], 'L', "local");
            touch(body_op.right_local);
        } else {
            if (instr.operands[3].empty() || instr.operands[3][0] != 'K') return std::nullopt;
            const auto const_index = ref_index(instr.operands[3], 'K', "constant");
            if (const_index >= module.constant_values.size() || !module.constant_values[const_index].is_number()) return std::nullopt;
            body_op.right_is_const = true;
            body_op.right_constant = module.constant_values[const_index].as_number();
        }
        body_ops.push_back(body_op);
        ++i;
    }

    // 5. The increment block must be exactly `counter = counter + step` (dest and left both the
    //    counter local, right the step local, op Add -- not e.g. Sub with a negated step, which
    //    this detector deliberately does not also try to recognize) followed by a JUMP back to
    //    this same loop's own condition block, closing the loop.
    if (inc_block >= function.blocks.size()) return std::nullopt;
    const auto& inc_instrs = function.blocks[inc_block].instructions;
    if (inc_instrs.empty() || inc_instrs.back().op != BytecodeOp::Jump || inc_instrs.back().prepared_targets.empty()) return std::nullopt;
    if (inc_instrs.back().prepared_targets.front().block != cond_block) return std::nullopt;
    const BytecodeInstruction* inc_op = nullptr;
    for (std::size_t i = 0; i + 1 < inc_instrs.size(); ++i) {
        if (inc_instrs[i].op == BytecodeOp::Source) continue;
        if (inc_op != nullptr) return std::nullopt; // more than one real instruction -- not this shape
        inc_op = &inc_instrs[i];
    }
    if (inc_op == nullptr || inc_op->op != BytecodeOp::BinaryLocalLocal || inc_op->operands.size() != 4) return std::nullopt;
    if (inc_op->prepared_numeric_op != NumericOp::Add) return std::nullopt;
    if (inc_op->operands[0].empty() || inc_op->operands[0][0] != 'L' || ref_index(inc_op->operands[0], 'L', "local") != counter_local) return std::nullopt;
    if (inc_op->operands[2].empty() || inc_op->operands[2][0] != 'L' || ref_index(inc_op->operands[2], 'L', "local") != counter_local) return std::nullopt;
    if (inc_op->operands[3].empty() || inc_op->operands[3][0] != 'L' || ref_index(inc_op->operands[3], 'L', "local") != step_local_index) return std::nullopt;

    JitLoopPlan plan;
    plan.counter_local = counter_local;
    plan.end_local = end_local;
    plan.step_local = step_local_index;
    plan.step_constant = step_constant;
    plan.ascending = ascending;
    plan.body_ops = std::move(body_ops);
    plan.cond_block = cond_block;
    plan.end_block = end_block;
    plan.touched_locals = std::move(touched);
    return plan;
}

// The byte offset of BytecodeSlot::number within the struct, computed from a real instance rather
// than offsetof() (BytecodeSlot has a non-trivial Value member, making it not a standard-layout
// type in the formal sense offsetof requires) -- this is what lets JIT-compiled native code
// address `frame.locals[i].number` directly as `locals_base + i*sizeof(BytecodeSlot) + this
// offset`, the same memory the interpreter itself reads and writes for that local.
std::size_t bytecode_slot_number_offset() {
    static const std::size_t offset = [] {
        BytecodeSlot sample;
        return static_cast<std::size_t>(reinterpret_cast<const char*>(&sample.number) - reinterpret_cast<const char*>(&sample));
    }();
    return offset;
}

// Compiles `plan` to native code and maps it executable, filling in plan.entry on success. Safe
// to call more than once (idempotent no-op if plan.entry is already set) and safe to fail (leaves
// plan.entry null, so the caller keeps falling back to ordinary interpretation forever after --
// compilation failing is never something a JIT-eligible loop's actual execution should surface).
bool compile_jit_loop(const JitLoopPlan& plan) {
    if (plan.entry != nullptr) return true;
#if defined(__linux__)
    using arco::fission::jit::Condition;
    using arco::fission::jit::Gpr;
    using arco::fission::jit::JitAssembler;
    using arco::fission::jit::Xmm;

    const std::size_t slot_size = sizeof(BytecodeSlot);
    const std::size_t number_offset = bytecode_slot_number_offset();
    const auto local_disp = [&](std::size_t local_index) {
        return static_cast<std::int32_t>(local_index * slot_size + number_offset);
    };

    JitAssembler as;
    JitAssembler::Label cond_label;
    JitAssembler::Label exit_label;

    as.bind(cond_label);
    as.movsd_load(Xmm::XMM0, Gpr::RDI, local_disp(plan.counter_local));
    as.movsd_load(Xmm::XMM1, Gpr::RDI, local_disp(plan.end_local));
    as.ucomisd(Xmm::XMM0, Xmm::XMM1);
    // PF=1 means the comparison was unordered (either operand NaN); the interpreter's own numeric
    // comparisons already treat any NaN comparison as false, which for a loop CONTINUE-condition
    // means "stop" -- check that before trusting CF/ZF, exactly matching that semantic.
    as.jcc(Condition::ParityEven, exit_label);
    as.jcc(plan.ascending ? Condition::Above : Condition::Below, exit_label);

    for (const auto& body_op : plan.body_ops) {
        as.movsd_load(Xmm::XMM0, Gpr::RDI, local_disp(body_op.left_local));
        if (body_op.right_is_const) {
            // A literal (BINARY_LOCAL_CONST, e.g. `x = x * 2`) is embedded directly as its raw
            // IEEE-754 bit pattern rather than referenced by address -- RAX (never used for
            // locals_base, which stays in RDI throughout) is free as scratch here, and this keeps
            // the generated code fully self-contained, with no dependency on any other object's
            // memory staying alive or unmoved for as long as this compiled loop might run again.
            std::uint64_t bits = 0;
            std::memcpy(&bits, &body_op.right_constant, sizeof(bits));
            as.mov_reg_imm64(Gpr::RAX, bits);
            as.movq_xmm_reg(Xmm::XMM1, Gpr::RAX);
        } else {
            as.movsd_load(Xmm::XMM1, Gpr::RDI, local_disp(body_op.right_local));
        }
        switch (body_op.op) {
            case NumericOp::Add: as.addsd(Xmm::XMM0, Xmm::XMM1); break;
            case NumericOp::Sub: as.subsd(Xmm::XMM0, Xmm::XMM1); break;
            case NumericOp::Mul: as.mulsd(Xmm::XMM0, Xmm::XMM1); break;
            case NumericOp::Div: as.divsd(Xmm::XMM0, Xmm::XMM1); break;
            default: return false; // unreachable given try_detect_jit_loop's own filtering
        }
        as.movsd_store(Gpr::RDI, local_disp(body_op.dest_local), Xmm::XMM0);
    }

    as.movsd_load(Xmm::XMM0, Gpr::RDI, local_disp(plan.counter_local));
    as.movsd_load(Xmm::XMM1, Gpr::RDI, local_disp(plan.step_local));
    as.addsd(Xmm::XMM0, Xmm::XMM1);
    as.movsd_store(Gpr::RDI, local_disp(plan.counter_local), Xmm::XMM0);
    as.jmp(cond_label);

    as.bind(exit_label);
    as.ret();

    const auto& code = as.bytes();
    if (code.empty()) return false;

    const auto page_size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    const std::size_t mapped_size = ((code.size() + page_size - 1) / page_size) * page_size;
    void* mapped = mmap(nullptr, mapped_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) return false;
    std::memcpy(mapped, code.data(), code.size());
    // W^X: never both writable and executable at once -- mprotect to RX only after the code is
    // fully written, matching how build_native_bytecode's own approach to running generated code
    // separates "write" from "execute" (there via a real linked binary rather than a raw mapping,
    // but the same underlying principle).
    if (mprotect(mapped, mapped_size, PROT_READ | PROT_EXEC) != 0) {
        munmap(mapped, mapped_size);
        return false;
    }
    plan.executable.base = std::shared_ptr<void>(mapped, [mapped_size](void* p) { munmap(p, mapped_size); });
    plan.executable.size = mapped_size;
    plan.entry = reinterpret_cast<void (*)(BytecodeSlot*)>(mapped);
    return true;
#else
    return false; // Linux-only for now, matching this codebase's other native-execution paths
#endif
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
        function.param_local_indices.clear();
        function.param_defaults.clear();
        function.param_default_calls.clear();
        function.temp_count = 0;

        for (std::size_t local_index = 0; local_index < function.locals.size(); ++local_index) {
            function.local_refs_by_base.emplace(local_base_name(function.locals[local_index]), "L" + std::to_string(local_index));
        }

        function.param_local_refs.reserve(function.params.size());
        function.param_local_indices.reserve(function.params.size());
        function.param_defaults.reserve(function.params.size());
        function.param_default_calls.reserve(function.params.size());
        for (const auto& param : function.params) {
            const auto found = function.local_refs_by_base.find(local_base_name(param));
            function.param_local_refs.push_back(found == function.local_refs_by_base.end() ? std::string() : found->second);
            function.param_local_indices.push_back(found == function.local_refs_by_base.end()
                                                         ? static_cast<std::size_t>(-1)
                                                         : ref_index(found->second, 'L', "local"));
            const std::string default_text = param_default_text(param);
            if (default_text.empty()) {
                function.param_defaults.emplace_back();
                function.param_default_calls.emplace_back();
            } else if (looks_like_inline_bytecode_value(default_text)) {
                function.param_defaults.push_back(parse_constant_value(default_text));
                function.param_default_calls.emplace_back();
            } else {
                // See resolve_param_default_call()'s own comment. module.function_indices is
                // already populated above, before this per-function loop, so a default like
                // `Vec3(0, 0, 0)` can resolve against a class constructor compiled earlier in the
                // very same module.
                function.param_defaults.emplace_back();
                function.param_default_calls.push_back(resolve_param_default_call(module, default_text));
            }
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

        // Hot-numeric-loop JIT detection (see JitLoopPlan's own comment) -- must run after the
        // prepared_targets pass above, since try_detect_jit_loop follows those resolved jump/
        // branch cursors rather than re-resolving block names itself. __fission_for_step-prefixed
        // locals are this compiler's own naming convention for a FOR loop's step variable, used
        // here only to find candidate loops worth examining, never to validate their shape.
        function.jit_loops.clear();
        for (std::size_t local_index = 0; local_index < function.locals.size(); ++local_index) {
            if (function.locals[local_index].rfind("__fission_for_step", 0) != 0) continue;
            auto plan = try_detect_jit_loop(module, function, local_index);
            if (plan) function.jit_loops.push_back(std::move(*plan));
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

// execute_function() below runs once per ArcoBASIC function call (and recurses for nested calls --
// see its own several self-call sites), and until now allocated a brand new heap buffer for both
// frame.temps and frame.locals on every single invocation, freeing them the moment the call
// returned. Confirmed via gdb-sampling a real, deployed capsule (Arconaut) burning ~41% of a CPU
// core continuously while sitting completely idle: a GUI redraw loop calling a few dozen small
// helper functions per frame, at ~20fps, adds up to thousands of these alloc/free pairs a second
// just for call overhead, before any of the actual work inside those calls.
//
// This pool reuses the same handful of buffers across calls instead. acquire() reuses a spare
// buffer's existing capacity when one is available -- resize() that doesn't need to grow the
// underlying storage costs nothing -- and release() clears any BytecodeSlot contents (a returned
// buffer's slots can hold their own heap resources, e.g. an object/array literal a local variable
// was holding, which would otherwise stay alive indefinitely just sitting in the pool) before
// handing the buffer's now-empty capacity back for the next call to reuse. This is transparent to
// every caller: resize() value-initializes new elements exactly the same way whether or not the
// underlying storage came from a fresh allocation or a reused one, so BytecodeSlot's own semantics
// (every slot starts Undefined) are unchanged -- only whether malloc/free actually run changes.
//
// thread_local rather than one shared instance: execute_function() recurses for nested ArcoBASIC
// calls (matching the C++ call stack, so LIFO acquire/release naturally matches arbitrary
// recursion depth correctly), and this keeps the pool race-free even if the interpreter is ever
// driven from more than one thread, at the cost of one pool per thread rather than a single global.
class BytecodeFramePool {
public:
    std::vector<BytecodeSlot> acquire(std::size_t size) {
        std::vector<BytecodeSlot> buffer;
        if (!spares_.empty()) {
            buffer = std::move(spares_.back());
            spares_.pop_back();
        }
        buffer.resize(size);
        return buffer;
    }

    void release(std::vector<BytecodeSlot>&& buffer) {
        if (spares_.size() >= kMaxSpares) return; // don't let the pool itself grow unbounded
        buffer.clear();
        spares_.push_back(std::move(buffer));
    }

private:
    static constexpr std::size_t kMaxSpares = 64;
    std::vector<std::vector<BytecodeSlot>> spares_;
};

thread_local BytecodeFramePool g_bytecode_frame_pool;

// Returns a frame's temps/locals buffers to the pool no matter how execute_function() exits --
// a normal return, or one of its many throw paths -- without needing to touch every return site
// by hand.
class BytecodeFrameReturn {
public:
    explicit BytecodeFrameReturn(BytecodeFrame& frame) : frame_(frame) {}
    ~BytecodeFrameReturn() {
        g_bytecode_frame_pool.release(std::move(frame_.temps));
        g_bytecode_frame_pool.release(std::move(frame_.locals));
    }
    BytecodeFrameReturn(const BytecodeFrameReturn&) = delete;
    BytecodeFrameReturn& operator=(const BytecodeFrameReturn&) = delete;

private:
    BytecodeFrame& frame_;
};

// Built-in, toggleable per-function profiling for every ArcoFission capsule -- set
// ARCOFISSION_DEBUG=1 in the environment before running a capsule (any capsule; this lives in the
// same runtime every one of them embeds, not something specific to one program) and it prints a
// per-function call-count/timing summary to stderr when the program exits. Grew directly out of
// diagnosing a real, confirmed-but-then-mysterious performance report (a GUI capsule burning ~40%
// of a CPU core while completely idle): manual gdb-sampling to find the cause turned out to be
// unreliable (ptrace attach disproportionately catches a process mid-syscall, e.g. blocked in
// ppoll, which skews naive sampling toward "it's just waiting" even when real, frequent CPU work
// is also happening) -- exactly the kind of question this answers directly and precisely instead.
//
// Timing is inclusive (a function's own recorded time includes whatever it calls), which is
// simpler than tracking self-time and still answers "which function accounts for the most total
// time" -- the question that actually matters for spotting a hot call path. Zero overhead when the
// environment variable isn't set: the check happens once, cached, and record() is never called at
// all when profiling is off (see FunctionProfileScope below).
struct FunctionProfile {
    std::uint64_t calls = 0;
    double total_seconds = 0.0;
};

class CapsuleDebugProfiler {
public:
    static CapsuleDebugProfiler& instance() {
        static CapsuleDebugProfiler profiler;
        return profiler;
    }

    bool enabled() const { return enabled_; }

    // Guarded by a mutex rather than assumed single-threaded: this is opt-in, off-hot-path debug
    // code (zero cost when disabled, since record() is never called at all in that case -- see
    // FunctionProfileScope), so the lock cost here is a cheap, worthwhile safety margin against
    // whatever future capsule this ends up profiling, not just today's single-threaded ones.
    void record(const std::string& name, double seconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& entry = table_[name];
        entry.calls += 1;
        entry.total_seconds += seconds;
    }

    ~CapsuleDebugProfiler() {
        if (!enabled_ || table_.empty()) return;
        std::vector<std::pair<std::string, FunctionProfile>> rows(table_.begin(), table_.end());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            return a.second.total_seconds > b.second.total_seconds;
        });
        std::fprintf(stderr, "\n[ArcoFission debug] per-function profile (ARCOFISSION_DEBUG=1, inclusive timing)\n");
        std::fprintf(stderr, "%-40s %12s %14s %14s\n", "function", "calls", "total ms", "avg us/call");
        for (const auto& [name, profile] : rows) {
            const double total_ms = profile.total_seconds * 1000.0;
            const double avg_us = profile.calls != 0 ? (profile.total_seconds * 1000000.0 / static_cast<double>(profile.calls)) : 0.0;
            std::fprintf(stderr, "%-40s %12llu %14.2f %14.2f\n", name.c_str(),
                         static_cast<unsigned long long>(profile.calls), total_ms, avg_us);
        }
    }

    CapsuleDebugProfiler(const CapsuleDebugProfiler&) = delete;
    CapsuleDebugProfiler& operator=(const CapsuleDebugProfiler&) = delete;

private:
    CapsuleDebugProfiler() {
        const char* env = std::getenv("ARCOFISSION_DEBUG");
        enabled_ = env != nullptr && env[0] != '\0' && std::string(env) != "0";
    }

    bool enabled_ = false;
    std::mutex mutex_;
    std::unordered_map<std::string, FunctionProfile> table_;
};

class FunctionProfileScope {
public:
    // name is taken BY REFERENCE, not by value -- a real, caught-live bug in the first version of
    // this class took it by value, forcing a fresh std::string copy (a heap allocation, for any
    // non-SSO-length function name) on every single execute_function() call regardless of whether
    // profiling was even enabled. Confirmed via a real before/after A-B measurement of a deployed
    // capsule's idle CPU usage: 45.1% on the build before this class existed vs. 53.5% right after
    // adding it (with ARCOFISSION_DEBUG unset) -- the exact opposite of "zero overhead when off"
    // this feature was supposed to guarantee. function.name (passed in from execute_function(),
    // which owns the BytecodeFunction for its entire call) outlives this scope, so a reference is
    // always safe and the disabled case now does no allocation at all.
    FunctionProfileScope(const std::string& name, bool enabled) : name_(name), enabled_(enabled) {
        if (enabled_) start_ = std::chrono::steady_clock::now();
    }
    ~FunctionProfileScope() {
        if (!enabled_) return;
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
        CapsuleDebugProfiler::instance().record(name_, elapsed);
    }
    FunctionProfileScope(const FunctionProfileScope&) = delete;
    FunctionProfileScope& operator=(const FunctionProfileScope&) = delete;

private:
    const std::string& name_;
    bool enabled_;
    std::chrono::steady_clock::time_point start_;
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

    // Cached rather than calling CapsuleDebugProfiler::instance().enabled() directly here every
    // time: enabled_ is fixed from getenv() once at process start and never changes, so this
    // static local (initialized thread-safely exactly once, same pattern as jit_disabled below)
    // removes a Meyer's-singleton guard check plus a member call from every single execute_function
    // invocation -- found profiling a function-call-heavy loop alongside the param_local_indices
    // fix just above.
    static const bool debug_enabled = CapsuleDebugProfiler::instance().enabled();
    FunctionProfileScope profile_scope(function.name, debug_enabled);

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
    frame.temps = g_bytecode_frame_pool.acquire(function.temp_count);
    frame.locals = g_bytecode_frame_pool.acquire(function.locals.size());
    BytecodeFrameReturn frame_return(frame);
    for (std::size_t i = 0; i < function.param_local_refs.size(); ++i) {
        if (function.param_local_refs[i].empty()) {
            continue;
        }
        // Precomputed once by prepare_bytecode_module (see param_local_indices's own comment) --
        // was local_index_from_ref(function.param_local_refs[i]) here, re-parsing the same "L<n>"
        // text (with a heap-allocating substr) on every single call to every function with at
        // least one parameter, found via ARCOFISSION_DEBUG profiling a function-call-heavy loop.
        const std::size_t local_index = function.param_local_indices[i];
        if (local_index >= frame.locals.size()) {
            throw std::runtime_error("local index out of range: " + function.param_local_refs[i]);
        }
        if (i < args.size()) {
            set_local(function, frame, local_index, args[i], function.param_local_refs[i]);
        } else if (i < function.param_defaults.size() && function.param_defaults[i].has_value()) {
            set_local(function, frame, local_index, *function.param_defaults[i], function.param_local_refs[i]);
        } else if (i < function.param_default_calls.size() && function.param_default_calls[i].has_value()) {
            // A non-literal default (e.g. `Vec3(0, 0, 0)`) resolved at prepare time -- see
            // resolve_param_default_call(). Evaluated fresh on every call that actually needs it,
            // the same as a real expression default would be, rather than a single Value shared
            // (and potentially aliased/mutated) across every caller that omits this argument.
            const auto& [target_function, call_args] = *function.param_default_calls[i];
            set_local(function, frame, local_index, execute_function(module, *target_function, runtime, call_args, count_instructions),
                      function.param_local_refs[i]);
        }
    }

    BytecodeCursor cursor{0, 0};
    std::vector<TryHandler> try_stack;
    int current_source_line = 0;
    while (cursor.block < function.blocks.size()) {
        const BytecodeBlock& block = function.blocks[cursor.block];
        // Hot-numeric-loop JIT dispatch (see JitLoopPlan's own comment). function.jit_loops is
        // empty for the overwhelming majority of functions -- anything with no eligible FOR loop
        // -- in which case this whole check is just an empty-range for-loop, negligible next to
        // everything else this interpreter already does per instruction. Only even considered at
        // the START of a block (cursor.instruction == 0): a jump straight into the middle of what
        // would otherwise be a loop's condition block can't happen from this loop's own back-edge
        // (which always targets cond_block at instruction 0), and treating a mid-block landing as
        // eligible would be wrong regardless of how it happened.
        static const bool jit_disabled = [] {
            const char* env = std::getenv("ARCOFISSION_NO_JIT");
            return env != nullptr && env[0] != '\0' && std::string(env) != "0";
        }();
        if (cursor.instruction == 0 && !jit_disabled) {
            bool jit_dispatched = false;
            for (const auto& plan : function.jit_loops) {
                if (plan.cond_block != cursor.block) continue;
                bool eligible = true;
                for (const auto local_index : plan.touched_locals) {
                    if (local_index >= frame.locals.size() || frame.locals[local_index].kind != BytecodeSlot::Kind::Number) {
                        eligible = false;
                        break;
                    }
                }
                // ArcoBASIC is dynamically typed: nothing guarantees a local that held a Number
                // the first time this loop ran still does on a later call, or even a later
                // iteration reached some other way -- re-checked on every single entry into this
                // block, not just once. A failed check falls back to plain interpretation of this
                // block, below, exactly as if no plan existed -- never a wrong answer, at worst a
                // missed speedup for that one execution.
                if (eligible && (plan.entry != nullptr || compile_jit_loop(plan)) && plan.entry != nullptr) {
                    if (jit_diagnostics_enabled()) {
                        std::fprintf(stderr, "[ArcoFission jit] dispatching native loop in %s cond_block=%zu\n",
                                     function.name.c_str(), plan.cond_block);
                    }
                    plan.entry(frame.locals.data());
                    cursor = BytecodeCursor{plan.end_block, 0};
                    jit_dispatched = true;
                }
                break; // at most one plan can own a given cond_block
            }
            if (jit_dispatched) continue; // re-fetch `block` for the new cursor.block
        }
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
                        // Split EVERY segment, not just the first: `a.b.c.Method` must dispatch
                        // method `Method` on the value reached by walking a -> .b -> .c, not
                        // search for a method literally named "b.c.Method" on `a`. A real,
                        // reproduced bug (not hypothetical): `restoredRoot.LocalMesh.VertexCount()`
                        // was misdispatched as looking for "Component.LocalMesh.VertexCount",
                        // found nothing, and fell through to "unknown host function". Same bug
                        // family, and the same fix shape (last segment is the method, everything
                        // before it is a receiver path), as MethodCallExpr's fix in
                        // src/frontend/parser.cpp for the tree-walking interpreter side of this --
                        // that fix doesn't cover this bytecode-VM dispatch path since it's a
                        // wholly separate C++ implementation, confirmed by this exact case still
                        // failing here after that fix landed.
                        std::vector<std::string> parts;
                        std::size_t part_start = 0;
                        while (part_start <= instruction.operands[1].size()) {
                            const auto next_dot = instruction.operands[1].find('.', part_start);
                            parts.push_back(instruction.operands[1].substr(part_start, next_dot == std::string::npos ? std::string::npos : next_dot - part_start));
                            if (next_dot == std::string::npos) break;
                            part_start = next_dot + 1;
                        }
                        const std::string& receiver_name = parts.front();
                        const std::string& method_name = parts.back();
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
                        // Walk any intermediate field segments (parts[1 .. size-2]) to reach the
                        // real receiver the final segment's method dispatches against.
                        for (std::size_t i = 1; receiver_value.has_value() && i + 1 < parts.size(); ++i) {
                            receiver_value = runtime.get_member(*receiver_value, parts[i]);
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

// Same idea, but for arco_runtime_core ALONE (no arco_compiler_core/fission.cpp along for the
// ride) -- build_linux_native_image's own generic host-function bridge
// (src/native/host_bridge.cpp) needs exactly this and nothing more. Only present when the build
// tree opted in with `cmake --build . --target ArcoNativeRuntimeCoreProbe`; empty otherwise, which
// build_linux_native_image treats as "host-function support isn't available in this build tree
// yet" rather than failing every native build outright.
std::vector<std::string> native_runtime_core_link_dependencies(const std::filesystem::path& build_dir) {
    return native_link_dependencies_from(build_dir,
        std::filesystem::path("CMakeFiles") / "ArcoNativeRuntimeCoreProbe.dir" / "link.txt", {"libarco_runtime_core.a"});
}

// The FULL, GUI-capable runtime library (`arco_runtime` -- the exact same one `arco_cli`, the
// tree-walking interpreter, already links -- real GLFW/Pango/Cairo/GTK/OpenGL backend when this
// build tree found them, gcc/cmake's own `${ARCO_GUI_BACKEND_SOURCE}` selection, the identical
// fallback to `src/gui/stub_backend.cpp` as `arco_runtime_core` above when it didn't) -- used
// instead of the lean core library ONLY for a program that actually calls a `GUI.*` host function
// (see program_calls_gui_function), so it can actually put pixels on a real screen instead of
// hitting the stub backend's own `unsupported()` panic the moment it tries. Unlike
// `arco_runtime_core` (EXCLUDE_FROM_ALL, needs an explicit
// `cmake --build . --target ArcoNativeRuntimeCoreProbe` opt-in), `arco_runtime` and `arco_cli` are
// both ordinary default build targets -- already built in any normal dev/CI workflow that produced
// this very `ArcoFission` binary -- so this capability needs no separate opt-in step at all. Reuses
// `arco_cli`'s own executable link line (a static library has no link.txt of its own; only a
// linked executable/shared-library target does) via the identical link.txt-probing trick every
// other native-link-dependency helper in this file already uses. Empty when `arco_cli`'s own
// link.txt doesn't exist yet (this build tree hasn't been built at all), which callers treat as
// "the GUI-capable runtime isn't available here yet" rather than failing outright.
std::vector<std::string> native_gui_runtime_link_dependencies(const std::filesystem::path& build_dir) {
    return native_link_dependencies_from(build_dir,
        std::filesystem::path("CMakeFiles") / "arco_cli.dir" / "link.txt", {"libarco_runtime.a"});
}

// True if `module` calls a `GUI.*` host function anywhere (a plain `Kind::CallValue` whose target
// starts with the literal prefix "GUI." -- GUI.Window, GUI.Clear, GUI.WaitEvent, and so on all
// reach here identically, since none of them have dedicated native codegen of their own; every one
// falls through to the generic host-function bridge the same way any other unrecognized host call
// does). Used to decide which prebuilt runtime library build_linux_native_image's own host-function
// bridge should link against: this walks the WHOLE module (every function, not just Main) because a
// GUI call inside a user-declared FUNCTION/CLASS method is exactly as real a GUI program as one
// written directly at script scope -- Arconaut itself calls GUI.* exclusively from inside its own
// Draw*/event-handling functions, never from Main directly.
bool program_calls_gui_function(const AmirModule& module) {
    for (const auto& function : module.functions) {
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if (instruction.kind == AmirInstruction::Kind::CallValue &&
                    instruction.target.rfind("GUI.", 0) == 0) {
                    return true;
                }
            }
        }
    }
    return false;
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
        // Set below, only meaningful for .html output -- see the -sSINGLE_FILE=1 block and the
        // post-build verification that has to branch on it too.
        bool single_file_requested = false;

        std::vector<std::string> args{
            compiler,
            "-std=c++17",
            // -Os (optimize for size) rather than -O2: this workload is draw-call and simple-math
            // bound, not CPU-bound, so the runtime cost of -Os over -O2 is negligible while the
            // wasm binary itself comes out meaningfully smaller -- matters a lot more here than in
            // a typical native build, since this binary is downloaded fresh by every visitor.
            "-Os",
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
        // The default Emscripten HTML shell's "powered by emscripten" progress/status UI is
        // vestigial for ArcoFission capsules (canvas_backend.cpp creates its own full-viewport
        // <canvas> for GUI capsules) and its progress-hiding logic doesn't reliably clear once
        // the module is actually running -- observed stuck forever on "Downloading..." even after
        // a GUI capsule had fully loaded and was drawing. Only relevant when producing a .html at
        // all (a bare .js/.wasm output has no shell to speak of).
        if (std::filesystem::path(output).extension() == ".html") {
            args.push_back("--shell-file");
            args.push_back((source_root / "src" / "gui" / "web_shell.html").string());
            // Browsers refuse to fetch() a separate .wasm across a file:// origin (CORS), which
            // is the *default* Emscripten output shape (a bare fetch of a sibling .wasm file) --
            // meaning double-clicking the .html and opening it directly aborts with "both async
            // and sync fetching of the wasm failed" unless it's served over real HTTP first.
            // -sSINGLE_FILE=1 embeds the wasm as base64 directly inside the generated JS (itself
            // inlined into this single .html, since there's no separate -o .js target here) --
            // one self-contained file, loadable by just opening it, no server needed at all, the
            // same way a Godot web export or any other single-file wasm deliverable works. Comes
            // at some cost (larger file via base64 overhead, a base64-decode at every load
            // instead of a binary fetch) that's a reasonable tradeoff for "just works," and is
            // only applied for .html output -- a caller explicitly asking for separate .js/.wasm
            // (say, for a real CDN-backed deployment where the fetch/caching tradeoff runs the
            // other way) still gets that. A caller that IS deploying behind a real HTTP server
            // (a CDN, a normal web host -- anywhere that isn't a bare file:// double-click) can
            // opt out with ARCOFISSION_WEB_SINGLE_FILE=0 to get the separate-files shape instead:
            // a real binary .wasm fetched once and cacheable by the browser/CDN independently of
            // the .html/.js, and compressible in transit the way base64-inside-JS isn't nearly as
            // effectively -- both add up to a real difference for repeat visitors and for the
            // first paint on a slow connection.
            const char* env_single_file = std::getenv("ARCOFISSION_WEB_SINGLE_FILE");
            single_file_requested = !env_single_file || std::string(env_single_file) != "0";
            if (single_file_requested) {
                args.push_back("-sSINGLE_FILE=1");
            }
        }

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
        // -sSINGLE_FILE=1 means .html output has no sibling .wasm to check anymore (the module is
        // embedded as base64 inside the .html itself) -- confirm that file exists and is a
        // plausible size instead. Without it (either non-.html output, which never gets
        // -sSINGLE_FILE, or .html built with ARCOFISSION_WEB_SINGLE_FILE=0 above), a real sibling
        // .wasm exists and gets the real magic-number check -- a small .html shell next to it is
        // then correct, not a sign anything went wrong.
        if (single_file_requested) {
            std::error_code size_error;
            const auto html_size = std::filesystem::file_size(output, size_error);
            if (size_error || html_size < 1024) {
                return {false, "", "Emscripten compiler did not produce a usable " + output};
            }
        } else {
            const std::filesystem::path wasm_output = std::filesystem::path(output).replace_extension(".wasm");
            if (!is_wasm_file(wasm_output)) {
                return {false, "", "Emscripten compiler did not produce a valid wasm module"};
            }
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

// Lets a running capsule compile-and-run ArcoBASIC source *in-process*, using the exact same
// compiler this capsule itself was built with -- the fallback examples/arcoflow.abas's Run button
// (Process.Run -> a separate ArcoFission compile-run invocation) needs on a target with no real
// subprocess sandbox to spawn that separate process in. The web/WASM target is exactly that: there
// is no popen() equivalent in a browser, so Process.Run fails immediately there (GUI.Backend() ==
// "canvas" is how ArcoBASIC code tells that case apart from a desktop build with a real ArcoFission
// binary reachable). Mirrors Process.Run's own {Ok, Output, ExitCode, Error} shape (see
// process_run in runtime.cpp) so callers can treat the two uniformly.
void register_self_compile_run(Runtime& runtime) {
    runtime.register_function("ArcoFission.CompileRunSource", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("ArcoFission.CompileRunSource expects one source-code argument");
        }
        const Result result = compile_run(args[0].to_string(), "ArcoFission.CompileRunSource");
        return Value::Object{{"Ok", result.ok}, {"Output", result.ok ? result.output : result.error},
                             {"ExitCode", result.ok ? 0.0 : 1.0}, {"Error", result.error}};
    });
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

// Phase 1 of the native (no-bytecode-VM) Linux backend -- see
// .agents/reports/ARCO_NATIVE_COMPILER_BACKEND_PLAN.md. Compiles straight to real x86-64 machine
// code via generate_x86_64_program(..., CallingConvention::SystemV) -- the same AMIR-driven pass
// build_efi_image above uses for UEFI, just targeting a different ABI and a different "how do I
// reach the outside world" story (PRINT calls into the native runtime ABI,
// include/arco/native_runtime_abi.h + src/native/runtime_abi.cpp, instead of a UEFI protocol
// method) -- then hands the result to the system assembler/linker exactly the way
// build_native_bytecode already does for the bytecode-VM-embedding capsule format, reusing that same
// "shell out to `c++`" mechanism rather than a second one. No bytecode, no embedded VM, no
// execute_function anywhere in the produced binary.
Result build_linux_native_image(const std::string& source, const std::string& source_name,
                                 const std::string& entry_function, const std::string& output_path,
                                 NativeDebugOptions debug_options) {
#if defined(__linux__)
    try {
        Runtime runtime;
        const std::string processed = runtime.preprocess_source(source);
        Lexer lexer(processed);
        auto tokens = lexer.scan_tokens();

        Parser parser(tokens, runtime.compile_metadata().runtime_mode == "NONE");
        auto statements = parser.parse();

        const AmirModule amir = build_amir(statements, source_name, runtime.compile_metadata().instruction_limit);
        // Unlike the UEFI/freestanding entry points above (build_efi_image, reveal_x86_64), this
        // deliberately does NOT treat amir.diagnostics as build-fatal -- report_integer_error's own
        // call sites (build_amir's lower_expression) enforce freestanding "systems profile"
        // constraints (fixed-width integer type matching, no BOOL arithmetic, IF/WHILE conditions
        // must be BOOL "under #RUNTIME NONE", etc.) UNCONDITIONALLY, regardless of target -- they
        // have no convention/runtime_mode gate of their own. Those constraints are real and correct
        // for the freestanding profile but do not apply to ordinary hosted ArcoBASIC, which is
        // dynamically typed (`"y=" + TRUE` is legitimate dynamic behavior, matching eval_binary's
        // own bool-to-number coercion -- a real case caught by direct testing: this backend
        // rejected it at build time while compile_run/arco_cli both correctly compute "y=TRUE").
        // Matches compile_run's own behavior exactly (it calls build_amir directly into
        // build_bytecode with no diagnostics check at all) rather than being needlessly stricter
        // than the ground truth this backend is diffed against everywhere else. A genuinely
        // unlowerable AMIR shape is still caught downstream by generate_x86_64_program's own
        // extensive per-instruction error checking, the same safety net execute_bytecode implicitly
        // relies on for the exact same reason.
        auto codegen = generate_x86_64_program(amir, entry_function, systems::CallingConvention::SystemV, debug_options.annotate);
        if (!codegen.ok) {
            return {false, "", codegen.error};
        }
        // Renamed to the literal symbol "main" for the C runtime startup path (_start ->
        // __libc_start_main -> main) to find, decoupled from entry_function (which must match the
        // real ArcoBASIC-declared/synthesized function name, "Main", for generate_x86_64_program to
        // find it in the AMIR module at all). The entry function always has zero declared
        // parameters (a top-level ArcoBASIC program never declares argc/argv), so main's own
        // incoming RDI/RSI (argc/argv, real values from the C runtime) are simply never spilled
        // anywhere by this function's prologue -- harmless, not a correctness issue: the SysV ABI
        // never requires a callee to consume arguments it has no parameters for.
        codegen.entry_symbol = "main";

        const std::filesystem::path source_root = source_root_path();
        const std::filesystem::path build_dir = current_executable_dir();
        const std::filesystem::path shim_path = source_root / "src" / "native" / "runtime_abi.cpp";
        // arco::Value is inline-defined except for its RuntimeHandle constructor/as_handle()
        // (src/runtime/runtime_handles.cpp) -- to_string() calls the latter from a branch the
        // compiler can't prove dead, so it's a real link dependency the moment to_string() is used
        // (arco_value_print, below), not merely a theoretical one. Compiled directly as an extra
        // source file, the same "shell out to c++ with raw sources" shape as everything else this
        // function does, rather than linking the full arco_runtime static library and its own much
        // larger dependency footprint (GUI/network/...) for two small functions.
        const std::filesystem::path runtime_handles_path = source_root / "src" / "runtime" / "runtime_handles.cpp";
        if (!std::filesystem::exists(shim_path) || !std::filesystem::exists(runtime_handles_path)) {
            return {false, "", "native ArcoSH runtime ABI sources not found under " + source_root.string() +
                " -- run from a full source checkout"};
        }
        // The generic host-function bridge (arco_call_host, src/native/host_bridge.cpp) needs a
        // real arco::Runtime -- the SAME ~244-entry host-function dispatch table
        // (Runtime::call_host_function) the interpreter/bytecode VM already use for everything
        // this compiler backend doesn't hand-roll its own native codegen for (string/array/math
        // utilities and more), so a call this backend's own Kind::CallValue case doesn't recognize
        // falls through to it instead of failing to compile outright. Linked against the
        // PREBUILT lean `arco_runtime_core` static library (CMakeLists.txt's own "runtime without
        // the optional GUI/network backends" precedent, src/gui/stub_backend.cpp instead of a real
        // GTK/GLFW backend) via the exact same link.txt-probing trick native_core_link_dependencies
        // already uses for the bytecode-capsule format -- NOT compiled from raw source here: a
        // direct attempt at that (this function's own earlier draft) hit a real, confirmed
        // undefined-reference wall at `arco::graphics::*` (arcology-os/src/graphics/graphics.cpp,
        // a whole separate subsystem `arco_runtime_core` itself links via `arcology_os` in
        // CMakeLists.txt) -- enumerating every transitive source file by hand here would silently
        // rot the moment that dependency graph changes, exactly what a real prebuilt library
        // dependency (parsed from CMake's own generated link.txt, not hand-maintained) avoids.
        //
        // Only attempted at all when the program actually calls a host function this backend
        // doesn't already have dedicated codegen for (codegen.external_calls contains
        // "arco_call_host") -- deliberately, not merely as an optimization: a real regression
        // caught by direct testing, not assumed, was linking this UNCONDITIONALLY whenever
        // ArcoNativeRuntimeCoreProbe's own link.txt happened to exist, which broke EVERY native
        // build (even a plain `PRINT "hello"` with no host-function call at all) the moment
        // libarco_runtime_core.a itself was moved/missing -- native_link_dependencies_from only
        // confirms link.txt exists, never that the .a file it names has actually been built.
        // Gating on whether the PROGRAM needs it at all sidesteps that false-positive risk
        // entirely for the overwhelming majority of programs, which don't.
        const bool program_needs_host_bridge = std::any_of(codegen.external_calls.begin(), codegen.external_calls.end(),
            [](const auto& external_call) { return external_call.symbol == "arco_call_host"; });
        // A program that calls a GUI.* function specifically needs the FULL, GLFW-capable runtime
        // (native_gui_runtime_link_dependencies), not the lean/stub one -- see
        // program_calls_gui_function's own comment. Every other host-function call (string/array/
        // math utilities and everything else this backend doesn't hand-roll native codegen for)
        // keeps using the lean core exactly as before; this is a strictly additive capability, not
        // a change to any program that doesn't touch GUI.* at all.
        const bool program_needs_gui_runtime = program_needs_host_bridge && program_calls_gui_function(amir);
        std::vector<std::string> host_bridge_link_dependencies;
        std::filesystem::path host_bridge_path;
        if (program_needs_host_bridge) {
            host_bridge_link_dependencies = program_needs_gui_runtime
                ? native_gui_runtime_link_dependencies(build_dir)
                : native_runtime_core_link_dependencies(build_dir);
            // native_link_dependencies_from only confirms CMake's own generated link.txt exists
            // and is readable -- NOT that the .a files it names have actually been built (a real,
            // confirmed gap: moving libarco_runtime_core.a aside left link.txt itself untouched,
            // so this check is the only thing standing between a clear, actionable error here and
            // a confusing linker "cannot find ... .a: No such file" further down).
            const bool any_archive_missing = std::any_of(host_bridge_link_dependencies.begin(), host_bridge_link_dependencies.end(),
                [](const std::string& dependency) {
                    return dependency.size() > 2 && dependency.compare(dependency.size() - 2, 2, ".a") == 0 &&
                        !std::filesystem::exists(dependency);
                });
            if (host_bridge_link_dependencies.empty() || any_archive_missing) {
                return {false, "", program_needs_gui_runtime
                    ? "this program calls a GUI.* function; the full, GUI-capable runtime isn't "
                      "available in this build tree -- run `cmake --build . --target arco_cli` "
                      "first, then rebuild"
                    : "this program calls a host function this backend doesn't have "
                      "dedicated native codegen for; the generic host-function bridge isn't available "
                      "in this build tree -- run `cmake --build . --target ArcoNativeRuntimeCoreProbe` "
                      "first, then rebuild"};
            }
            host_bridge_path = source_root / "src" / "native" / "host_bridge.cpp";
            if (!std::filesystem::exists(host_bridge_path)) {
                return {false, "", "native ArcoSH host-function bridge source not found under " + source_root.string() +
                    " -- run from a full source checkout"};
            }
        }

        const std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() /
            ("arcofission-linux-native-" + std::to_string(static_cast<long long>(::getpid())));
        std::filesystem::create_directories(tmp_dir);
        const std::filesystem::path asm_path = tmp_dir / "program.s";
        {
            std::ofstream out(asm_path);
            if (!out) {
                std::filesystem::remove_all(tmp_dir);
                return {false, "", "could not write generated assembly"};
            }
            out << render_x86_64_linux_asm(codegen);
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
        };
        // Arco native debugger tooling (`--sanitize`): AddressSanitizer plus real debug symbols.
        // This is exactly the workflow that found and fixed a real, AddressSanitizer-confirmed SEGV
        // and narrowed a real, still-open FOR-EACH-loop reference-counting bug down to a minimal
        // repro (RFC-0049 Entry 21) -- previously done by hand-patching this exact args list,
        // rebuilding ArcoFission itself, then reverting the patch afterward. A permanent, supported
        // CLI flag instead.
        if (debug_options.sanitize) {
            args.push_back("-g");
            args.push_back("-fsanitize=address");
        }
        args.insert(args.end(), {
            asm_path.string(),
            shim_path.string(),
            runtime_handles_path.string(),
        });
        if (program_needs_host_bridge) args.push_back(host_bridge_path.string());
        args.insert(args.end(), {
            "-o",
            output_path,
            "-I" + (source_root / "include").string(),
            // Real IEEE-754 fmod (hosted MOD's own semantics -- generate_x86_64_function's Binary
            // case calls it directly as an external symbol rather than hand-rolling x87 FPREM's
            // partial-remainder loop). Explicit rather than relying on transitive linkage through
            // libstdc++: harmless if the toolchain would have pulled it in anyway.
            "-lm",
        });
        if (program_needs_host_bridge) {
            args.insert(args.end(), host_bridge_link_dependencies.begin(), host_bridge_link_dependencies.end());
        }
        std::ostringstream command;
        bool first = true;
        for (const auto& arg : args) {
            if (!first) command << ' ';
            first = false;
            command << shell_quote(arg);
        }

        // Arco native debugger tooling (`--debug`): the annotated .s file is the actual deliverable
        // of that flag -- copied out to a stable path BEFORE the temp directory it was built in
        // gets removed below, regardless of whether the compile itself succeeds (an assembler
        // error is exactly when seeing the annotated source is most useful).
        std::filesystem::path saved_asm_path;
        if (debug_options.annotate) {
            saved_asm_path = std::filesystem::path(output_path).string() + ".s";
            std::error_code copy_error;
            std::filesystem::copy_file(asm_path, saved_asm_path, std::filesystem::copy_options::overwrite_existing, copy_error);
            if (copy_error) saved_asm_path.clear();
        }

        const int status = std::system(command.str().c_str());
        std::filesystem::remove_all(tmp_dir);
        if (status == -1) {
            return {false, "", "could not launch C++ compiler/assembler"};
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            return {false, "", "assembler/linker failed while building native ELF64"};
        }
        if (!is_elf64_file(output_path)) {
            return {false, "", "toolchain did not produce an ELF64 executable"};
        }
        ::chmod(output_path.c_str(), 0755);

        std::ostringstream message;
        message << "SOURCE ACCEPTED\n";
        message << "STRUCTURE ASSEMBLED\n";
        message << "X86_64 GENERATED (System V, no bytecode VM)\n";
        if (!saved_asm_path.empty()) {
            message << "ANNOTATED ASSEMBLY WRITTEN " << saved_asm_path.string() << "\n";
        }
        if (debug_options.sanitize) {
            message << "ADDRESSSANITIZER ENABLED (real debug symbols, -fsanitize=address)\n";
        }
        // Only mentioned when this specific program actually needed it (and, having reached this
        // point at all, successfully linked it -- the unavailable case already returned a clear
        // error above, before ever invoking the linker). A program that calls no host function
        // gets no mention at all, exactly like every build before this feature existed.
        if (program_needs_host_bridge) {
            message << (program_needs_gui_runtime
                ? "HOST FUNCTION BRIDGE LINKED (real GUI backend -- arco_cli's own runtime library)\n"
                : "HOST FUNCTION BRIDGE LINKED (arco::Runtime's own host-function library)\n");
        }
        message << "ELF64 WRITTEN " << output_path << "\n";
        return {true, message.str(), ""};
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
#else
    (void)source; (void)source_name; (void)entry_function; (void)output_path; (void)debug_options;
    return {false, "", "the native Linux x86-64 backend is only supported on Linux"};
#endif
}

Result build_linux_native_image_file(const std::string& path, const std::string& entry_function, const std::string& output_path,
                                      NativeDebugOptions debug_options) {
    try {
        return build_linux_native_image(read_file(path), path, entry_function, output_path, debug_options);
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
    std::ostringstream output;
    try {
        Runtime runtime;
        runtime.set_instruction_limit_policy(true);
        runtime.set_instruction_limit_override(instruction_limit_override);
        runtime.set_output(output);
        auto module = parse_bytecode(bytecode);
        prepare_bytecode_module(module);
        const bool count_instructions = !(instruction_limit_override.has_value() && *instruction_limit_override == 0);
        (void)execute_bytecode(module, runtime, count_instructions);
        return {true, output.str(), ""};
    } catch (const ExitSignal& signal) {
        // Exit()/ExitTheProgram() (registered as a core builtin -- see Runtime's constructor in
        // runtime.cpp) is a real, immediate process exit, not an ordinary runtime error: flush
        // whatever output already accumulated, matching what a normal successful run would have
        // printed, then actually terminate with the requested code instead of reporting a
        // synthetic Result failure that would misrepresent a clean exit as a crash.
        std::cout << output.str();
        std::exit(signal.code());
    } catch (const std::exception& error) {
        return {false, "", error.what()};
    }
}

Result run_bytecode_binary(const std::string& bytecode, std::optional<std::size_t> instruction_limit_override,
                           const std::vector<std::string>& script_args) {
    std::ostringstream output;
    try {
        Runtime runtime;
        runtime.set_instruction_limit_policy(true);
        runtime.set_instruction_limit_override(instruction_limit_override);
        runtime.set_output(output);
        register_self_compile_run(runtime);
        Value::Array args_array;
        args_array.reserve(script_args.size());
        for (const auto& arg : script_args) args_array.emplace_back(arg);
        runtime.set_global("Args", Value(std::move(args_array)));
        auto module = parse_binary_bytecode(bytecode);
        prepare_bytecode_module(module);
        const bool count_instructions = !(instruction_limit_override.has_value() && *instruction_limit_override == 0);
        (void)execute_bytecode(module, runtime, count_instructions);
        return {true, output.str(), ""};
    } catch (const ExitSignal& signal) {
        // See run_bytecode()'s identical catch above -- this is the path every native capsule
        // actually runs through (native_launcher_source() calls this), so this is what makes
        // ExitTheProgram() in a compiled arco3d program (or any other native capsule) actually
        // terminate the process instead of crashing with an uncaught C++ exception.
        std::cout << output.str();
        std::exit(signal.code());
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
    std::ostringstream output;
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
        runtime.set_output(output);
        register_self_compile_run(runtime);
        auto module = build_bytecode(build_amir(
            statements, source_name, preprocess_runtime.compile_metadata().instruction_limit));
        prepare_bytecode_module(module);
        const bool count_instructions = !(instruction_limit_override.has_value() && *instruction_limit_override == 0);
        (void)execute_bytecode(module, runtime, count_instructions);
        return {true, output.str(), ""};
    } catch (const ExitSignal& signal) {
        // See run_bytecode()/run_bytecode_binary()'s identical catch above.
        std::cout << output.str();
        std::exit(signal.code());
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
