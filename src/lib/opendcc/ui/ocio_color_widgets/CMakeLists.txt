set(headers api.h OCIO_color_widget.h OCIO_gradient_widget.h)

set(src OCIO_color_widget.cpp OCIO_gradient_widget.cpp)

opendcc_make_library(
    ocio_color_widgets
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
    ${PROJECT_SOURCE_DIR}/src/lib/thirdparty
    ${OCIO_INCLUDE_DIRS}
    PRIVATE_DEFINITIONS
    OPENDCC_UI_OCIO_COLOR_WIDGETS_EXPORT
    VS_FOLDER
    widgets
    LIBRARY_DEPENDENCIES
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    common_widgets
    ${OCIO_LIBRARIES})

install(FILES ${headers} DESTINATION "include/opendcc/ocio_color_widgets/")

install(
    EXPORT ocio_color_widgets-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/ocio_color_widgets
    NAMESPACE opendcc::)
