# Fissure: adaptive regression impact analysis (fissure/docs/fissure-rfc.md, fissure/AGENT_PROGRESS.md).
# Mirrors arcfs-utils/CMakeLists.txt's own established shape for a subdirectory tool: a static
# library for the native core, linked against ArcoBASIC::runtime (the same embedding boundary
# arco_cli/ArcoFission/arcfs-utils already use), plus its own CLI executable. Gated on SQLite3
# being available (cmake/Dependencies.cmake) the same conservative way GUI/FUSE3 support is
# elsewhere in this project -- build without it rather than fail the whole configure.

if(NOT ARCO_SQLITE3_FOUND)
    message(STATUS "Fissure: sqlite3 not found via pkg-config, skipping (install libsqlite3-dev to enable)")
    return()
endif()

add_library(fissure_core STATIC
    core/graph/types.cpp
    core/graph/graph.cpp
    core/graph/store.cpp
    core/events/events.cpp
    core/impact/impact.cpp
    core/platform/posix.cpp
    core/scheduler/scheduler.cpp
    core/vm/manifest.cpp
    core/vm/vm.cpp
)
arco_configure_library(fissure_core)
target_include_directories(fissure_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(fissure_core PUBLIC ArcoBASIC::runtime PkgConfig::SQLITE3)
add_library(Fissure::core ALIAS fissure_core)

add_executable(fissure apps/fissure/main.cpp)
target_link_libraries(fissure PRIVATE Fissure::core)

# Deliberate deviation from the RFC's own suggested layout (fissure/docs/fissure-rfc.md section
# 19 lists a fissure/tests/ subtree): this repository's OWN established convention -- confirmed by
# reading cmake/Testing.cmake and arcfs-utils/CMakeLists.txt, whose own test .cpp file lives at
# the top-level tests/unit/, not arcfs-utils/tests/ -- centralizes every subsystem's test
# registration in the top-level tests/{unit,integration}/ + cmake/Testing.cmake, regardless of
# which subdirectory owns the code under test. Fissure follows that convention instead of adding a
# second, inconsistent pattern; see tests/unit/fissure_tests.cpp and
# tests/integration/fissure_smoke.sh, registered in cmake/Testing.cmake. Recorded here and in
# fissure/AGENT_PROGRESS.md per the packet's own section 22.8 ("record any proposed architectural
# deviation... before implementing it").
