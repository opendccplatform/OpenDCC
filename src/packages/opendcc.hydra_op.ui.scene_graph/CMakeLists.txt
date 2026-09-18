set(PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/packages/opendcc.hydra_op.ui.scene_graph/)

set(OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(_src_dir lib/opendcc/hydra_op/ui/scene_graph)

opendcc_make_library(
    opendcc.hydra_op.ui.scene_graph
    QT_AUTOMOC
    TYPE
    PLUGIN
    INCLUDE_DIRS
    ${OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR}
    PRIVATE_HEADERS
    ${_src_dir}/hydra_op_scene_graph.h
    ${_src_dir}/hydra_op_scene_graph_tree_widget.h
    ${_src_dir}/entry_point.h
    CPPFILES
    ${_src_dir}/hydra_op_scene_graph.cpp
    ${_src_dir}/hydra_op_scene_graph_tree_widget.cpp
    ${_src_dir}/entry_point.cpp
    LIBRARY_DEPENDENCIES
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    opendcc_lib
    opendcc.hydra_op
    hd
    VS_FOLDER
    packages/hydra_op
    PACKAGE_DIR
    opendcc.hydra_op.ui.scene_graph/lib)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/package.toml DESTINATION ${PLUGIN_DIR})
