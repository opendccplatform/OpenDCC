set(TARGET_NAME usd_live_share)

opendcc_make_library(
    ${TARGET_NAME}
    CPPFILES
    api.h
    live_share_edits.h
    live_share_edits.cpp
    live_share_state_delegate.h
    live_share_state_delegate.cpp
    live_share_session.h
    live_share_session.cpp
    PRIVATE_DEFINITIONS
    USD_LIVE_SHARE_EXPORT
    BOOST_ALL_DYN_LINK
    LIBRARY_DEPENDENCIES
    ${Boost_SYSTEM_LIBRARY}
    layer_tree_watcher
    usd_ipc_serialization
    utils
    Threads::Threads)

if(CMAKE_COMPILER_IS_GNUCC)
    target_link_libraries(${TARGET_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT})
endif()

install(
    EXPORT usd_live_share-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/${TARGET_NAME}
    NAMESPACE opendcc::)

add_subdirectory(python)
