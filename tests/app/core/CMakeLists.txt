opendcc_add_cpp_test(
    RichSelectionTests
    SOURCE
    test_rich_selection.cpp
    TARGET
    opendcc_lib
    SUITES
    RichSelection)
opendcc_add_cpp_test(
    IntervalVectorTests
    SOURCE
    test_interval_vector.cpp
    TARGET
    opendcc_lib
    SUITES
    IntervalVector)
opendcc_add_cpp_test(
    SettingsTests
    SOURCE
    test_settings.cpp
    TARGET
    opendcc_lib
    SUITES
    SettingsTests)
opendcc_add_cpp_test(
    UsdClipboardTests
    SOURCE
    test_usd_clipboard.cpp
    TARGET
    opendcc_lib
    SUITES
    UsdClipboard)

opendcc_add_python_test(ApplicationTests SOURCE test_application.py)
opendcc_add_python_test(SelectionTests SOURCE test_selection.py)

opendcc_add_python_test(ApplicationCallbackTests SOURCE test_application_callbacks.py)
