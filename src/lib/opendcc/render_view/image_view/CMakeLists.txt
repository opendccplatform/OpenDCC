include_directories(
    ${OPENGL_INCLUDE_DIR}
    ${GLEW_INCLUDE_DIR}
    ${OIIO_INCLUDE_DIR}
    ${ILMBASE_INCLUDE_DIR}
    ${OCIO_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${RENDER_VIEW_SOURCE_DIR}
    ${PYTHON_INCLUDE_DIRS})

set(src
    gl_widget.cpp
    app.cpp
    qt_utils.cpp
    listener_thread.cpp
    gl_utils.cpp
    app_gui.cpp
    image_cache.cpp
    gl_widget_tools.cpp
    color_convert.cpp
    preferences_window.cpp
    key_sequence_edit.cpp
    HUD.cpp
    metadata_view.cpp
    translator.cpp)

set(headers
    gl_widget.h
    app.h
    qt_utils.h
    listener_thread.hpp
    gl_utils.hpp
    image_cache.h
    gl_widget_tools.hpp
    preferences_window.hpp
    key_sequence_edit.h
    color_convert.hpp
    HUD.hpp
    metadata_view.h
    stylesheet.h
    translator.h)

set(CMAKE_AUTOMOC ON)
qt_add_resources(resources shaders/shaders.qrc)
include_directories(${PROJECT_SOURCE_DIR}/src/lib)
include_directories(${RENDER_VIEW_SOURCE_DIR}/thirdparty/qtsingleapplication/src)

add_library(image_view SHARED ${src} ${headers} ${resources})
target_compile_definitions(image_view PRIVATE -DOPENDCC_RENDER_VIEW_EXPORT)

make_folder("render_view" image_view)

target_link_libraries(
    image_view
    Imath::Imath
    OpenEXR::OpenEXR
    ${Boost_REGEX_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
    ${Boost_THREAD_LIBRARY}
    ${Boost_DATE_TIME_LIBRARY}
    ${Boost_CHRONO_LIBRARY}
    ${ZMQ_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${GLEW_LIBRARY}
    ${OIIO_OpenImageIO_LIBRARY}
    ${OIIO_OpenImageIO_Util_LIBRARY}
    ${OCIO_LIBRARIES}
    ${PYTHON_LIBRARY}
    ipc_commands_api)
target_link_libraries(
    image_view
    render_view_display_driver_api
    app_version
    app_config
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    ${DCC_QT_OPENGL_TARGETS}
    Qt${QT_VERSION_MAJOR}::Network)

install(
    TARGETS image_view
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)

if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:image_view>
        DESTINATION bin
        OPTIONAL)
endif()
