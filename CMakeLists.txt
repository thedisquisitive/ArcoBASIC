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
    ${ARCO_GUI_BACKEND_SOURCE}
)
arco_configure_library(arco_runtime)
target_link_libraries(arco_runtime PRIVATE arcology_os_headers)
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
