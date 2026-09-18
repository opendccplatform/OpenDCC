qt_add_resources(resources ${PROJECT_SOURCE_DIR}/icons/icons.qrc)


set(BIN_NAME dcc_base)


if(UNIX AND NOT APPLE)
    set(BIN_STARTUP_NAME "${BIN_NAME}")
    set(BIN_NAME "${BIN_NAME}.bin")
endif()

add_executable(${BIN_NAME} ${PROJECT_SOURCE_DIR}/icons/opendcc_resources.rc ${resources})

if(WIN32)
    target_sources(${BIN_NAME} PRIVATE ${WIN32_MANIFEST})
endif()


target_sources(${BIN_NAME} PUBLIC main.cpp)
target_link_libraries(${BIN_NAME} opendcc_lib common_widgets ${CMAKE_THREAD_LIBS_INIT})


target_include_directories(${BIN_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src/lib ${TBB_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS}
                                               ${PYTHON_INCLUDE_DIRS} ${PXR_INCLUDE_DIRS} ${ZMQ_INCLUDE_DIRS})

set_target_properties(${BIN_NAME} PROPERTIES AUTOMOC ON)

if(MSVC)
    set_target_properties(${BIN_NAME} PROPERTIES VS_DEBUGGER_COMMAND "$<TARGET_FILE:${BIN_NAME}>"
                                                 VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${BIN_NAME}>")
    if(DCC_MAKE_VSDEBUG_LAUNCHER)
        set_target_properties(${BIN_NAME} PROPERTIES VS_DEBUGGER_ENVIRONMENT "${VS_ENVIRONMENT}")
    endif()
endif()

install(TARGETS ${BIN_NAME} RUNTIME DESTINATION bin)

if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${BIN_NAME}>
        DESTINATION bin
        OPTIONAL)
elseif(UNIX AND NOT APPLE)
    install(
        PROGRAMS ${PROJECT_SOURCE_DIR}/cmake/macros/launch_script.sh
        DESTINATION bin
        RENAME ${BIN_STARTUP_NAME})
endif()

if(DCC_BUILD_TESTS)
    add_test(
        NAME IntervalVectorTests
        COMMAND $<TARGET_FILE:dcc_base> --with-tests -e
        WORKING_DIRECTORY $<TARGET_FILE_DIR:dcc_base>)
endif()
