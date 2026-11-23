target_compile_definitions(opendcc.usd_editor.usd_outliner PRIVATE OPENDCC_USD_OUTLINER_TESTS)
target_include_directories(opendcc.usd_editor.usd_outliner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

opendcc_add_cpp_test(
    usd_outliner_conversion_bridge
    SOURCE
    binding_tests.cpp
    TARGET
    opendcc.usd_editor.usd_outliner
    SUITES
    UsdOutlinerBindings)
set(_outliner_test_environment "PYTHONPATH=${PROJECT_SOURCE_DIR}/tests/test_utils")
foreach(_suite IN ITEMS bindings visibility editing panel updates extensions)
    opendcc_add_python_test(usd_outliner_${_suite} SOURCE test_${_suite}.py GUI ENV ${_outliner_test_environment})
endforeach()
opendcc_add_python_test(
    usd_outliner_gui
    SOURCE
    test_gui.py
    GUI
    ENV
    ${_outliner_test_environment}
    "QT_SCALE_FACTOR=1")
opendcc_add_python_test(
    usd_outliner_pointer
    SOURCE
    test_pointer.py
    GUI
    ENV
    ${_outliner_test_environment}
    "QT_QPA_PLATFORM=offscreen"
    "QT_SCALE_FACTOR=1")
opendcc_add_cpp_test(
    usd_outliner_work
    SOURCE
    work_tests.cpp
    TARGET
    opendcc.usd_editor.usd_outliner
    SUITES
    UsdOutlinerWork)
