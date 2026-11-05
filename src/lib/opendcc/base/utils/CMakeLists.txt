set(TARGET_NAME utils)

opendcc_make_library(
    ${TARGET_NAME}
    CPPFILES
    api.h
    process.cpp
    process.h
    string_utils.cpp
    string_utils.h
    library.h
    library.cpp
    allocation.h
    allocation.cpp
    expected.h
    ../fs_utils/defines.h
    ../fs_utils/glob.h
    ../fs_utils/glob.cpp
    hash.h
    env.h
    env.cpp
    scope_guard.h
    noncopyable.h
    debug.h
    debug.cpp
    VS_FOLDER
    base
    PRIVATE_DEFINITIONS
    OPENDCC_BASE_UTILS_EXPORT
    LIBRARY_DEPENDENCIES
    ${CMAKE_DL_LIBS})

install(
    DIRECTORY .
    DESTINATION "include/opendcc/base/utils"
    FILES_MATCHING
    PATTERN "*.h")

install(
    EXPORT utils-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/${TARGET_NAME}
    NAMESPACE opendcc::)

install(
    DIRECTORY ../fs_utils/
    DESTINATION "include/opendcc/base/fs_utils"
    FILES_MATCHING
    PATTERN "*.h")
