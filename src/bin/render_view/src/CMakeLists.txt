qt_add_resources(resources ${PROJECT_SOURCE_DIR}/icons/icons.qrc)

set(_executable_options)
set(_entry_point_libraries)
if(WIN32 AND DCC_RENDER_VIEW_WIN_GUI_EXECUTABLE)
    list(APPEND _executable_options WIN32)
    list(APPEND resources ${PROJECT_SOURCE_DIR}/icons/render_view/opendcc_app.rc)
    # Qt6 supplies its entry point through Qt6::Core.
    if(QT_VERSION_MAJOR LESS 6)
        list(APPEND _entry_point_libraries Qt5::WinMain)
    endif()
endif()

opendcc_make_executable(
    render_view
    LINUX_LAUNCHER
    QT_AUTOMOC
    ${_executable_options}
    CPPFILES
    main.cpp
    ${resources}
    PRIVATE_DEFINITIONS
    GIT_COMMIT="${OPENDCC_GIT_COMMIT}"
    INCLUDE_DIRS
    ${OIIO_INCLUDE_DIR}
    ${ILMBASE_INCLUDE_DIR}
    ${OCIO_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${RENDER_VIEW_SOURCE_DIR}
    ${RENDER_VIEW_SOURCE_DIR}/thirdparty/qtsingleapplication/src
    ${PROJECT_SOURCE_DIR}/lib/src
    LIBRARY_DEPENDENCIES
    qtsingleapplication
    image_view
    color_theme
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    ${DCC_QT_OPENGL_TARGETS}
    Qt${QT_VERSION_MAJOR}::Network
    ${CMAKE_THREAD_LIBS_INIT}
    crash_reporting
    ${_entry_point_libraries})
