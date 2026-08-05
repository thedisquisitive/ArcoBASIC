#include "arco/c/arco_c_api.h"

#include "arco/runtime.hpp"

#include <exception>
#include <string>

struct ArcoRuntime {
    arco::Runtime runtime;
    std::string last_error;
};

ArcoRuntime* arco_create_runtime(void) {
    try {
        return new ArcoRuntime();
    } catch (...) {
        return nullptr;
    }
}

void arco_destroy_runtime(ArcoRuntime* runtime) {
    delete runtime;
}

int arco_run_string(ArcoRuntime* runtime, const char* code) {
    if (!runtime) {
        return 1;
    }
    if (!code) {
        runtime->last_error = "code cannot be null";
        return 1;
    }
    const auto result = runtime->runtime.run_string(code);
    runtime->last_error = result.error;
    return result.ok ? 0 : 1;
}

const char* arco_last_error(ArcoRuntime* runtime) {
    if (!runtime) {
        return "runtime cannot be null";
    }
    return runtime->last_error.c_str();
}
