add_executable(arco_tests tests/unit/runtime_tests.cpp)
target_link_libraries(arco_tests PRIVATE arco)

add_test(NAME arco_runtime_tests COMMAND arco_tests)

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

include(arcology-os/cmake/Testing.cmake)
include(arcology-commons/cmake/Testing.cmake)
