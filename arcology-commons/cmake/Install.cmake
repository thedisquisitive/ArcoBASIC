install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/arcology-commons/stdlib/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/arcobasic/stdlib
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/arcology-commons/examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/arcobasic/examples/arcology-commons
    FILES_MATCHING PATTERN "*.abas" PATTERN "*.arc" PATTERN "*.bas" PATTERN "*.arcsh"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/arcology-commons/docs/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/arcobasic/arcology-commons
    FILES_MATCHING PATTERN "*.md"
)

install(PROGRAMS arcology-commons/scripts/run/serve-arcology.sh
    DESTINATION ${CMAKE_INSTALL_DATADIR}/arcobasic/arcology-commons/scripts
)
