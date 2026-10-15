set(TARGET_NAME commands_api)

add_library(
    ${TARGET_NAME} SHARED
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
    command_interface.cpp)

make_folder("base/commands_api" ${TARGET_NAME})

target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/src/lib ${Boost_INCLUDE_DIR})
target_precompile_headers(${TARGET_NAME} PRIVATE ${PRECOMPILED_INCLUDES})
target_compile_definitions(${TARGET_NAME} PRIVATE -DOPENDCC_BASE_COMMANDS_EXPORT)
target_link_libraries(${TARGET_NAME} test_runner logging)
install(TARGETS ${TARGET_NAME})
if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${TARGET_NAME}>
        DESTINATION bin
        OPTIONAL)
endif()
