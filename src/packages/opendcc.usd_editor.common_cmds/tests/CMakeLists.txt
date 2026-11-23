opendcc_add_python_test(CommonCommandsTests SOURCE test_common_commands.py GUI)

opendcc_add_cpp_test(
    usd_prim_properties_recording
    SOURCE
    prim_properties_tests.cpp
    TARGET
    ${PKG_NAME}
    SUITES
    UsdPrimPropertiesRecording)

opendcc_add_python_test(usd_prim_properties SOURCE test_prim_properties.py)
