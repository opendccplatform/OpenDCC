opendcc_make_library(
    render_view_display_driver_api
    CPPFILES
    api.h
    display_driver_api.cpp
    display_driver_api.h
    PRIVATE_INCLUDE_DIRS
    ${ZMQ_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/src/lib
    PRIVATE_DEFINITIONS
    OPENDCC_RENDER_VIEW_EXPORT
    LIBRARY_DEPENDENCIES
    ${ZMQ_LIBRARIES}
    VS_FOLDER
    render_view)

install(FILES api.h display_driver_api.h DESTINATION "include/opendcc/render_view/display_driver_api")
install(
    EXPORT render_view_display_driver_api-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/render_view_display_driver_api
    NAMESPACE opendcc::)
