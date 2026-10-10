set(PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/packages/opendcc.anim_engine.core/)

set(OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(_src_dir lib/opendcc/anim_engine/core)

opendcc_make_library(
    opendcc.anim_engine.core
    TYPE
    PLUGIN
    INCLUDE_DIRS
    ${OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR}
    PUBLIC_HEADERS
    ${_src_dir}/api.h
    ${_src_dir}/engine.h
    ${_src_dir}/commands.h
    ${_src_dir}/anim_engine_curve.h
    ${_src_dir}/utils.h
    ${_src_dir}/session.h
    ${_src_dir}/stage_listener.h
    CPPFILES
    ${_src_dir}/stage_listener.cpp
    ${_src_dir}/session.cpp
    ${_src_dir}/utils.cpp
    ${_src_dir}/anim_engine_curve.cpp
    ${_src_dir}/commands.cpp
    ${_src_dir}/engine.cpp
    PRIVATE_DEFINITIONS
    USE_ANIM_ENGINE
    ANIM_ENGINE_EXPORT
    LIBRARY_DEPENDENCIES
    opendcc.vendor.animx
    opendcc.anim_engine.curve
    opendcc_lib
    opendcc.anim_engine.schema
    VS_FOLDER
    packages/anim_engine
    PACKAGE_DIR
    opendcc.anim_engine.core/lib
    PYMODULE_NAME
    _anim_engine_core
    PYMODULE_SOURCES
    ${_src_dir}/wrap_engine.cpp
    PYMODULE_INSTALL_PATH
    packages/opendcc.anim_engine.core/python/opendcc/anim_engine/core)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python"
    DESTINATION "${PLUGIN_DIR}"
    FILES_MATCHING
    PATTERN "*.py")

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/pxr_plugins/plugInfo.json DESTINATION ${PLUGIN_DIR}/pxr_plugins)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/package.toml DESTINATION ${PLUGIN_DIR})

if(DCC_BUILD_TESTS)
    add_subdirectory(tests)
endif()
