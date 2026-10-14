target_link_libraries(utils PRIVATE test_runner)
opendcc_add_cpp_test(
    FileSystemGlobTests
    SOURCE
    test_glob.cpp
    TARGET
    utils
    SUITES
    FileSystemGlob)

opendcc_add_cpp_test(
    StringUtilsTests
    SOURCE
    test_string_utils.cpp
    TARGET
    utils
    SUITES
    StringUtils)
