install(TARGETS
        arco_runtime
        arco_compiler
        arco_shell
        arco_c_api
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(TARGETS arcosh arco_cli ArcoFission
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

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

install(DIRECTORY tutorials/
    DESTINATION share/arcosh/tutorials
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas"
)

install(DIRECTORY scripts/arcosh/
    DESTINATION share/arcosh/scripts
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas" PATTERN "*.arcsh"
)

install(PROGRAMS scripts/install/install-deb-wizard.sh
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

install(DIRECTORY assets/ DESTINATION share/arcobasic/assets)

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
install(FILES README.md DESTINATION share/doc/arcobasic)

include(arcology-os/cmake/Install.cmake)
include(arcology-commons/cmake/Install.cmake)
