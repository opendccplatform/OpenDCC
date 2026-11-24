set(PKG_NAME opendcc.usd_editor.layer_cmds)
set(PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/packages/${PKG_NAME}/)
set(_src_dir lib/opendcc/usd_editor/layer_cmds)

opendcc_make_library(
    ${PKG_NAME}
    TYPE
    PLUGIN
    INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    PRIVATE_HEADERS
    ${_src_dir}/set_edit_target.h
    ${_src_dir}/mute_layer.h
    ${_src_dir}/add_sublayer.h
    ${_src_dir}/remove_sublayer.h
    ${_src_dir}/move_sublayer.h
    ${_src_dir}/apply_layer_text.h
    ${_src_dir}/layer_file_commands.h
    ${_src_dir}/sublayer_utils.h
    ${_src_dir}/entry_point.h
    CPPFILES
    ${_src_dir}/set_edit_target.cpp
    ${_src_dir}/mute_layer.cpp
    ${_src_dir}/add_sublayer.cpp
    ${_src_dir}/remove_sublayer.cpp
    ${_src_dir}/move_sublayer.cpp
    ${_src_dir}/apply_layer_text.cpp
    ${_src_dir}/layer_file_commands.cpp
    ${_src_dir}/entry_point.cpp
    LIBRARY_DEPENDENCIES
    opendcc_lib
    VS_FOLDER
    packages/usd_editor
    PACKAGE_DIR
    ${PKG_NAME}/lib)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/package.toml DESTINATION ${PLUGIN_DIR})

if(DCC_BUILD_TESTS)
    opendcc_add_python_test(LayerCommandsTests SOURCE tests/test_layer_commands.py)
    opendcc_add_python_test(
        LayerUndoTests
        SOURCE
        tests/test_layer_undo.py
        GUI
        ENV
        "QT_QPA_PLATFORM=offscreen"
        "QT_QPA_PLATFORM_PLUGIN_PATH=${DCC_QT_INSTALL_PREFIX}/plugins/platforms")
endif()
