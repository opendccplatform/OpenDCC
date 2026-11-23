set(PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/packages/opendcc.usd_editor.usd_outliner/)

set(OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(_src_dir lib/opendcc/usd_editor/usd_outliner)

opendcc_make_library(
    opendcc.usd_editor.usd_outliner
    QT_AUTOMOC
    TYPE
    PLUGIN
    INCLUDE_DIRS
    ${OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR}
    PUBLIC_HEADERS
    ${_src_dir}/api.h
    ${_src_dir}/usd_item_id.h
    ${_src_dir}/usd_stage_data_source.h
    ${_src_dir}/usd_selection_context.h
    CPPFILES
    ${_src_dir}/usd_stage_data_source.cpp
    ${_src_dir}/usd_selection_context.cpp
    LIBRARY_DEPENDENCIES
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    opendcc_lib
    opendcc.ui.scene_outliner
    usd
    usdGeom
    usdShade
    usdUtils
    sdf
    tf
    work
    PRIVATE_DEFINITIONS
    OPENDCC_USD_OUTLINER_EXPORT
    VS_FOLDER
    packages/usd_editor
    PACKAGE_DIR
    opendcc.usd_editor.usd_outliner/lib)

opendcc_make_shiboken_bindings(
    opendcc.usd_editor.usd_outliner_py
    ${_verbose_shiboken_output}
    TARGET
    opendcc.usd_editor.usd_outliner
    SOURCE_DIR
    ${_src_dir}/python
    GLUE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/usd_stage_data_source.h
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/usd_selection_context.h
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/usd_item_id.h
    GEN_CONFIG_INCLUDE_DIRS
    ${OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR}
    ${PROJECT_SOURCE_DIR}/src/packages/opendcc.ui.scene_outliner/lib
    VS_FOLDER
    packages/usd_editor
    PYTHON_INSTALL_DIR
    "${PLUGIN_DIR}/python/opendcc/usd_editor/usd_outliner")

add_dependencies(opendcc.usd_editor.usd_outliner_py opendcc.ui.scene_outliner_py)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python"
    DESTINATION "${PLUGIN_DIR}"
    FILES_MATCHING
    PATTERN "*.py")

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/package.toml DESTINATION ${PLUGIN_DIR})

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/"
    DESTINATION "include/opendcc/usd_editor/usd_outliner"
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "python" EXCLUDE)

if(DCC_BUILD_TESTS)
    add_subdirectory(tests)
endif()
