add_executable(arco_tests tests/unit/runtime_tests.cpp)
target_link_libraries(arco_tests PRIVATE arco)

add_executable(resource_registry_tests tests/unit/resource_registry_tests.cpp)
target_link_libraries(resource_registry_tests PRIVATE arco_runtime)

add_executable(random_tests tests/unit/random_tests.cpp)
target_link_libraries(random_tests PRIVATE arco_runtime)

add_test(NAME arco_runtime_tests COMMAND arco_tests)
add_test(NAME resource_registry_tests COMMAND resource_registry_tests)
add_test(NAME random_tests COMMAND random_tests)

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
    random_integration_smoke
    tests/integration/random_smoke.sh
    $<TARGET_FILE:arco_cli>
    $<TARGET_FILE:ArcoFission>
    ${CMAKE_CURRENT_SOURCE_DIR}
)

include(arcology-os/cmake/Testing.cmake)
include(arcology-commons/cmake/Testing.cmake)
