#pragma once

#include "arco/value.hpp"

#include <cstddef>
#include <exception>
#include <functional>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

namespace arco {

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

class Runtime {
public:
    using HostFunction = std::function<Value(const std::vector<Value>&)>;

    Runtime();

    RunResult run_string(const std::string& code);
    const CompileMetadata& compile_metadata() const;

    void register_function(const std::string& name, HostFunction function);
    void set_global(const std::string& name, Value value);
    void set_indexed(const std::string& name, const std::vector<int>& indexes, Value value);
    Value get_global(const std::string& name) const;
    bool has_global(const std::string& name) const;
    void push_scope();
    void pop_scope();

    void set_output(std::ostream& output);
    std::ostream& output();

    void set_limits(RuntimeLimits limits);
    const RuntimeLimits& limits() const;
    void tick();
    void reset_instruction_count();

    Value call_host_function(const std::string& name, const std::vector<Value>& args);

private:
    std::string preprocess_source(const std::string& code);
    std::string preprocess_source(const std::string& code, bool reset_metadata);

    RuntimeLimits limits_;
    std::size_t instruction_count_ = 0;
    std::ostream* output_;
    CompileMetadata metadata_;
    std::unordered_map<std::string, Value> globals_;
    std::vector<std::unordered_map<std::string, Value>> scopes_;
    std::unordered_map<std::string, HostFunction> host_functions_;
};

} // namespace arco
