set(TARGET_NAME ipc_commands_api)

add_library(
    ${TARGET_NAME} SHARED
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
    server.h)

make_folder("base" ${TARGET_NAME})

target_include_directories(${TARGET_NAME} PRIVATE ${ZMQ_INCLUDE_DIRS} ${PROJECT_SOURCE_DIR}/src/lib/)

target_link_libraries(${TARGET_NAME} ${CMAKE_THREAD_LIBS_INIT} ${ZMQ_LIBRARIES} utils logging)

target_compile_definitions(${TARGET_NAME} PRIVATE -DOPENDCC_BASE_IPC_COMMANDS_EXPORT)
install(TARGETS ${TARGET_NAME})

if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${TARGET_NAME}>
        DESTINATION bin
        OPTIONAL)
endif()
