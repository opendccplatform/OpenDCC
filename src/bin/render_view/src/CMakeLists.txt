include_directories(${OIIO_INCLUDE_DIR} ${ILMBASE_INCLUDE_DIR} ${OCIO_INCLUDE_DIR} ${Boost_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${RENDER_VIEW_SOURCE_DIR})

set(render_view_src main.cpp)
set(CMAKE_AUTOMOC ON)
qt_add_resources(resources ${PROJECT_SOURCE_DIR}/icons/icons.qrc)
include_directories(${RENDER_VIEW_SOURCE_DIR}/thirdparty/qtsingleapplication/src)
include_directories(${PROJECT_SOURCE_DIR}/lib/src)

set(BIN_NAME render_view)
if(UNIX AND NOT APPLE)
    set(BIN_NAME "${BIN_NAME}.bin")
endif()
if(WIN32 AND DCC_RENDER_VIEW_WIN_GUI_EXECUTABLE)

    set(BIN_RESOURCES opendcc_app.rc)
    add_executable(${BIN_NAME} WIN32 ${render_view_src} ${resources}
                                     ${PROJECT_SOURCE_DIR}/icons/render_view/${BIN_RESOURCES})
else()
    add_executable(${BIN_NAME} ${render_view_src} ${resources})
endif()
if(WIN32)
    target_sources(${BIN_NAME} PRIVATE ${WIN32_MANIFEST})
endif()
target_link_libraries(${BIN_NAME} qtsingleapplication)
target_compile_definitions(${BIN_NAME} PRIVATE -DGIT_COMMIT="${OPENDCC_GIT_COMMIT}")
target_link_libraries(${BIN_NAME} image_view color_theme)
target_link_libraries(
    ${BIN_NAME}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    ${DCC_QT_OPENGL_TARGETS}
    Qt${QT_VERSION_MAJOR}::Network
    ${CMAKE_THREAD_LIBS_INIT}
    crash_reporting)
if(WIN32 AND DCC_RENDER_VIEW_WIN_GUI_EXECUTABLE)
    # Qt5 needs Qt5::WinMain linked explicitly. Qt6 has no equivalent to link by hand:
    # Qt6::Core's INTERFACE_LINK_LIBRARIES already pulls in Qt6::EntryPointPrivate for
    # any WIN32_EXECUTABLE target that does not set qt_no_entrypoint.
    if(QT_VERSION_MAJOR LESS 6)
        target_link_libraries(${BIN_NAME} Qt5::WinMain)
    endif()
endif()

if(MSVC)
    set_target_properties(
        ${BIN_NAME} PROPERTIES VS_DEBUGGER_COMMAND "${CMAKE_INSTALL_PREFIX}/bin/$<TARGET_FILE_NAME:${BIN_NAME}>"
                               VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_INSTALL_PREFIX}/bin")
    if(DCC_MAKE_VSDEBUG_LAUNCHER)
        set_target_properties(${BIN_NAME} PROPERTIES VS_DEBUGGER_ENVIRONMENT "${VS_ENVIRONMENT}")
    endif()
endif()

install(
    TARGETS ${BIN_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin)

if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${BIN_NAME}>
        DESTINATION bin
        OPTIONAL)
elseif(UNIX AND NOT APPLE)
    install(
        PROGRAMS ${PROJECT_SOURCE_DIR}/cmake/macros/launch_script.sh
        DESTINATION bin
        RENAME render_view)
endif()
