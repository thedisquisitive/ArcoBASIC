cmake_minimum_required(VERSION 3.16)

project(ArcoBASIC VERSION 0.1.0 LANGUAGES CXX C)

# The documented dev workflow (docs/arcofission.md: `cmake -S . -B build`, no flags) previously
# left CMAKE_BUILD_TYPE unset for a single-config generator, which for GCC/Clang means no -O flag
# at all (effectively -O0) -- every ArcoBASIC bytecode capsule (arcfsctl, arconaut, ...) embeds
# arco_runtime's bytecode interpreter loop directly, so an unoptimized dev build isn't just a
# slower compiler -- it's a measurably slower VM inside every capsule ArcoFission produces. Found
# by a real, reported "laggy/randomly unresponsive" Arconaut session: switching an existing build/
# tree from the empty default to RelWithDebInfo alone dropped its idle CPU from ~71% to ~44%, no
# source change involved. RelWithDebInfo (not plain Release) so -g stays on -- this project does a
# lot of real native-crash/perf debugging (see project memory) and losing symbols by default would
# trade one footgun for another. Only applies to single-config generators (Makefiles/Ninja) --
# multi-config generators (Visual Studio, Xcode) pick the config at build time via
# CMAKE_CONFIGURATION_TYPES instead, and forcing CMAKE_BUILD_TYPE there does nothing useful.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Build type (Debug/Release/RelWithDebInfo/MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ARCO_ENABLE_GUI "Build the desktop GUI backend when dependencies are available" ON)
option(ARCO_ENABLE_NETWORK "Build HTTP networking helpers when libcurl is available" ON)

if(EMSCRIPTEN)
    # Emscripten disables C++ exception support by default (it costs code size); this codebase
    # uses try/catch pervasively (every ArcoFission entry point, Runtime::run_string, ...) as its
    # primary error-reporting mechanism, so leaving the default in place doesn't fail to build --
    # it builds fine and then aborts the whole wasm module the moment anything actually throws.
    add_compile_options(-fexceptions)
    add_link_options(-fexceptions)
endif()

include(cmake/Dependencies.cmake)

function(arco_configure_library target)
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
endfunction()

add_subdirectory(arcology-os)

# The frontend AST doubles as the hosted interpreter's executable tree, and Runtime invokes that
# same frontend. Keeping them together avoids an artificial static-library cycle.
add_library(arco_runtime STATIC
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/runtime/runtime.cpp
    src/runtime/random.cpp
    src/runtime/runtime_handles.cpp
    src/runtime/resource_registry.cpp
    src/gui/arcoui/geometry.cpp
    src/gui/arcoui/gesture.cpp
    src/gui/arcoui/core.cpp
    src/gui/arcoui/bindings.cpp
    ${ARCO_GUI_BACKEND_SOURCE}
)
arco_configure_library(arco_runtime)
target_link_libraries(arco_runtime PUBLIC arcology_os)
add_library(ArcoBASIC::runtime ALIAS arco_runtime)

if(ARCO_GUI_BACKEND_AVAILABLE AND EMSCRIPTEN)
    target_compile_definitions(arco_runtime PRIVATE ARCO_GUI_CANVAS=1)
    # -sASYNCIFY: see src/gui/canvas_backend.cpp's file-level comment -- lets the blocking-style
    # `WHILE ... GUI.WaitEvent(...) ... WEND` loop every GUI capsule is written with actually work
    # in a browser without rewriting the VM/runtime into a callback state machine.
    target_link_options(arco_runtime PUBLIC "-sASYNCIFY" "-sALLOW_MEMORY_GROWTH=1")
elseif(ARCO_GUI_BACKEND_AVAILABLE)
    target_link_libraries(arco_runtime
        PUBLIC
            PkgConfig::GLFW3
            PkgConfig::PANGOCAIRO
            PkgConfig::GTK3
            OpenGL::GL
    )
    target_compile_definitions(arco_runtime PRIVATE ARCO_GUI_GLFW=1)
endif()

if(CURL_FOUND)
    target_link_libraries(arco_runtime PUBLIC CURL::libcurl)
    target_compile_definitions(arco_runtime PRIVATE ARCO_NETWORK_CURL=1)
endif()

