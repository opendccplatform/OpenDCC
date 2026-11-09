if(WIN32)
    set(_library_install_dir lib)
else()
    set(_library_install_dir plugin/opendcc)
endif()

set(include render_settings_registry.h render_settings_property_factory.h)

set(src render_settings_registry.cpp render_settings_property_factory.cpp)

opendcc_make_library(
    render_settings_property_proxy
    CPPFILES
    ${include}
    ${src}
    plugInfo.json.in
    INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/src/lib
    ${PROJECT_BINARY_DIR}/include
    ${PROJECT_SOURCE_DIR}
    ${USD_INCLUDE_DIR}
    LIBRARY_DEPENDENCIES
    hydra_render_session_api
    usd_fallback_proxy_utils
    usd_fallback_proxy
    usdImaging
    usd
    LIBRARY_INSTALL_DIR
    ${_library_install_dir}
    ARCHIVE_INSTALL_DIR
    ${_library_install_dir})

if(WIN32)
    file(RELATIVE_PATH LIBRARY_LOCATION
         ${CMAKE_INSTALL_PREFIX}/plugin/opendcc/render_settings_property_proxy/resources/ ${CMAKE_INSTALL_PREFIX}/bin)
else()
    file(RELATIVE_PATH LIBRARY_LOCATION
         ${CMAKE_INSTALL_PREFIX}/plugin/opendcc/render_settings_property_proxy/resources/
         ${CMAKE_INSTALL_PREFIX}/plugin/opendcc)
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.json.in" "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json" @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json
        DESTINATION ${CMAKE_INSTALL_PREFIX}/plugin/opendcc/render_settings_property_proxy/resources/)

install(FILES ${header} DESTINATION "include/render_settings_property_proxy/")
