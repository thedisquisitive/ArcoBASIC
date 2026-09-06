install(TARGETS
        arco_runtime
        arco_compiler
        arco_c_api
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(TARGETS arco_cli ArcoFission
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# arcfsctl/arconaut/arcfs-linux and all of their packaging (desktop entry, icons, udev rules,
# mount/mkfs wrappers) install themselves from arcfs-utils/CMakeLists.txt -- see that file.

install(DIRECTORY include/arco/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/arco
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
install(FILES include/arco.hpp include/arco_c_api.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY stdlib/
    DESTINATION share/arcobasic/stdlib
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas"
)

install(DIRECTORY examples/
    DESTINATION share/arcobasic/examples
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas" PATTERN "*.arcsh"
)

install(DIRECTORY assets/ DESTINATION share/arcobasic/assets)

# application-x-arcobasic.xml is the general ArcoBASIC MIME type (any .abas file, not ArcFS-
# specific), so it stays installed from here rather than arcfs-utils/ -- Arconaut's own .desktop
# entry, icons, and everything else ArcFS-specific install from arcfs-utils/CMakeLists.txt.
install(FILES packaging/linux/application-x-arcobasic.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
)

install(DIRECTORY docs/
    DESTINATION share/doc/arcobasic
    FILES_MATCHING PATTERN "*.md"
)
install(FILES README.md DESTINATION share/doc/arcobasic)

include(arcology-os/cmake/Install.cmake)
include(arcology-commons/cmake/Install.cmake)
