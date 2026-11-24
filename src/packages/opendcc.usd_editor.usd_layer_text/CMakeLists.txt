set(PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/packages/opendcc.usd_editor.usd_layer_text/)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python"
    DESTINATION "${PLUGIN_DIR}"
    FILES_MATCHING
    PATTERN "*.py")
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/package.toml DESTINATION ${PLUGIN_DIR})

if(DCC_BUILD_TESTS)
    opendcc_add_python_test(
        UsdLayerTextTests
        SOURCE
        tests/test_layer_text.py
        GUI
        ENV
        "QT_QPA_PLATFORM=offscreen"
        "QT_QPA_PLATFORM_PLUGIN_PATH=${DCC_QT_INSTALL_PREFIX}/plugins/platforms")
endif()
