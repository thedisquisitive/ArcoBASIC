add_executable(arco_tests tests/unit/runtime_tests.cpp)
target_link_libraries(arco_tests PRIVATE arco)

add_executable(resource_registry_tests tests/unit/resource_registry_tests.cpp)
target_link_libraries(resource_registry_tests PRIVATE arco_runtime)

add_executable(random_tests tests/unit/random_tests.cpp)
target_link_libraries(random_tests PRIVATE arco_runtime)

add_executable(arcfs_host_tests tests/unit/arcfs_host_tests.cpp)
target_link_libraries(arcfs_host_tests PRIVATE arco_arcfs_host)

add_test(NAME arco_runtime_tests COMMAND arco_tests)
add_test(NAME resource_registry_tests COMMAND resource_registry_tests)
add_test(NAME random_tests COMMAND random_tests)
add_test(NAME arcfs_host_tests COMMAND arcfs_host_tests)
if(TARGET arcfs-linux)
    add_test(
        NAME mount_arcfs_helper_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/mount_arcfs_helper_smoke.sh
                ${CMAKE_CURRENT_BINARY_DIR}/packaging/linux/mount.arcfs
                $<TARGET_FILE:arcfs-linux>
                ${CMAKE_CURRENT_BINARY_DIR}/packaging/linux/61-arcfs.rules
                $<TARGET_FILE:arcfs_host_tests>
                ${CMAKE_CURRENT_BINARY_DIR}/packaging/linux/mkfs.arcfs
    )
endif()

if(TARGET arcfsctl)
    add_test(
        NAME arcfsctl_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/arcfsctl_smoke.sh
                ${CMAKE_CURRENT_BINARY_DIR}/generated/arcfsctl
    )
endif()

if(TARGET arconaut)
    add_test(
        NAME arconaut_capsule_smoke
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/generated/arconaut --smoke
    )
endif()

function(arco_add_script_test name script)
    add_test(
        NAME ${name}
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/${script} ${ARGN}
    )
endfunction()

arco_add_script_test(
    arcosh_alpha_smoke
    tests/integration/arcosh_alpha_smoke.sh
    $<TARGET_FILE:arcosh>
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
    ${CMAKE_COMMAND}
)
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
    random_integration_smoke
    tests/integration/random_smoke.sh
    $<TARGET_FILE:arco_cli>
    $<TARGET_FILE:ArcoFission>
    ${CMAKE_CURRENT_SOURCE_DIR}
)

include(arcology-os/cmake/Testing.cmake)
include(arcology-commons/cmake/Testing.cmake)
