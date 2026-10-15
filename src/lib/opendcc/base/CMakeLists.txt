add_subdirectory(app_config)
add_subdirectory(commands_api)
add_subdirectory(ipc_commands_api)
add_subdirectory(test_runner)
add_subdirectory(utils)
add_subdirectory(py_utils)
add_subdirectory(vendor)
add_subdirectory(logging)
add_subdirectory(packaging)
add_subdirectory(crash_reporting)

install(FILES export.h defines.h DESTINATION "include/opendcc/base")

add_library(app_version SHARED app_version.h app_version.cpp)
target_compile_definitions(app_version PRIVATE -DOPENDCC_BASE_APP_VERSION_EXPORT)
target_compile_definitions(app_version PRIVATE -DGIT_COMMIT="${OPENDCC_GIT_COMMIT}")

install(
    TARGETS app_version
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib)

if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:app_version>
        DESTINATION bin
        OPTIONAL)
endif()
