include_directories(${QT_INCLUDE_DIR} ${PXR_INCLUDE_DIRS} ${PYTHON_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}
                    ${PROJECT_SOURCE_DIR}/src/lib/thirdparty)

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

add_library(timeline_widget SHARED ${src} ${moc} ${headers})
target_link_libraries(
    timeline_widget
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Multimedia
    vt
    color_theme)

set_property(TARGET timeline_widget PROPERTY AUTOMOC ON)
target_compile_definitions(timeline_widget PRIVATE -DOPENDCC_UI_TIMELINE_EXPORT)

install(
    TARGETS timeline_widget
    EXPORT timeline_widget-targets
    ARCHIVE DESTINATION "lib"
    LIBRARY DESTINATION "lib"
    RUNTIME DESTINATION "bin")

install(
    EXPORT timeline_widget-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/timeline_widget
    NAMESPACE opendcc::)

add_subdirectory(python)
make_folder("widgets" timeline_widget)
