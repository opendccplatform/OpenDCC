add_definitions(-DNOMINMAX)
set(PROJECT_NAME schema_ext)

set(HEADERS schema_ext_factory.h)

set(SRC schema_ext_factory.cpp)

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
    TYPE
    PLUGIN
    PLUGIN_DIR
    usd)

file(RELATIVE_PATH LIBRARY_LOCATION ${CMAKE_INSTALL_PREFIX}/plugin/usd/${PROJECT_NAME}/resources/..
     ${CMAKE_INSTALL_PREFIX}/plugin/usd)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.json.in" "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json" @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json
        DESTINATION ${CMAKE_INSTALL_PREFIX}/plugin/usd/${PROJECT_NAME}/resources/)
# install(CODE "file(WRITE \"${CMAKE_INSTALL_PREFIX}/lib/usd/plugInfo.json\" \"{\n    \\\"Includes\\\": [
# \\\"*/resources/\\\" ]\n}\")" )
