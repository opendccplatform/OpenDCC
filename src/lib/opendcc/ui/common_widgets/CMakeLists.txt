set(headers
    api.h
    canvas_widget.h
    color_widget.h
    gradient_widget.h
    group_widget.h
    popup_window.h
    ramp.h
    ramp_widget.h
    tiny_slider.h
    vec_widget.h
    marking_menu.h
    round_marking_menu.h
    float_slider.h
    number_value_widget.h
    ladder_widget.h
    rollout_widget.h
    double_slider.h
    double_widget.h
    help_button_widget.h
    search_widget.h
    precision_slider.h
    qt_compat.h)

set(src
    canvas_widget.cpp
    color_widget.cpp
    gradient_widget.cpp
    group_widget.cpp
    popup_window.cpp
    ramp.inl
    ramp_widget.cpp
    tiny_slider.cpp
    vec_widget.cpp
    marking_menu.cpp
    round_marking_menu.cpp
    float_slider.cpp
    number_value_widget.cpp
    ladder_widget.cpp
    rollout_widget.cpp
    double_slider.cpp
    double_widget.cpp
    help_button_widget.cpp
    search_widget.cpp
    precision_slider.cpp)

opendcc_make_library(
    common_widgets
    QT_AUTOMOC
    CPPFILES
    ${src}
    ${headers}
    PRIVATE_INCLUDE_DIRS
    ${QT_INCLUDE_DIR}
    ${PXR_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/src/lib
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${Boost_INCLUDE_DIRS}
    PRIVATE_DEFINITIONS
    OPENDCC_UI_COMMON_WIDGETS_EXPORT
    VS_FOLDER
    widgets
    LIBRARY_DEPENDENCIES
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    ${PYTHON_LIBRARY}
    ${Boost_PYTHON_LIBRARY}
    vt)

install(FILES ${headers} DESTINATION "include/opendcc/common_widgets/")

install(
    EXPORT common_widgets-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/common_widgets
    NAMESPACE opendcc::)

add_subdirectory(bind)
