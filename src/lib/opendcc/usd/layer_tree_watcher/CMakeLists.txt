set(TARGET_NAME layer_tree_watcher)

opendcc_make_library(
    ${TARGET_NAME}
    CPPFILES
    api.h
    layer_tree_watcher.h
    layer_tree_watcher.cpp
    layer_state_delegates_registry.h
    layer_state_delegates_registry.cpp
    layer_state_delegates_holder.h
    layer_state_delegates_holder.cpp
    PRIVATE_DEFINITIONS
    USD_LAYER_TREE_WATCHER_EXPORT
    INCLUDE_DIRS
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/lib/>"
    "$<INSTALL_INTERFACE:include>"
    ${Boost_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
    LIBRARY_DEPENDENCIES
    ${PYTHON_LIBRARY}
    ${Boost_PYTHON_LIBRARY}
    usd
    test_runner
    logging)

install(
    EXPORT layer_tree_watcher-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/${TARGET_NAME}
    NAMESPACE opendcc::)
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DESTINATION "${CMAKE_INSTALL_PREFIX}/include/opendcc/usd"
    FILES_MATCHING
    PATTERN "*.h")
