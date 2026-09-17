cmake_minimum_required(VERSION 3.16)

project(ArcoBASIC VERSION 0.1.0 LANGUAGES CXX C)

# NOTE: a RelWithDebInfo-by-default block was considered here (ported from
# origin/agent/arcfs-admin-suite, motivated by a real "unoptimized dev build -> slower bytecode VM
# inside every capsule" finding) but reverted: building at that optimization level exposes a
# pre-existing, real undefined-behavior bug in the retired src/shell/arcosh.cpp (confirmed by
# building unpatched master at the same optimization level in an isolated worktree -- same
# hang/segfault in tests/unit/runtime_tests.cpp's shell-builtin block, so this is not a regression
# from anything in this tree, just newly exposed). Since that file is being kept alive only long
# enough for its existing test coverage to keep passing (see arcosh/README.md), not invested in
# further, revisit enabling this once arco_shell is fully removed rather than debugging legacy code
# on its way out.

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
    src/gui/graphics.cpp
    ${ARCO_GUI_BACKEND_SOURCE}
)
arco_configure_library(arco_runtime)
# arcology_os_headers, not the (removed) arcology_os static lib: parser.cpp (one of this library's
# own sources) still uses uefi_bindings.hpp, the one genuinely OS-specific header that stayed under
# arcology-os/ -- see docs/project-layout.md's shared-infrastructure note.
target_link_libraries(arco_runtime PUBLIC arcology_os_headers)
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
    src/compiler/pe_image.cpp
)
arco_configure_library(arco_compiler)
target_link_libraries(arco_compiler PUBLIC arco_runtime arcology_os_headers)
target_compile_definitions(arco_compiler PRIVATE ARCO_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_library(ArcoBASIC::compiler ALIAS arco_compiler)

add_library(arco_arcfs_host STATIC
    src/arcfs/host.cpp
)
arco_configure_library(arco_arcfs_host)
add_library(ArcoBASIC::arcfs_host ALIAS arco_arcfs_host)

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
        src/gui/graphics.cpp
        src/gui/stub_backend.cpp
    )
    arco_configure_library(arco_runtime_core)
    target_link_libraries(arco_runtime_core PUBLIC arcology_os_headers)

    add_library(arco_compiler_core STATIC EXCLUDE_FROM_ALL src/compiler/fission.cpp src/compiler/pe_image.cpp)
    arco_configure_library(arco_compiler_core)
    target_link_libraries(arco_compiler_core PUBLIC arco_runtime_core arcology_os_headers)
    target_compile_definitions(arco_compiler_core PRIVATE ARCO_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(ArcoFissionCapsuleCoreProbe EXCLUDE_FROM_ALL apps/arcofission/capsule_core_probe.cpp)
    target_link_libraries(ArcoFissionCapsuleCoreProbe PRIVATE arco_compiler_core)

    # Same probe trick, but for arco_runtime_core ALONE (no arco_compiler_core/fission.cpp) -- the
    # native (no-bytecode-VM) Linux backend's own generic host-function bridge (arco_call_host,
    # src/native/host_bridge.cpp) needs a real arco::Runtime but must NOT drag ArcoFission's own
    # compiler implementation into every generated native binary the way
    # ArcoFissionCapsuleCoreProbe's own link.txt would. Opt in with
    # `cmake --build . --target ArcoNativeRuntimeCoreProbe`.
    add_executable(ArcoNativeRuntimeCoreProbe EXCLUDE_FROM_ALL apps/arcofission/runtime_core_probe.cpp)
    target_link_libraries(ArcoNativeRuntimeCoreProbe PRIVATE arco_runtime_core)
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

# The `arcosh` executable (apps/arcosh/main.cpp) is retired: ArcoSH development restarted as the
# standalone arcosh/ project. arco_shell itself keeps building because tests/unit/runtime_tests.cpp
# still exercises arco::shell::* directly; see docs/arcosh.md and arcosh/README.md.

add_executable(ArcoFission apps/arcofission/main.cpp)
target_link_libraries(ArcoFission PRIVATE arco_compiler)

set(ARCFSCTL_CAPSULE ${CMAKE_CURRENT_BINARY_DIR}/generated/arcfsctl)
add_custom_command(
    OUTPUT ${ARCFSCTL_CAPSULE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND $<TARGET_FILE:ArcoFission> build
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/arcfsctl.abas
            -o ${ARCFSCTL_CAPSULE}
    DEPENDS ArcoFission ${CMAKE_CURRENT_SOURCE_DIR}/tools/arcfsctl.abas
    COMMENT "Building arcfsctl ArcoBASIC capsule"
    VERBATIM
)
add_custom_target(arcfsctl ALL DEPENDS ${ARCFSCTL_CAPSULE})

set(ARCONAUT_CAPSULE ${CMAKE_CURRENT_BINARY_DIR}/generated/arconaut)
add_custom_command(
    OUTPUT ${ARCONAUT_CAPSULE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND $<TARGET_FILE:ArcoFission> build
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/arconaut.abas
            -o ${ARCONAUT_CAPSULE}
    DEPENDS ArcoFission ${CMAKE_CURRENT_SOURCE_DIR}/examples/arconaut.abas
    COMMENT "Building Arconaut ArcoBASIC capsule"
    VERBATIM
)
add_custom_target(arconaut ALL DEPENDS ${ARCONAUT_CAPSULE})

if(UNIX AND NOT APPLE)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/mount.arcfs.in
        ${CMAKE_CURRENT_BINARY_DIR}/packaging/linux/mount.arcfs
        @ONLY
    )
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/mkfs.arcfs.in
        ${CMAKE_CURRENT_BINARY_DIR}/packaging/linux/mkfs.arcfs
        @ONLY
    )
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/61-arcfs.rules.in
        ${CMAKE_CURRENT_BINARY_DIR}/packaging/linux/61-arcfs.rules
        @ONLY
    )

    add_executable(arcfs-linux apps/arcfs-linux/main.cpp)
    target_link_libraries(arcfs-linux PRIVATE arco_arcfs_host)
    if(ARCO_FUSE3_FOUND)
        target_link_libraries(arcfs-linux PRIVATE PkgConfig::FUSE3)
        target_compile_definitions(arcfs-linux PRIVATE ARCO_HAVE_FUSE3=1)
    endif()
endif()

if(BUILD_TESTING)
    include(cmake/Testing.cmake)
endif()

include(cmake/Install.cmake)
