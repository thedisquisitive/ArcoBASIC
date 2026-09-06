# ArcFS host-utility suite: the Linux FUSE mount daemon, the arcfsctl/Arconaut ArcoBASIC capsules,
# and their packaging (udev rules, mount/mkfs wrappers, desktop entry, icons). Everything ArcFS-
# specific that isn't part of the ArcoBASIC language/stdlib itself lives under this one directory --
# see the root CMakeLists.txt's single `add_subdirectory(arcfs-utils)` call for how this plugs in.
#
# Build outputs land under arcfs-utils/build/ -- inside this source directory, not the shared top-
# level build/ tree everything else builds into -- by user request. ARCFS_UTILS_BUILD_DIR is the
# one place that path is defined; it's propagated to PARENT_SCOPE so cmake/Testing.cmake (included
# from the root CMakeLists.txt, after add_subdirectory(arcfs-utils) runs) can find the same
# generated/packaging paths without duplicating this path computation. arcfs-utils/build/ is
# covered by the repo's existing bare `build/` .gitignore pattern (it matches any directory named
# `build` anywhere in the tree), so it's ignored the same way the top-level build/ already is.
#
# Depends on `ArcoFission` (to build the arcfsctl/arconaut capsules) and the `arco_configure_library`
# function (for arco_arcfs_host's include paths), both defined earlier in the root CMakeLists.txt --
# add_subdirectory(arcfs-utils) there must stay after both.

set(ARCFS_UTILS_BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build)
set(ARCFS_UTILS_BUILD_DIR ${ARCFS_UTILS_BUILD_DIR} PARENT_SCOPE)

add_library(arco_arcfs_host STATIC
    src/host.cpp
)
arco_configure_library(arco_arcfs_host)
add_library(ArcoBASIC::arcfs_host ALIAS arco_arcfs_host)
set_target_properties(arco_arcfs_host PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${ARCFS_UTILS_BUILD_DIR}
)

set(ARCFSCTL_CAPSULE ${ARCFS_UTILS_BUILD_DIR}/generated/arcfsctl)
add_custom_command(
    OUTPUT ${ARCFSCTL_CAPSULE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ARCFS_UTILS_BUILD_DIR}/generated
    COMMAND $<TARGET_FILE:ArcoFission> build
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/arcfsctl.abas
            -o ${ARCFSCTL_CAPSULE}
    DEPENDS ArcoFission ${CMAKE_CURRENT_SOURCE_DIR}/tools/arcfsctl.abas
    COMMENT "Building arcfsctl ArcoBASIC capsule"
    VERBATIM
)
add_custom_target(arcfsctl ALL DEPENDS ${ARCFSCTL_CAPSULE})

set(ARCONAUT_CAPSULE ${ARCFS_UTILS_BUILD_DIR}/generated/arconaut)
add_custom_command(
    OUTPUT ${ARCONAUT_CAPSULE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ARCFS_UTILS_BUILD_DIR}/generated
    COMMAND $<TARGET_FILE:ArcoFission> build
            ${CMAKE_CURRENT_SOURCE_DIR}/apps/arconaut/arconaut.abas
            -o ${ARCONAUT_CAPSULE}
    # Stages assets/arconaut/ next to the built capsule so ArcoSH.AssetsDir()'s own
    # `<exe_dir>/assets` search candidate (runtime.cpp) finds arconaut's icon (Path.Join(
    # ArcoSH.AssetsDir(), "arconaut", "title-icon.png") in arconaut.abas) when the capsule is run
    # directly out of the build tree, not just after `cmake --install` (which separately installs
    # the same directory to share/arcobasic/assets/arconaut -- see this file's own install() rules
    # below; that one covers AssetsDir()'s installed-layout search candidate, not this one).
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/arconaut
            ${ARCFS_UTILS_BUILD_DIR}/generated/assets/arconaut
    DEPENDS ArcoFission ${CMAKE_CURRENT_SOURCE_DIR}/apps/arconaut/arconaut.abas
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/arconaut/title-icon.png
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/arconaut/arcobasic-file.png
    COMMENT "Building Arconaut ArcoBASIC capsule"
    VERBATIM
)
add_custom_target(arconaut ALL DEPENDS ${ARCONAUT_CAPSULE})

if(UNIX AND NOT APPLE)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/mount.arcfs.in
        ${ARCFS_UTILS_BUILD_DIR}/packaging/linux/mount.arcfs
        @ONLY
    )
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/mkfs.arcfs.in
        ${ARCFS_UTILS_BUILD_DIR}/packaging/linux/mkfs.arcfs
        @ONLY
    )
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/61-arcfs.rules.in
        ${ARCFS_UTILS_BUILD_DIR}/packaging/linux/61-arcfs.rules
        @ONLY
    )

    add_executable(arcfs-linux apps/arcfs-linux/main.cpp)
    set_target_properties(arcfs-linux PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${ARCFS_UTILS_BUILD_DIR}
    )
    target_link_libraries(arcfs-linux PRIVATE arco_arcfs_host)
    if(ARCO_FUSE3_FOUND)
        target_link_libraries(arcfs-linux PRIVATE PkgConfig::FUSE3)
        target_compile_definitions(arcfs-linux PRIVATE ARCO_HAVE_FUSE3=1)
    endif()
endif()

# --- Install rules (RFC-ArcoUI-adjacent packaging note: kept here, not in the root
# cmake/Install.cmake, so this directory stays the one place that knows about ArcFS's own
# packaging layout) ----------------------------------------------------------------------

if(TARGET arcfsctl)
    install(PROGRAMS ${ARCFSCTL_CAPSULE}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/tools/arcfsctl.abas
        DESTINATION share/arcobasic/tools
    )
endif()

if(TARGET arconaut)
    install(PROGRAMS ${ARCONAUT_CAPSULE}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/apps/arconaut/arconaut.abas
        DESTINATION share/arcobasic/examples
    )
endif()

if(TARGET arcfs-linux)
    install(TARGETS arcfs-linux
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(PROGRAMS ${ARCFS_UTILS_BUILD_DIR}/packaging/linux/mount.arcfs
        DESTINATION ${CMAKE_INSTALL_SBINDIR}
    )
    install(PROGRAMS ${ARCFS_UTILS_BUILD_DIR}/packaging/linux/mkfs.arcfs
        DESTINATION ${CMAKE_INSTALL_SBINDIR}
    )
    install(FILES ${ARCFS_UTILS_BUILD_DIR}/packaging/linux/61-arcfs.rules
        DESTINATION lib/udev/rules.d
    )
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/arcfs-tmpfiles.conf
        DESTINATION lib/tmpfiles.d
    )
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/arconaut.desktop.in
    ${ARCFS_UTILS_BUILD_DIR}/packaging/linux/arconaut.desktop
    @ONLY
)
install(FILES ${ARCFS_UTILS_BUILD_DIR}/packaging/linux/arconaut.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/arconaut/title-icon.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
    RENAME arconaut.png
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/arconaut/arcobasic-file.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/mimetypes
    RENAME application-x-arcobasic.png
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/arconaut/title-icon.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
    RENAME arconaut.png
)
# Also installed at runtime-lookup path ArcoSH.AssetsDir()/arconaut/... (see arconaut.abas's own
# icon-loading code) -- share/arcobasic/assets is the generic assets/ install's own destination
# (cmake/Install.cmake), so this mirrors an "arconaut" subdirectory into it from here instead.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/arconaut/
    DESTINATION share/arcobasic/assets/arconaut
)
