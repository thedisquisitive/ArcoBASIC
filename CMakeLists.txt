cmake_minimum_required(VERSION 3.16)

project(ArcoBASIC VERSION 0.1.0 LANGUAGES CXX C)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ARCO_ENABLE_GUI "Build the desktop GUI backend when dependencies are available" ON)
option(ARCO_ENABLE_NETWORK "Build HTTP networking helpers when libcurl is available" ON)

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
    ${ARCO_GUI_BACKEND_SOURCE}
)
arco_configure_library(arco_runtime)
target_link_libraries(arco_runtime PUBLIC arcology_os)
add_library(ArcoBASIC::runtime ALIAS arco_runtime)

if(ARCO_GUI_BACKEND_AVAILABLE)
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
endif()

add_library(arco_shell STATIC src/shell/arcosh.cpp)
arco_configure_library(arco_shell)
target_link_libraries(arco_shell PUBLIC arco_runtime)
add_library(ArcoBASIC::shell ALIAS arco_shell)

add_library(arco_c_api STATIC src/bindings/c_api.cpp)
arco_configure_library(arco_c_api)
target_link_libraries(arco_c_api PUBLIC arco_runtime)
add_library(ArcoBASIC::c_api ALIAS arco_c_api)

# Compatibility target for existing in-tree and embedding consumers that previously linked the
# monolithic `arco` target.
add_library(arco INTERFACE)
target_link_libraries(arco INTERFACE arco_runtime arco_compiler arco_shell arco_c_api)
add_library(ArcoBASIC::all ALIAS arco)

add_executable(arco_cli apps/arco/main.cpp)
target_link_libraries(arco_cli PRIVATE arco_runtime)

add_executable(arcosh apps/arcosh/main.cpp)
target_link_libraries(arcosh PRIVATE arco_shell)

add_executable(ArcoFission apps/arcofission/main.cpp)
target_link_libraries(ArcoFission PRIVATE arco_compiler)

if(BUILD_TESTING)
    include(cmake/Testing.cmake)
endif()

include(cmake/Install.cmake)
