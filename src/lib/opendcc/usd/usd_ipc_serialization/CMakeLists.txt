set(TARGET_NAME usd_ipc_serialization)

opendcc_make_library(
    ${TARGET_NAME}
    CPPFILES
    api.h
    usd_edits.h
    usd_edits.cpp
    usd_data_types.h
    serialization.h
    serialization.cpp
    usd_ipc_utils.h
    usd_ipc_utils.cpp
    PUBLIC_DEFINITIONS
    USD_IPC_SERIALIZATION_EXPORT
    INCLUDE_DIRS
    "$<BUILD_INTERFACE:${ZMQ_INCLUDE_DIRS}>"
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/lib/>"
    "$<INSTALL_INTERFACE:include>"
    ${Boost_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
    LIBRARY_DEPENDENCIES
    ${ZMQ_LIBRARIES}
    ${PYTHON_LIBRARY}
    ${Boost_PYTHON_LIBRARY}
    usd
    sdf
    test_runner
    logging)

if(WIN32)
    target_link_libraries(${TARGET_NAME} PUBLIC bcrypt)
endif()

install(
    EXPORT usd_ipc_serialization-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/${TARGET_NAME}
    NAMESPACE opendcc::)
