qt_add_resources(resources ${CMAKE_SOURCE_DIR}/src/lib/opendcc/app/ui/resources/stylesheets.qrc)

opendcc_make_executable(
    crash_reporter
    CPPFILES
    ${resources}
    main.cpp
    crash_reporter_window.h
    crash_reporter_window.cpp
    crash_report_handler.h
    crash_report_handler.cpp
    LIBRARY_DEPENDENCIES
    utils
    app_config
    sentry::sentry
    sentry_crashpad::client
    sentry_crashpad::handler
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui)
