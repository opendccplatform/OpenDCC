set(TARGET_NAME utils)

add_library(
    ${TARGET_NAME} SHARED
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
    debug.cpp)

make_folder("base" ${TARGET_NAME})

target_compile_definitions(${TARGET_NAME} PRIVATE -DOPENDCC_BASE_UTILS_EXPORT)
target_link_libraries(${TARGET_NAME} PUBLIC ${CMAKE_DL_LIBS})

install(
    TARGETS ${TARGET_NAME}
    EXPORT utils-targets
    ARCHIVE DESTINATION "lib"
    RUNTIME DESTINATION "bin"
    LIBRARY DESTINATION "lib")

if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${TARGET_NAME}>
        DESTINATION bin
        OPTIONAL)
endif()

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
