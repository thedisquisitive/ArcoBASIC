# A deliberately tiny, fast CMake project used only by tests/integration/fissure_build_smoke.sh --
# NOT built against this repository's own (large) CMakeLists.txt, so M2's cmake.ab adapter can be
# tested quickly and in isolation rather than risking a slow/invasive reconfigure of the real,
# huge arcobasic build tree.
cmake_minimum_required(VERSION 3.16)
project(FissureTinyCMakeProject CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(mathlib STATIC src/mathlib.cpp)
add_executable(app src/main.cpp)
target_link_libraries(app PRIVATE mathlib)
