set(PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/packages/opendcc.usd_editor.usd_layers/)

set(OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(_src_dir lib/opendcc/usd_editor/usd_layers)

opendcc_make_library(
    opendcc.usd_editor.usd_layers
    QT_AUTOMOC
    TYPE
    PLUGIN
    INCLUDE_DIRS
    ${OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR}
    PUBLIC_HEADERS
    ${_src_dir}/api.h
    ${_src_dir}/usd_layers_model.h
    ${_src_dir}/usd_layers_view.h
    CPPFILES
    ${_src_dir}/usd_layers_model.cpp
    ${_src_dir}/usd_layers_view.cpp
    PRIVATE_DEFINITIONS
    OPENDCC_USD_LAYERS_EXPORT
    LIBRARY_DEPENDENCIES
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    opendcc_lib
    usd
    sdf
    tf
    VS_FOLDER
    packages/usd_editor
    PACKAGE_DIR
    opendcc.usd_editor.usd_layers/lib)

opendcc_make_shiboken_bindings(
    opendcc.usd_editor.usd_layers_py
    ${_verbose_shiboken_output}
    TARGET
    opendcc.usd_editor.usd_layers
    SOURCE_DIR
    ${_src_dir}/python
    GLUE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/usd_layers_model.h
    ${CMAKE_CURRENT_SOURCE_DIR}/${_src_dir}/usd_layers_view.h
    GEN_CONFIG_INCLUDE_DIRS
    ${OPENDCC_CURRENT_PACKAGE_INCLUDE_DIR}
    VS_FOLDER
    packages/usd_editor
    PYTHON_INSTALL_DIR
    "${CMAKE_INSTALL_PREFIX}/packages/opendcc.usd_editor.usd_layers/python/opendcc/usd_editor/usd_layers")

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python"
    DESTINATION "${PLUGIN_DIR}"
    FILES_MATCHING
    PATTERN "*.py")

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/package.toml DESTINATION ${PLUGIN_DIR})

if(DCC_BUILD_TESTS)
    opendcc_add_python_test(UsdLayersModelTests SOURCE tests/test_model.py)
    opendcc_add_python_test(
        UsdLayersViewTests
        SOURCE
        tests/test_view.py
        GUI
        ENV
        "QT_QPA_PLATFORM=offscreen"
        "QT_QPA_PLATFORM_PLUGIN_PATH=${DCC_QT_INSTALL_PREFIX}/plugins/platforms")
    opendcc_add_python_test(
        UsdLayersRenderingTests
        SOURCE
        tests/test_rendering.py
        GUI
        ENV
        "PYTHONPATH=${PROJECT_SOURCE_DIR}/tests/test_utils"
        "QT_SCALE_FACTOR=1"
        "QT_SCREEN_SCALE_FACTORS=1")
    if(DCC_TESTS_GUI)
        opendcc_add_cpp_test(
            UsdLayersExtensionTests
            SOURCE
            tests/test_extensions.cpp
            TARGET
            opendcc.usd_editor.usd_layers
            SUITES
            UsdLayersExtensions)
        set_tests_properties(
            UsdLayersExtensionTests
            PROPERTIES
                RESOURCE_LOCK
                OpenDCCGui
                ENVIRONMENT
                "PYTHONNOUSERSITE=1;QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${DCC_QT_INSTALL_PREFIX}/plugins/platforms"
        )
    endif()
    opendcc_add_python_test(
        UsdLayersTests
        SOURCE
        tests/test_layers.py
        GUI
        ENV
        "QT_QPA_PLATFORM=offscreen"
        "QT_QPA_PLATFORM_PLUGIN_PATH=${DCC_QT_INSTALL_PREFIX}/plugins/platforms")
endif()
