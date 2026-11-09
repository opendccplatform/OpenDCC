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

opendcc_make_library(
    app_version
    CPPFILES
    app_version.h
    app_version.cpp
    PRIVATE_DEFINITIONS
    OPENDCC_BASE_APP_VERSION_EXPORT
    GIT_COMMIT="${OPENDCC_GIT_COMMIT}")
