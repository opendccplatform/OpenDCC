set(headers api.h utils.h)

set(src utils.cpp)

opendcc_make_library(
    usd_fallback_proxy_utils
    CPPFILES
    ${headers}
    ${src}
    INCLUDE_DIRS
    ${USD_INCLUDE_DIR}
    LIBRARY_DEPENDENCIES
    usd
    usdRender
    usd_fallback_proxy
    LIBRARY_INSTALL_DIR
    plugin/usd
    ARCHIVE_INSTALL_DIR
    plugin/usd)

target_compile_definitions(usd_fallback_proxy_utils PRIVATE -DFALLBACK_PROXY_UTILS_EXPORT)
