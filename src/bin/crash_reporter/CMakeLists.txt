set(TARGET_NAME crash_reporter)

set(BIN_NAME ${TARGET_NAME})

qt_add_resources(resources ${CMAKE_SOURCE_DIR}/src/lib/opendcc/app/ui/resources/stylesheets.qrc)

add_executable(${BIN_NAME} ${resources} main.cpp crash_reporter_window.h crash_reporter_window.cpp
                           crash_report_handler.h crash_report_handler.cpp)
if(WIN32)
    target_sources(${BIN_NAME} PRIVATE ${WIN32_MANIFEST})
endif()
target_link_libraries(
    ${BIN_NAME}
    utils
    app_config
    sentry::sentry
    sentry_crashpad::client
    sentry_crashpad::client
    sentry_crashpad::handler
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui)

install(TARGETS ${BIN_NAME} RUNTIME DESTINATION bin)
if(MSVC)
    set_target_properties(
        ${BIN_NAME} PROPERTIES VS_DEBUGGER_COMMAND "${CMAKE_INSTALL_PREFIX}/bin/$<TARGET_FILE_NAME:${BIN_NAME}>"
                               VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_INSTALL_PREFIX}/bin")
    if(DCC_MAKE_VSDEBUG_LAUNCHER)
        set_target_properties(${BIN_NAME} PROPERTIES VS_DEBUGGER_ENVIRONMENT "${VS_ENVIRONMENT}")
    endif()
    install(
        FILES $<TARGET_PDB_FILE:${BIN_NAME}>
        DESTINATION bin
        OPTIONAL)
endif()
