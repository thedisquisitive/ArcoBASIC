cmake_minimum_required(VERSION 3.16)

project(ArcoBASIC VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(arco
    core/lexer.cpp
    core/parser.cpp
    runtime/runtime.cpp
    shell/arcosh.cpp
    bindings/c/arco_c_api.cpp
)

target_include_directories(arco
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/bindings/c
        ${CMAKE_CURRENT_SOURCE_DIR}/bindings/cpp
)

add_executable(arco_cli tools/arco_cli.cpp)
target_link_libraries(arco_cli PRIVATE arco)

add_executable(arcosh tools/arcosh_main.cpp)
target_link_libraries(arcosh PRIVATE arco)

add_executable(arco_tests tests/runtime_tests.cpp)
target_link_libraries(arco_tests PRIVATE arco)

enable_testing()
add_test(NAME arco_runtime_tests COMMAND arco_tests)
add_test(
    NAME arcosh_alpha_smoke
    COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/arcosh_alpha_smoke.sh
        $<TARGET_FILE:arcosh>
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}
        ${CMAKE_COMMAND}
)

install(TARGETS arcosh arco_cli
    RUNTIME DESTINATION bin
)

install(DIRECTORY stdlib/
    DESTINATION share/arcobasic/stdlib
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas"
)

install(DIRECTORY tutorials/
    DESTINATION share/arcosh/tutorials
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas"
)

install(DIRECTORY examples/
    DESTINATION share/arcobasic/examples
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas" PATTERN "*.arcsh"
)

install(DIRECTORY docs/
    DESTINATION share/doc/arcobasic
    FILES_MATCHING PATTERN "*.md"
)

install(FILES readme.md arcosh.md
    DESTINATION share/doc/arcobasic
)
