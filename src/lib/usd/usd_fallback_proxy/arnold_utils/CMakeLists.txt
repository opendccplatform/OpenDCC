add_definitions(-DNOMINMAX)
set(PROJECT_NAME arnold_utils)

set(HEADERS api.h utils.h)

set(SRC utils.cpp)

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
    ${ARNOLDUSD_NDR_LIBRARY}
    ${ARNOLD_LIBRARY}
    ndr
    sdf
    LIBRARY_INSTALL_DIR
    plugin/usd
    ARCHIVE_INSTALL_DIR
    plugin/usd)

target_compile_definitions(${PROJECT_NAME} PRIVATE -DARNOLD_UTILS_EXPORT)
