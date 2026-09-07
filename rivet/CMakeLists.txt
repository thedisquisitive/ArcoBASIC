# Rivet: build orchestration (rivet/docs/rivet-rfc.md, rivet/RIVET_PROGRESS.md). Mirrors
# fissure/CMakeLists.txt's own established shape: a static library for the native core, linked
# against ArcoBASIC::runtime (the same embedding boundary arco_cli/ArcoFission/fissure already
# use), plus its own CLI executable. Gated on SQLite3 being available the same conservative way
# GUI/FUSE3/Fissure's own SQLite3 support already is elsewhere in this project -- build without it
# rather than fail the whole configure.

if(NOT ARCO_SQLITE3_FOUND)
    message(STATUS "Rivet: sqlite3 not found via pkg-config, skipping (install libsqlite3-dev to enable)")
    return()
endif()

find_package(Threads REQUIRED)

add_library(rivet_core STATIC
    core/action/action.cpp
    core/graph/graph.cpp
    core/fingerprint/fingerprint.cpp
    core/store/store.cpp
    core/cache/cache.cpp
    core/scheduler/scheduler.cpp
    core/platform/posix.cpp
    core/toolchain/cxx.cpp
    core/vm/vm.cpp
)
arco_configure_library(rivet_core)
target_include_directories(rivet_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(rivet_core PUBLIC ArcoBASIC::runtime PkgConfig::SQLITE3 Threads::Threads)
add_library(Rivet::core ALIAS rivet_core)

add_executable(rivet apps/rivet/main.cpp)
target_link_libraries(rivet PRIVATE Rivet::core)

# Test source files live under the top-level tests/{unit,integration}/ tree, centrally registered
# in cmake/Testing.cmake -- this repo's own established convention (confirmed via
# arcfs-utils/CMakeLists.txt's own precedent, and already followed the same way by
# fissure/CMakeLists.txt for its own RFC's suggested-but-different layout), not a
# rivet/tests/ subtree.
