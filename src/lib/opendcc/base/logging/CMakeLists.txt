if(${CMAKE_CXX_STANDARD} GREATER 14)
    set(_deps_libs utils)
endif()
opendcc_make_library(
    logging
    TYPE
    SHARED
    PUBLIC_HEADERS
    api.h
    logger.h
    logging_delegate.h
    default_logging_delegate.h
    logging_utils.h
    CPPFILES
    logger.cpp
    default_logging_delegate.cpp
    LIBRARY_DEPENDENCIES
    ${_deps_libs}
    PRIVATE_DEFINITIONS
    OPENDCC_BASE_LOGGING_EXPORT
    VS_FOLDER
    base)

install(
    EXPORT logging-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/logging
    NAMESPACE opendcc::)

install(
    DIRECTORY .
    DESTINATION "include/opendcc/base/logging"
    FILES_MATCHING
    PATTERN "*.h")

install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/lib/opendcc/base/vendor/spdlog"
    DESTINATION "include/opendcc/base/vendor"
    FILES_MATCHING
    PATTERN "*.h")
