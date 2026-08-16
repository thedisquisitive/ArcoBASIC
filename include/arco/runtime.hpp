#pragma once

#include "arco/value.hpp"
#include "arco/runtime_handles.hpp"
#include "arco/resource_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace arco {

class Pcg32;

struct RuntimeLimits {
    std::size_t instruction_limit = 100000;
};

struct RunResult {
    bool ok = true;
    std::string error;
    bool exited = false;
    int exit_code = 0;
};

struct CompileMetadata {
    std::string version;
    std::string author;
    std::string description;
    std::string entry;
    std::vector<std::string> targets;
    std::vector<std::string> requirements;
    std::vector<std::string> features;
    std::vector<std::string> warnings;
    std::vector<std::string> todos;
    std::vector<std::string> notes;
    std::vector<std::string> imports;
    std::vector<std::string> attributes;
    std::string pack;
    std::string align;
    std::string endian;
    bool strict = false;
    bool experimental = false;
    bool deprecated = false;
    // Systems-target directives (arcology-os/docs/systems/uefi-target.md section 2).
    std::string profile;        // #PROFILE value, e.g. "UEFI"; empty = no systems profile
    std::string runtime_mode;   // #RUNTIME value, e.g. "NONE"; empty = hosted runtime
    std::string arch;           // architecture selected via #TARGET under a systems profile
    std::string callconv;       // #CALLCONV value, e.g. "UEFI"
    std::string export_symbol;  // #EXPORT value, e.g. "efi_main"
    std::optional<std::uint64_t> instruction_limit; // hosted #INSTRUCTION_LIMIT request
};

struct ClassMetadata {
    std::string name;
    std::string parent;
    std::vector<std::string> interfaces;
    std::vector<std::string> abstract_methods;
    std::unordered_map<std::string, int> fields_access;
    std::unordered_map<std::string, std::string> fields_type;
    std::unordered_map<std::string, int> methods_access;
};

struct MethodSignature {
    std::string name;
    std::vector<std::string> param_types;
    std::string return_type;
};

struct InterfaceMetadata {
    std::string name;
    std::vector<MethodSignature> methods;
};

struct CallableDescriptor {
    std::string name;
    std::optional<Value> receiver;
};

class ExitSignal final : public std::exception {
public:
    explicit ExitSignal(int code);
    const char* what() const noexcept override;
    int code() const noexcept;

private:
    int code_;
};

class ReturnSignal final : public std::exception {
public:
    explicit ReturnSignal(Value value);
    const char* what() const noexcept override;
    const Value& value() const noexcept;

private:
    Value value_;
};

class GotoSignal final : public std::exception {
public:
    explicit GotoSignal(int line);
    const char* what() const noexcept override;
    int line() const noexcept;

private:
    int line_;
};

class StopSignal final : public std::exception {
public:
    const char* what() const noexcept override;
};

class UserError final : public std::exception {
public:
    UserError(std::string message, int source_line = 0, int source_column = 0);
    const char* what() const noexcept override;
    int source_line() const noexcept;
    int source_column() const noexcept;

private:
    std::string message_;
    int source_line_;
    int source_column_;
};

class Runtime {
public:
    using HostFunction = std::function<Value(const std::vector<Value>&)>;

    Runtime();

    RunResult run_string(const std::string& code);
    const CompileMetadata& compile_metadata() const;

