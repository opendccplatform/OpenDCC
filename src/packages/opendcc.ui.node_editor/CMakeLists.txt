set(PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/packages/opendcc.ui.node_editor/)

set(OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(_src_dir lib/opendcc/ui/node_editor)

opendcc_make_library(
    opendcc.ui.node_editor
    QT_AUTOMOC
    TYPE
    PLUGIN
    INCLUDE_DIRS
    ${OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR}
    PUBLIC_HEADERS
    ${_src_dir}/api.h
    ${_src_dir}/scene.h
    ${_src_dir}/node.h
    ${_src_dir}/connection.h
    ${_src_dir}/graph_model.h
    ${_src_dir}/view.cpp
    ${_src_dir}/view.h
    ${_src_dir}/tab_search.h
    ${_src_dir}/layout.h
    ${_src_dir}/layout.cpp
    ${_src_dir}/text_item.h
    ${_src_dir}/item_registry.h
    ${_src_dir}/thumbnail_cache.h
    ${_src_dir}/node_snapper.h
    CPPFILES
    ${_src_dir}/scene.cpp
    ${_src_dir}/node.cpp
    ${_src_dir}/connection.cpp
    ${_src_dir}/tab_search.cpp
    ${_src_dir}/text_item.cpp
    ${_src_dir}/thumbnail_cache.cpp
    ${_src_dir}/node_snapper.cpp
    LIBRARY_DEPENDENCIES
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    ${DCC_QT_OPENGL_TARGETS}
    ${DCC_QT_SVG_TARGETS}
    Graphviz
    PRIVATE_DEFINITIONS
    OPENDCC_UI_NODE_EDITOR_EXPORT
    VS_FOLDER
    packages/ui
    PACKAGE_DIR
    opendcc.ui.node_editor/lib)

opendcc_make_shiboken_bindings(
    opendcc.ui.node_editor_py
    ${_verbose_shiboken_output}
    TARGET
    opendcc.ui.node_editor
    SOURCE_DIR
    ${_src_dir}/python
    GEN_CONFIG_INCLUDE_DIRS
    ${OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR}
    VS_FOLDER
    packages/ui
    PYTHON_INSTALL_DIR
    "${CMAKE_INSTALL_PREFIX}/packages/opendcc.ui.node_editor/python/opendcc/ui/node_editor")

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python"
    DESTINATION "${PLUGIN_DIR}/"
    FILES_MATCHING
    PATTERN "*.py")
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/package.toml DESTINATION ${PLUGIN_DIR})
