set(headers
    api.h
    audio_decoder.h
    frames_per_second_widget.h
    time_display.h
    time_widget.h
    timebar_widget.h
    timeline_widget.h
    timeline_slider.h
    range_slider.h)

set(src
    audio_decoder.cpp
    frames_per_second_widget.cpp
    time_display.cpp
    time_widget.cpp
    timebar_widget.cpp
    timeline_widget.cpp
    timeline_slider.cpp
    range_slider.cpp)

opendcc_make_library(
    timeline_widget
    QT_AUTOMOC
    CPPFILES
    ${src}
    ${moc}
    ${headers}
    PRIVATE_INCLUDE_DIRS
    ${QT_INCLUDE_DIR}
    ${PXR_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/lib/thirdparty
    PRIVATE_DEFINITIONS
    OPENDCC_UI_TIMELINE_EXPORT
    VS_FOLDER
    widgets
    LIBRARY_DEPENDENCIES
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Multimedia
    vt
    color_theme)

install(
    EXPORT timeline_widget-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/timeline_widget
    NAMESPACE opendcc::)

add_subdirectory(python)
