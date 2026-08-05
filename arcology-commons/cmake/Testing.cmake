add_executable(arcology_commons_tests arcology-commons/tests/unit/arcology_commons_tests.cpp)
target_link_libraries(arcology_commons_tests PRIVATE arco)
add_test(NAME arcology_commons_unit_tests COMMAND arcology_commons_tests)
set_tests_properties(arcology_commons_unit_tests PROPERTIES
    ENVIRONMENT "ARCOBASIC_STDLIB=${CMAKE_CURRENT_SOURCE_DIR}/arcology-commons/stdlib"
)
