cmake_minimum_required(VERSION 3.20)

project(ArcologyLazarus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(GTKMM4 REQUIRED IMPORTED_TARGET gtkmm-4.0)
pkg_check_modules(HIVEX REQUIRED IMPORTED_TARGET hivex)

add_library(lazarus_core
    src/core/core.cpp
)

target_include_directories(lazarus_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(lazarus_core PUBLIC OpenSSL::Crypto PkgConfig::ZSTD)

add_executable(lazarus src/cli/main.cpp)
target_link_libraries(lazarus PRIVATE lazarus_core)

add_executable(lazarus-tui src/tui/main.cpp)
target_link_libraries(lazarus-tui PRIVATE lazarus_core)

add_executable(lazarus-gui src/gui/workflow_main.cpp src/gui/widgets.cpp)
target_link_libraries(lazarus-gui PRIVATE PkgConfig::GTK4 PkgConfig::GTKMM4)
target_compile_definitions(lazarus-gui PRIVATE LAZARUS_SOURCE_ICON_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/icons")
target_compile_definitions(lazarus-gui PRIVATE LAZARUS_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_executable(lazarus-service src/service/main.cpp)
target_link_libraries(lazarus-service PRIVATE lazarus_core PkgConfig::HIVEX)

add_executable(lazarus_smoke_tests tests/lazarus_smoke.cpp)
target_link_libraries(lazarus_smoke_tests PRIVATE lazarus_core)

enable_testing()
add_subdirectory(lazarum)

add_test(NAME lazarus_smoke_tests COMMAND lazarus_smoke_tests)
add_test(
    NAME lazarus_os_layout
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/lazarus-os/scripts/validate-layout.sh"
)
add_test(
    NAME lazarus_service_ping
    COMMAND sh -c "printf '%s\\n' '{\"command\":\"ping\"}' | '$<TARGET_FILE:lazarus-service>' --stdio --config '${CMAKE_CURRENT_SOURCE_DIR}/examples/bench-alpha.profile' | grep -q '\"ok\":true'"
)
add_test(
    NAME lazarus_service_devices
    COMMAND sh -c "printf '%s\\n' '{\"command\":\"devices\"}' | '$<TARGET_FILE:lazarus-service>' --stdio --config '${CMAKE_CURRENT_SOURCE_DIR}/examples/bench-alpha.profile' | grep -q '\"devices\"'"
)
add_test(
    NAME lazarus_service_profile
    COMMAND sh -c "printf '%s\\n' '{\"command\":\"profile\"}' | '$<TARGET_FILE:lazarus-service>' --stdio --config '${CMAKE_CURRENT_SOURCE_DIR}/examples/bench-alpha.profile' | grep -q '\"summary_text\"'"
)
add_test(
    NAME lazarus_service_rejects_volatile_storage
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/service_volatile_storage.sh" "$<TARGET_FILE:lazarus-service>"
)
add_test(
    NAME lazarus_service_backups
    COMMAND sh -c "printf '%s\\n' '{\"command\":\"backups\"}' | '$<TARGET_FILE:lazarus-service>' --stdio --config '${CMAKE_CURRENT_SOURCE_DIR}/examples/bench-alpha.profile' | grep -q '\"backups_rows\"'"
)
add_test(
    NAME lazarus_service_job_presets
    COMMAND sh -c "printf '%s\\n' '{\"command\":\"job_presets\"}' | '$<TARGET_FILE:lazarus-service>' --stdio --config '${CMAKE_CURRENT_SOURCE_DIR}/examples/bench-alpha.profile' | grep -q 'backup-before-repair'"
)
add_test(
    NAME lazarus_service_device_generation
    COMMAND sh -c "printf '%s\\n' '{\"command\":\"device_generation\"}' | '$<TARGET_FILE:lazarus-service>' --stdio --config '${CMAKE_CURRENT_SOURCE_DIR}/examples/bench-alpha.profile' | grep -q '\"device_generation\"'"
)
add_test(
    NAME lazarus_service_rejects_unconfined_report
    COMMAND sh -c "printf '%s\\n' '{\"command\":\"print_report\",\"report_path\":\"/etc/passwd\"}' | '$<TARGET_FILE:lazarus-service>' --stdio --config '${CMAKE_CURRENT_SOURCE_DIR}/examples/bench-alpha.profile' | grep -q 'outside configured Lazarus image storage'"
)
add_test(
    NAME lazarus_service_backup_labels
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/service_backup_labels.sh" "$<TARGET_FILE:lazarus-service>"
)
add_test(
    NAME lazarus_service_admin_auth
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/service_admin_auth.sh" "$<TARGET_FILE:lazarus-service>"
)
add_test(
    NAME lazarus_service_branding_save
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/service_branding_save.sh" "$<TARGET_FILE:lazarus-service>"
)
add_test(
    NAME lazarus_service_nas_profile
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/service_nas_profile.sh" "$<TARGET_FILE:lazarus-service>"
)
add_test(
    NAME lazarus_service_failure_detail
    COMMAND sh -c "printf '%s\\n' '{\"command\":\"verify_image\",\"image_directory\":\"/definitely/missing/lazarus-image\"}' | '$<TARGET_FILE:lazarus-service>' --stdio --config '${CMAKE_CURRENT_SOURCE_DIR}/examples/bench-alpha.profile' | grep -Eq '\"ok\":false.*\"error\":\"[^\"]+'"
)
add_test(
    NAME lazarus_service_rejects_unconfined_image_path
    COMMAND sh -c "printf '%s\\n' '{\"command\":\"image_source\",\"selector\":\"/dev/not-present\",\"output_directory\":\"/tmp/lazarus-outside-root\",\"ticket_number\":\"T-1\",\"customer_name\":\"Test\",\"technician\":\"Tech\",\"purpose\":\"Safety test\"}' | '$<TARGET_FILE:lazarus-service>' --stdio --config '${CMAKE_CURRENT_SOURCE_DIR}/examples/bench-alpha.profile' | grep -q 'outside every configured image-storage directory'"
)
add_test(
    NAME lazarus_service_printers
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/service_printers.sh" "$<TARGET_FILE:lazarus-service>"
)
add_test(
    NAME lazarus_service_network
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/service_network.sh" "$<TARGET_FILE:lazarus-service>"
)
add_test(
    NAME lazarus_service_install_os_rejects_invalid_selector
    COMMAND sh -c "printf '%s\\n' '{\"command\":\"install_os\",\"selector\":\"/dev/does-not-exist\",\"confirmation\":\"ERASE\"}' | '$<TARGET_FILE:lazarus-service>' --stdio --config '${CMAKE_CURRENT_SOURCE_DIR}/examples/bench-alpha.profile' | grep -q '\"ok\":false'"
)
add_test(
    NAME lazarus_service_power_rejects_invalid_confirmation
    COMMAND sh -c "printf '%s\\n' '{\"command\":\"system_power\",\"action\":\"shutdown\",\"confirmation\":\"NO\"}' | '$<TARGET_FILE:lazarus-service>' --stdio --config '${CMAKE_CURRENT_SOURCE_DIR}/examples/bench-alpha.profile' | grep -q '\"ok\":false'"
)
add_test(
    NAME lazarus_service_driver_plan
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/service_driver_plan.sh" "$<TARGET_FILE:lazarus-service>"
)
add_test(
    NAME lazarus_service_boot_repair
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/service_boot_repair.sh" "$<TARGET_FILE:lazarus-service>"
)

install(TARGETS lazarus lazarus-tui lazarus-gui RUNTIME DESTINATION bin)
install(TARGETS lazarus-service RUNTIME DESTINATION sbin)
install(DIRECTORY examples/ DESTINATION share/arcology-lazarus/examples)
install(DIRECTORY docs/ DESTINATION share/arcology-lazarus/docs)
install(DIRECTORY assets/ DESTINATION share/arcology-lazarus/assets PATTERN cursors EXCLUDE)
install(FILES assets/cursors/index.theme DESTINATION share/icons/Arcology-Lazarus)
install(DIRECTORY assets/cursors/cursors/ DESTINATION share/icons/Arcology-Lazarus/cursors)
install(
    DIRECTORY lazarus-os/
    DESTINATION share/arcology-lazarus/lazarus-os
    PATTERN build EXCLUDE
)
