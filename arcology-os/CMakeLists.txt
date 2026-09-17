add_library(arcology_os_headers INTERFACE)
target_include_directories(arcology_os_headers
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
add_library(ArcologyOS::headers ALIAS arcology_os_headers)
