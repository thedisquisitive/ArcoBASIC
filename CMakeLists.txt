cmake_minimum_required(VERSION 3.16)

project(ArcoBASIC VERSION 0.1.0 LANGUAGES CXX C)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(arco
    core/lexer.cpp
    core/parser.cpp
    runtime/runtime.cpp
    shell/arcosh.cpp
    compiler/fission.cpp
    bindings/c/arco_c_api.cpp
)

option(ARCO_ENABLE_GUI "Build the desktop GUI backend when dependencies are available" ON)
option(ARCO_ENABLE_NETWORK "Build HTTP networking helpers when libcurl is available" ON)
if(ARCO_ENABLE_GUI AND UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GLFW3 QUIET IMPORTED_TARGET glfw3)
        pkg_check_modules(PANGOCAIRO QUIET IMPORTED_TARGET pangocairo)
        pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
    endif()
    find_package(OpenGL QUIET)
endif()

if(ARCO_ENABLE_NETWORK)
    find_package(CURL QUIET)
endif()

if(GLFW3_FOUND AND PANGOCAIRO_FOUND AND GTK3_FOUND AND OpenGL_FOUND)
    target_sources(arco PRIVATE gui/gui_glfw.cpp)
    target_link_libraries(arco PRIVATE PkgConfig::GLFW3 PkgConfig::PANGOCAIRO PkgConfig::GTK3 OpenGL::GL)
    target_compile_definitions(arco PRIVATE ARCO_GUI_GLFW=1)
else()
    target_sources(arco PRIVATE gui/gui_stub.cpp)
endif()

if(CURL_FOUND)
    target_link_libraries(arco PRIVATE CURL::libcurl)
    target_compile_definitions(arco PRIVATE ARCO_NETWORK_CURL=1)
endif()

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

add_executable(ArcoFission tools/arcofission_main.cpp)
target_link_libraries(ArcoFission PRIVATE arco)

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
add_test(
    NAME arcofission_alpha_smoke
    COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/arcofission_alpha_smoke.sh
        $<TARGET_FILE:ArcoFission>
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(
    NAME systems_fixed_width_types_smoke
    COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/systems_fixed_width_types_smoke.sh
        $<TARGET_FILE:ArcoFission>
)
add_test(
    NAME systems_freestanding_profile_smoke
    COMMAND bash
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/systems_freestanding_profile_smoke.sh
        $<TARGET_FILE:ArcoFission>
)

install(TARGETS arcosh arco_cli ArcoFission
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

install(DIRECTORY scripts/arcosh/
    DESTINATION share/arcosh/scripts
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas" PATTERN "*.arcsh"
)

install(PROGRAMS scripts/install-deb-wizard.sh
    DESTINATION share/arcobasic/scripts
)

install(DIRECTORY examples/
    DESTINATION share/arcobasic/examples
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas" PATTERN "*.arcsh"
)

install(DIRECTORY mods/
    DESTINATION share/arcobasic/mods
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas" PATTERN "*.arcsh"
)

install(DIRECTORY assets/
    DESTINATION share/arcobasic/assets
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/arconaut.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/packaging/linux/arconaut.desktop
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/packaging/linux/arconaut.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES packaging/linux/application-x-arcobasic.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
)

install(FILES assets/arconaut/title-icon.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
    RENAME arconaut.png
)

install(FILES assets/arconaut/arcobasic-file.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/mimetypes
    RENAME application-x-arcobasic.png
)

install(FILES assets/arconaut/title-icon.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
    RENAME arconaut.png
)

install(DIRECTORY docs/
    DESTINATION share/doc/arcobasic
    FILES_MATCHING PATTERN "*.md"
)

install(FILES readme.md arcosh.md
    DESTINATION share/doc/arcobasic
)
