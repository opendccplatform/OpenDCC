set(TARGET_NAME commands_api)

opendcc_make_library(
    ${TARGET_NAME}
    CPPFILES
    api.h
    args.h
    args.cpp
    block.h
    block.cpp
    router.h
    router.cpp
    command.h
    command.cpp
    command_syntax.h
    command_syntax.cpp
    command_registry.h
    command_registry.cpp
    command_interface.h
    command_interface.cpp
    VS_FOLDER
    base/commands_api
    PRIVATE_DEFINITIONS
    OPENDCC_BASE_COMMANDS_EXPORT
    INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src/lib
    ${Boost_INCLUDE_DIR}
    LIBRARY_DEPENDENCIES
    test_runner
    logging)

target_precompile_headers(${TARGET_NAME} PRIVATE ${PRECOMPILED_INCLUDES})
