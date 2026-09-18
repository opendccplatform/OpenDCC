set(CMAKE_AUTOMOC ON)
include_directories(
    ${QT_INCLUDE_DIR}
    ${PXR_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/src/lib
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/lib/thirdparty
    ${OCIO_INCLUDE_DIRS})

set(headers api.h OCIO_color_widget.h OCIO_gradient_widget.h)

set(src OCIO_color_widget.cpp OCIO_gradient_widget.cpp)

add_library(ocio_color_widgets SHARED ${src} ${headers})
target_compile_definitions(ocio_color_widgets PRIVATE -DOPENDCC_UI_OCIO_COLOR_WIDGETS_EXPORT)

target_link_libraries(
    ocio_color_widgets
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    common_widgets
    ${OCIO_LIBRARIES})

install(FILES ${headers} DESTINATION "include/opendcc/ocio_color_widgets/")

install(
    TARGETS ocio_color_widgets
    EXPORT ocio_color_widgets-targets
    ARCHIVE DESTINATION "lib"
    LIBRARY DESTINATION "lib"
    RUNTIME DESTINATION "bin")
if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:ocio_color_widgets>
        DESTINATION bin
        OPTIONAL)
endif()

install(
    EXPORT ocio_color_widgets-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/ocio_color_widgets
    NAMESPACE opendcc::)
make_folder("widgets" ocio_color_widgets)
