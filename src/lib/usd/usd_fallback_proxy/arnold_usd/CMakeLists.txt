add_definitions(-DNOMINMAX)
set(PROJECT_NAME arnold_usd_property_proxy)

set(HEADERS metadata_cache.h arnold_usd_property_factory.h)

set(SRC metadata_cache.cpp arnold_usd_property_factory.cpp)

opendcc_make_library(
    ${PROJECT_NAME}
    CPPFILES
    ${HEADERS}
    ${SRC}
    INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/src/lib/
    ${USD_FALLBACK_PROXY_INCLUDE_DIR}
    ${ARNOLD_INCLUDE_DIR}
    ${ARNOLDUSD_INCLUDE_DIR}
    LIBRARY_DEPENDENCIES
    usd_fallback_proxy
    ${ARNOLDUSD_NDR_LIBRARY}
    ${ARNOLD_LIBRARY}
    usdShade
    usdUIExt
    usd_fallback_proxy_utils
    arnold_utils
    TYPE
    PLUGIN
    PLUGIN_DIR
    arnold)

file(RELATIVE_PATH LIBRARY_LOCATION ${CMAKE_INSTALL_PREFIX}/plugin/arnold/${PROJECT_NAME}/resources/..
     ${CMAKE_INSTALL_PREFIX}/plugin/arnold)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.json.in" "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json" @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json
        DESTINATION "${CMAKE_INSTALL_PREFIX}/plugin/arnold/${PROJECT_NAME}/resources/")
install(FILES metadata.mtd DESTINATION "${CMAKE_INSTALL_PREFIX}/plugin/arnold/${PROJECT_NAME}/resources/")

# install(CODE "file(WRITE \"${CMAKE_INSTALL_PREFIX}/lib/arnold/plugInfo.json\" \"{\n    \\\"Includes\\\": [
# \\\"*/resources/\\\" ]\n}\")" )
