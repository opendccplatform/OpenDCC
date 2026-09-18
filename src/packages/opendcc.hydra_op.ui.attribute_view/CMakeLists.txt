set(PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/packages/opendcc.hydra_op.ui.attribute_view/)

set(OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(_src_dir lib/opendcc/hydra_op/ui/attribute_view)

opendcc_make_library(
    opendcc.hydra_op.ui.attribute_view
    QT_AUTOMOC
    TYPE
    PLUGIN
    INCLUDE_DIRS
    ${OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR}
    PRIVATE_HEADERS
    ${_src_dir}/dataSourceTreeWidget.h
    ${_src_dir}/dataSourceValueTreeView.h
    ${_src_dir}/hydra_op_attribute_view.h
    ${_src_dir}/entry_point.h
    CPPFILES
    ${_src_dir}/dataSourceTreeWidget.cpp
    ${_src_dir}/dataSourceValueTreeView.cpp
    ${_src_dir}/hydra_op_attribute_view.cpp
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
    opendcc.hydra_op.ui.attribute_view/lib)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/package.toml DESTINATION ${PLUGIN_DIR})
