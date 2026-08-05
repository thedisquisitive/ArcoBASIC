#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArcoRuntime ArcoRuntime;

ArcoRuntime* arco_create_runtime(void);
void arco_destroy_runtime(ArcoRuntime* runtime);

int arco_run_string(ArcoRuntime* runtime, const char* code);
const char* arco_last_error(ArcoRuntime* runtime);

#ifdef __cplusplus
}
#endif

