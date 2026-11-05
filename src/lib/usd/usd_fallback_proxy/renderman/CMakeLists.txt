set(PROJECT_NAME renderman_property_proxy)

set(HEADERS renderman_property_factory.h)

set(SRC renderman_property_factory.cpp)

opendcc_make_library(
    ${PROJECT_NAME}
    CPPFILES
    ${HEADERS}
    ${SRC}
    INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/src/lib/
    ${USD_FALLBACK_PROXY_INCLUDE_DIR}
    LIBRARY_DEPENDENCIES
    usd_fallback_proxy
    usd_fallback_proxy_utils
    usdShade
    TYPE
    PLUGIN
    PLUGIN_DIR
    renderman)

include(${USD_CONFIG_FILE})
link_directories(${USD_LIBRARY_DIR})

file(RELATIVE_PATH LIBRARY_LOCATION ${CMAKE_INSTALL_PREFIX}/plugin/renderman/${PROJECT_NAME}/resources/..
     ${CMAKE_INSTALL_PREFIX}/plugin/renderman)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.json.in" "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json" @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json
        DESTINATION ${CMAKE_INSTALL_PREFIX}/plugin/renderman/${PROJECT_NAME}/resources/)
# install(CODE "file(WRITE \"${CMAKE_INSTALL_PREFIX}/lib/renderman/plugInfo.json\" \"{\n    \\\"Includes\\\": [
# \\\"*/resources/\\\" ]\n}\")" )
