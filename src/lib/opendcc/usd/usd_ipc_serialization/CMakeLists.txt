set(TARGET_NAME usd_ipc_serialization)

add_library(
    ${TARGET_NAME} SHARED
    api.h
    usd_edits.h
    usd_edits.cpp
    usd_data_types.h
    serialization.h
    serialization.cpp
    usd_ipc_utils.h
    usd_ipc_utils.cpp)

target_compile_definitions(${TARGET_NAME} PUBLIC -DUSD_IPC_SERIALIZATION_EXPORT)
target_link_libraries(
    ${TARGET_NAME}
    ${ZMQ_LIBRARIES}
    ${PYTHON_LIBRARY}
    ${Boost_PYTHON_LIBRARY}
    usd
    sdf
    test_runner
    logging)

if(WIN32)
    target_link_libraries(${TARGET_NAME} bcrypt)
endif()

target_include_directories(
    ${TARGET_NAME} PUBLIC "$<BUILD_INTERFACE:${ZMQ_INCLUDE_DIRS}>" "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/lib/>"
                          "$<INSTALL_INTERFACE:include>" ${Boost_INCLUDE_DIRS} ${PYTHON_INCLUDE_DIRS})

install(
    TARGETS ${TARGET_NAME}
    EXPORT usd_ipc_serialization-targets
    ARCHIVE DESTINATION "lib"
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION "lib")

if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${TARGET_NAME}>
        DESTINATION bin
        OPTIONAL)
endif()

install(
    EXPORT usd_ipc_serialization-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/${TARGET_NAME}
    NAMESPACE opendcc::)