add_library(arco_compiler STATIC
    src/compiler/fission.cpp
)
arco_configure_library(arco_compiler)
target_link_libraries(arco_compiler PUBLIC arco_runtime arcology_os)
target_compile_definitions(arco_compiler PRIVATE ARCO_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_library(ArcoBASIC::compiler ALIAS arco_compiler)

# A second, lean build of the runtime/compiler libraries without the optional GUI/network
# backends, so native capsule builds have a portable option for programs that don't call real
# GUI.* functions (see fission.cpp's bytecode_needs_full_runtime) instead of always inheriting
# ArcoFission's own GTK/GLFW/curl dependency footprint. Only worth building when the primary
# arco_runtime above actually differs from this -- i.e. when those were found in the first place
# -- and EXCLUDE_FROM_ALL so it doesn't add build time to every default build; opt in with
# `cmake --build . --target ArcoFissionCapsuleCoreProbe`.
if(ARCO_GUI_BACKEND_AVAILABLE OR CURL_FOUND)
    add_library(arco_runtime_core STATIC EXCLUDE_FROM_ALL
        src/frontend/lexer.cpp
        src/frontend/parser.cpp
        src/runtime/runtime.cpp
        src/runtime/random.cpp
        src/runtime/runtime_handles.cpp
        src/runtime/resource_registry.cpp
        src/gui/arcoui/geometry.cpp
        src/gui/arcoui/gesture.cpp
        src/gui/arcoui/core.cpp
        src/gui/arcoui/bindings.cpp
        src/gui/stub_backend.cpp
    )
    arco_configure_library(arco_runtime_core)
    target_link_libraries(arco_runtime_core PUBLIC arcology_os)

    add_library(arco_compiler_core STATIC EXCLUDE_FROM_ALL src/compiler/fission.cpp)
    arco_configure_library(arco_compiler_core)
    target_link_libraries(arco_compiler_core PUBLIC arco_runtime_core arcology_os)
    target_compile_definitions(arco_compiler_core PRIVATE ARCO_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(ArcoFissionCapsuleCoreProbe EXCLUDE_FROM_ALL apps/arcofission/capsule_core_probe.cpp)
    target_link_libraries(ArcoFissionCapsuleCoreProbe PRIVATE arco_compiler_core)

    # Same probe trick, but for arco_runtime_core ALONE (no arco_compiler_core/fission.cpp) -- the
    # native (no-bytecode-VM) Linux backend's own generic host-function bridge
    # (arco_call_host, src/native/host_bridge.cpp) needs a real arco::Runtime but must NOT drag
    # ArcoFission's own compiler implementation into every generated native binary the way
    # ArcoFissionCapsuleCoreProbe's own link.txt would. Opt in with
    # `cmake --build . --target ArcoNativeRuntimeCoreProbe`.
    add_executable(ArcoNativeRuntimeCoreProbe EXCLUDE_FROM_ALL apps/arcofission/runtime_core_probe.cpp)
    target_link_libraries(ArcoNativeRuntimeCoreProbe PRIVATE arco_runtime_core)
endif()

add_library(arco_c_api STATIC src/bindings/c_api.cpp)
arco_configure_library(arco_c_api)
target_link_libraries(arco_c_api PUBLIC arco_runtime)
add_library(ArcoBASIC::c_api ALIAS arco_c_api)

# Compatibility target for existing in-tree and embedding consumers that previously linked the
# monolithic `arco` target.
add_library(arco INTERFACE)
target_link_libraries(arco INTERFACE arco_runtime arco_compiler arco_c_api)
add_library(ArcoBASIC::all ALIAS arco)

add_executable(arco_cli apps/arco/main.cpp)
target_link_libraries(arco_cli PRIVATE arco_runtime)

add_executable(ArcoFission apps/arcofission/main.cpp)
target_link_libraries(ArcoFission PRIVATE arco_compiler)

# ArcFS host-utility suite (arcfsctl/Arconaut capsules, the Linux FUSE mount daemon, and their
# packaging) is entirely self-contained under arcfs-utils/ -- see that directory's own
# CMakeLists.txt. Must come after arco_configure_library (defined above) and ArcoFission (just
# defined) since it uses both.
add_subdirectory(arcfs-utils)

# Fissure: adaptive regression impact analysis (fissure/docs/fissure-rfc.md). Self-contained under
# fissure/ -- see that directory's own CMakeLists.txt. Must come after arco_configure_library
# (defined above), which it uses the same way arcfs-utils does.
add_subdirectory(fissure)

# Rivet: build orchestration (rivet/docs/rivet-rfc.md). Self-contained under rivet/ -- see that
# directory's own CMakeLists.txt. Must come after arco_configure_library (defined above), which
# it uses the same way arcfs-utils/fissure do.
add_subdirectory(rivet)

if(BUILD_TESTING)
    include(cmake/Testing.cmake)
endif()

include(cmake/Install.cmake)
