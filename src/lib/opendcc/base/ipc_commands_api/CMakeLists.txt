set(TARGET_NAME ipc_commands_api)

opendcc_make_library(
    ${TARGET_NAME}
    CPPFILES
    api.h
    command_registry.h
    command_registry.cpp
    command.cpp
    command.h
    server_info.cpp
    server_info.h
    server_registry.cpp
    server_registry.h
    server.cpp
    server.h
    VS_FOLDER
    base
    PRIVATE_DEFINITIONS
    OPENDCC_BASE_IPC_COMMANDS_EXPORT
    PRIVATE_INCLUDE_DIRS
    ${ZMQ_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/src/lib/
    LIBRARY_DEPENDENCIES
    ${CMAKE_THREAD_LIBS_INIT}
    ${ZMQ_LIBRARIES}
    utils
    logging)
