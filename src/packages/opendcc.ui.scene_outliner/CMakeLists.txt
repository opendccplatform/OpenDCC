set(PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/packages/opendcc.ui.scene_outliner/)

set(OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(_src_dir lib/opendcc/ui/scene_outliner)

opendcc_make_library(
    opendcc.ui.scene_outliner
    QT_AUTOMOC
    TYPE
    PLUGIN
    INCLUDE_DIRS
    ${OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR}
    PUBLIC_HEADERS
    ${_src_dir}/api.h
    ${_src_dir}/item_id.h
    ${_src_dir}/tree_data_source.h
    ${_src_dir}/search_request.h
    ${_src_dir}/item_predicate.h
    ${_src_dir}/selection_context.h
    ${_src_dir}/notice_batcher.h
    ${_src_dir}/hierarchy_cache.h
    ${_src_dir}/outliner_model.h
    ${_src_dir}/outliner_proxy_model.h
    ${_src_dir}/outliner_tree_view.h
    ${_src_dir}/outliner_widget.h
    PRIVATE_HEADERS
    ${_src_dir}/details/cancellation_token.h
    CPPFILES
    ${_src_dir}/item_id.cpp
    ${_src_dir}/search_request.cpp
    ${_src_dir}/tree_data_source.cpp
    ${_src_dir}/notice_batcher.cpp
    ${_src_dir}/hierarchy_cache.cpp
    ${_src_dir}/outliner_model.cpp
    ${_src_dir}/outliner_proxy_model.cpp
    ${_src_dir}/outliner_tree_view.cpp
    ${_src_dir}/outliner_widget.cpp
    LIBRARY_DEPENDENCIES
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    logging
    PRIVATE_DEFINITIONS
    OPENDCC_SCENE_OUTLINER_EXPORT
    VS_FOLDER
    packages/ui
    PACKAGE_DIR
    opendcc.ui.scene_outliner/lib)

if(QT_VERSION_MAJOR EQUAL 5)
    set(_outliner_to_python_template cppvector_to_pylist_conversion)
    set(_outliner_to_cpp_template pyseq_to_cppvector_conversion)
else()
    set(_outliner_to_python_template shiboken_conversion_cppsequence_to_pylist)
    set(_outliner_to_cpp_template shiboken_conversion_pyiterable_to_cppsequentialcontainer_reserve)
endif()

opendcc_make_shiboken_bindings(
    opendcc.ui.scene_outliner_py
    ${_verbose_shiboken_output}
    TARGET
    opendcc.ui.scene_outliner
    SOURCE_DIR
    ${_src_dir}/python
    GLUE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/item_id.h
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/tree_data_source.h
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/selection_context.h
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/item_predicate.h
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/outliner_widget.h
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/outliner_model.h
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/outliner_proxy_model.h
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/outliner_tree_view.h
    GEN_CONFIG_INCLUDE_DIRS
    ${OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR}
    VS_FOLDER
    packages/ui
    PYTHON_INSTALL_DIR
    "${CMAKE_INSTALL_PREFIX}/packages/opendcc.ui.scene_outliner/python/opendcc/ui/scene_outliner")

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python"
    DESTINATION "${PLUGIN_DIR}"
    FILES_MATCHING
    PATTERN "*.py")

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/package.toml DESTINATION ${PLUGIN_DIR})

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/"
    DESTINATION "include/opendcc/ui/scene_outliner"
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "python" EXCLUDE)

if(DCC_BUILD_TESTS)
    add_subdirectory(tests)
endif()
