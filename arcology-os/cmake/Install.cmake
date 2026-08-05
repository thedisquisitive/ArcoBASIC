install(TARGETS arcology_os
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/arcology-os/include/arco/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/arco
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/arcology-os/stdlib/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/arcobasic/stdlib
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/arcology-os/examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/arcobasic/examples/arcology-os
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas" PATTERN "*.arcsh"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/arcology-os/docs/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/arcobasic/arcology-os
    FILES_MATCHING PATTERN "*.md"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/arcology-os/rfcs/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/arcobasic/arcology-os/rfcs
    FILES_MATCHING PATTERN "*.md"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/arcology-os/agent-packets/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/arcobasic/arcology-os/agent-packets
    FILES_MATCHING PATTERN "*.md"
)
