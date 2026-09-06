add_executable(arco_tests tests/unit/runtime_tests.cpp)
target_link_libraries(arco_tests PRIVATE arco)

add_executable(resource_registry_tests tests/unit/resource_registry_tests.cpp)
target_link_libraries(resource_registry_tests PRIVATE arco_runtime)

add_executable(random_tests tests/unit/random_tests.cpp)
target_link_libraries(random_tests PRIVATE arco_runtime)

add_executable(arcfs_host_tests tests/unit/arcfs_host_tests.cpp)
target_link_libraries(arcfs_host_tests PRIVATE arco_arcfs_host)

add_executable(arcoui_core_tests tests/unit/arcoui_core_tests.cpp)
target_link_libraries(arcoui_core_tests PRIVATE arco_runtime)

# Header-only (include/arco/jit_x86_64.hpp has no .cpp of its own) -- links against arco_runtime
# purely to inherit its public include path, the same way every other test here does.
add_executable(jit_x86_64_tests tests/unit/jit_x86_64_tests.cpp)
target_link_libraries(jit_x86_64_tests PRIVATE arco_runtime)

add_test(NAME arco_runtime_tests COMMAND arco_tests)
add_test(NAME resource_registry_tests COMMAND resource_registry_tests)
add_test(NAME random_tests COMMAND random_tests)
add_test(NAME arcfs_host_tests COMMAND arcfs_host_tests)
add_test(NAME arcoui_core_tests COMMAND arcoui_core_tests)
add_test(NAME jit_x86_64_tests COMMAND jit_x86_64_tests)
if(TARGET arcfs-linux)
    add_test(
        NAME mount_arcfs_helper_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/mount_arcfs_helper_smoke.sh
                ${ARCFS_UTILS_BUILD_DIR}/packaging/linux/mount.arcfs
                $<TARGET_FILE:arcfs-linux>
                ${ARCFS_UTILS_BUILD_DIR}/packaging/linux/61-arcfs.rules
                $<TARGET_FILE:arcfs_host_tests>
                ${ARCFS_UTILS_BUILD_DIR}/packaging/linux/mkfs.arcfs
    )
endif()

if(TARGET arcfsctl)
    add_test(
        NAME arcfsctl_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/arcfsctl_smoke.sh
                ${ARCFS_UTILS_BUILD_DIR}/generated/arcfsctl
    )
endif()

if(TARGET arconaut)
    add_test(
        NAME arconaut_capsule_smoke
        COMMAND ${ARCFS_UTILS_BUILD_DIR}/generated/arconaut --smoke
    )
endif()

function(arco_add_script_test name script)
    add_test(
        NAME ${name}
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/${script} ${ARGN}
    )
endfunction()

arco_add_script_test(
    arcofission_alpha_smoke
    tests/integration/arcofission_alpha_smoke.sh
    $<TARGET_FILE:ArcoFission>
    ${CMAKE_CURRENT_SOURCE_DIR}
)
arco_add_script_test(
    arcofission_windows_capsule_smoke
    tests/integration/arcofission_windows_capsule_smoke.sh
    $<TARGET_FILE:ArcoFission>
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_COMMAND}
)
arco_add_script_test(
    jit_loop_smoke
    tests/integration/jit_loop_smoke.sh
    $<TARGET_FILE:ArcoFission>
    ${CMAKE_CURRENT_SOURCE_DIR}
)
arco_add_script_test(
    linux_native_backend_smoke
    tests/integration/linux_native_backend_smoke.sh
    $<TARGET_FILE:ArcoFission>
    ${CMAKE_CURRENT_SOURCE_DIR}
)

arco_add_script_test(
    random_integration_smoke
    tests/integration/random_smoke.sh
    $<TARGET_FILE:arco_cli>
    $<TARGET_FILE:ArcoFission>
    ${CMAKE_CURRENT_SOURCE_DIR}
)

include(arcology-os/cmake/Testing.cmake)
include(arcology-commons/cmake/Testing.cmake)
