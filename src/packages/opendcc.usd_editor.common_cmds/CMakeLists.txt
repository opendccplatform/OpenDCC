set(PKG_NAME opendcc.usd_editor.common_cmds)

set(PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/packages/${PKG_NAME}/)

set(OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(_src_dir lib/opendcc/usd_editor/common_cmds)

opendcc_make_library(
    ${PKG_NAME}
    TYPE
    PLUGIN
    INCLUDE_DIRS
    ${OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR}
    PRIVATE_HEADERS
    ${_src_dir}/api.h
    ${_src_dir}/entry_point.h
    ${_src_dir}/assign_material.h
    ${_src_dir}/center_pivot.h
    ${_src_dir}/clear_edits.h
    ${_src_dir}/copy_attribute.h
    ${_src_dir}/copy_prims.h
    ${_src_dir}/create_camera_from_view.h
    ${_src_dir}/create_material.h
    ${_src_dir}/create_mesh.h
    ${_src_dir}/create_prim.h
    ${_src_dir}/cut_prims.h
    ${_src_dir}/duplicate_prim.h
    ${_src_dir}/export_selection.h
    ${_src_dir}/group_prim.h
    ${_src_dir}/parent_prim.h
    ${_src_dir}/paste_attribute.h
    ${_src_dir}/paste_prims.h
    ${_src_dir}/pick_walk.h
    ${_src_dir}/remove_prims.h
    ${_src_dir}/rename_prim.h
    ${_src_dir}/selection.h
    ${_src_dir}/prim_properties.h
    CPPFILES
    ${_src_dir}/entry_point.cpp
    ${_src_dir}/assign_material.cpp
    ${_src_dir}/center_pivot.cpp
    ${_src_dir}/clear_edits.cpp
    ${_src_dir}/copy_attribute.cpp
    ${_src_dir}/copy_prims.cpp
    ${_src_dir}/create_camera_from_view.cpp
    ${_src_dir}/create_material.cpp
    ${_src_dir}/create_mesh.cpp
    ${_src_dir}/create_prim.cpp
    ${_src_dir}/cut_prims.cpp
    ${_src_dir}/duplicate_prim.cpp
    ${_src_dir}/export_selection.cpp
    ${_src_dir}/group_prim.cpp
    ${_src_dir}/parent_prim.cpp
    ${_src_dir}/paste_attribute.cpp
    ${_src_dir}/paste_prims.cpp
    ${_src_dir}/pick_walk.cpp
    ${_src_dir}/remove_prims.cpp
    ${_src_dir}/rename_prim.cpp
    ${_src_dir}/selection.cpp
    ${_src_dir}/prim_properties.cpp
    PRIVATE_DEFINITIONS
    OPENDCC_USD_EDITOR_COMMON_CMDS_EXPORT
    LIBRARY_DEPENDENCIES
    opendcc_lib
    VS_FOLDER
    packages/usd_editor
    PACKAGE_DIR
    ${PKG_NAME}/lib)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/package.toml DESTINATION ${PLUGIN_DIR})

if(DCC_BUILD_TESTS)
    add_subdirectory(tests)
endif()