    void register_function(const std::string& name, HostFunction function);
    bool has_function(const std::string& name) const;
    void register_class(std::string name, std::string parent = "", std::vector<std::string> interfaces = {});
    void register_class_field(const std::string& class_name, const std::string& field_name, int access, const std::string& type_name = "");
    void register_class_method(const std::string& class_name, const std::string& method_name, int access);
    void register_interface(std::string name, std::vector<MethodSignature> methods);
    std::vector<MethodSignature> interface_methods(const std::string& interface_name) const;
    void set_class_abstract_methods(const std::string& class_name, std::vector<std::string> methods);
    std::vector<std::string> class_abstract_methods(const std::string& class_name) const;
    bool implements_interface(const Value& value, const std::string& interface_name) const;
    bool is_instance_of(const Value& value, const std::string& class_name) const;
    bool value_matches_type(const Value& value, const std::string& type_name) const;
    void push_class_context(std::string class_name);
    void pop_class_context();
    void set_global(const std::string& name, Value value);
    void set_indexed(const std::string& name, const std::vector<int>& indexes, Value value);
    Value get_global(const std::string& name) const;
    bool has_global(const std::string& name) const;
    Value make_reference_to(std::string name, std::string type_name = "") const;
    Value make_reference(Value value, std::string type_name = "") const;
    Value reference_value(const Value& reference) const;
    void set_reference_value(Value reference, Value value);
    void clear_reference(Value reference) const;
    bool is_reference(const Value& value) const;
    void push_scope();
    void pop_scope();

    void set_output(std::ostream& output);
    std::ostream& output();

    void set_limits(RuntimeLimits limits);
    const RuntimeLimits& limits() const;
    void set_instruction_limit_policy(bool allow_source_requests,
                                      std::optional<std::size_t> hard_maximum = std::nullopt);
    void set_instruction_limit_override(std::optional<std::size_t> limit);
    void prepare_execution(std::optional<std::uint64_t> source_request = std::nullopt);
    void tick();
    void reset_instruction_count();

    Value call_host_function(const std::string& name, const std::vector<Value>& args);
    // Fast path for a bare (unqualified) call name whose host_functions_ key a caller has
    // already lowercased once (e.g. cached at bytecode-prepare time). Skips call_host_function's
    // own re-lowering and its namespaced-class member-access check, both moot for a name with
    // no '.' -- callers must only use this for names verified dot-free.
    Value call_host_function_prepared(const std::string& lowered_key, const std::vector<Value>& args);
    Value call_method(Value receiver, const std::string& method, const std::vector<Value>& args);
    Value make_callable(const std::string& name, std::optional<Value> receiver = std::nullopt,
                        bool require_registered = true);
    bool is_callable(const Value& value) const;
    CallableDescriptor callable_descriptor(const Value& value) const;
    Value call_callable(const Value& callable, const std::vector<Value>& args);

    RuntimeHandleTable& object_handles() { return object_handles_; }
    const RuntimeHandleTable& object_handles() const { return object_handles_; }
    ResourceRegistry& resources() { return resources_; }
    const ResourceRegistry& resources() const { return resources_; }

    std::string preprocess_source(const std::string& code);
    std::string preprocess_source(const std::string& code, bool reset_metadata);

private:
    std::string current_class_context() const;
    std::string member_declaring_class(const std::string& runtime_class, const std::string& member, bool method) const;
    void ensure_member_access(const std::string& runtime_class, const std::string& member, bool method) const;
    void ensure_field_assignment_type(const std::string& runtime_class, const std::string& field, const Value& value) const;

    RuntimeLimits limits_;
    RuntimeLimits effective_limits_;
    bool allow_source_instruction_limits_ = false;
    std::optional<std::size_t> instruction_limit_hard_maximum_;
    std::optional<std::size_t> instruction_limit_override_;
    std::size_t instruction_count_ = 0;
    std::ostream* output_;
    CompileMetadata metadata_;
    std::unordered_map<std::string, Value> globals_;
    std::vector<std::unordered_map<std::string, Value>> scopes_;
    std::unordered_map<std::string, HostFunction> host_functions_;
    std::unordered_map<std::string, ClassMetadata> classes_;
    std::unordered_map<std::string, InterfaceMetadata> interfaces_;
    std::vector<std::string> class_contexts_;
    RuntimeHandleTable object_handles_;
    ResourceRegistry resources_;
    std::shared_ptr<Pcg32> default_random_;
    std::optional<RuntimeHandle> primary_surface_handle_;
};

} // namespace arco
