target_compile_definitions(packaging PRIVATE OPENDCC_PACKAGING_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
                                             OPENDCC_PACKAGING_TEST_PYTHON="${PYTHON_EXECUTABLE}")
if(WIN32)
    opendcc_add_cpp_test(
        PackageLoaderTests
        SOURCE
        test_package_loader.cpp
        TARGET
        packaging
        SUITES
        PackagingPackageLoader)
endif()
opendcc_add_cpp_test(
    TomlParserTests
    SOURCE
    test_toml_parser.cpp
    TARGET
    packaging
    SUITES
    PackagingTOMLParserTests)

opendcc_add_cpp_test(
    PackageRegistryTests
    SOURCE
    test_package_registry.cpp
    TARGET
    packaging
    SUITES
    PackagingPackageRegistry
    PackagingRegistry)
opendcc_add_cpp_test(
    PackageLifecycleTests
    SOURCE
    test_package_lifecycle.cpp
    TARGET
    packaging
    SUITES
    PackagingLifecycle)
opendcc_add_python_test(PackagingPythonTests SOURCE test_packaging.py)

opendcc_add_cpp_test(
    PackageEnvironmentTests
    SOURCE
    test_package_environment.cpp
    TARGET
    packaging
    SUITES
    PackagingEnvironment)

opendcc_make_library(
    packaging_lifecycle_tests
    TYPE
    SHARED
    CPPFILES
    fixtures/lifecycle_entry_point.cpp
    LIBRARY_DEPENDENCIES
    packaging
    VS_FOLDER
    base/packaging)

opendcc_make_library(
    packaging_plain_tests
    TYPE
    SHARED
    CPPFILES
    fixtures/plain_library.cpp
    VS_FOLDER
    base/packaging)

target_compile_definitions(
    packaging PRIVATE OPENDCC_PACKAGING_LIFECYCLE_LIBRARY="$<TARGET_FILE:packaging_lifecycle_tests>"
                      OPENDCC_PACKAGING_PLAIN_LIBRARY="$<TARGET_FILE:packaging_plain_tests>")

opendcc_make_library(
    packaging_tests_a
    TYPE
    SHARED
    CPPFILES
    fixtures/entry_point.cpp
    LIBRARY_DEPENDENCIES
    packaging
    PRIVATE_DEFINITIONS
    OPENDCC_PACKAGING_TESTS_EXPORT
    VS_FOLDER
    base/packaging)

opendcc_make_library(
    packaging_tests_b
    TYPE
    SHARED
    CPPFILES
    fixtures/entry_point.cpp
    LIBRARY_DEPENDENCIES
    packaging
    PRIVATE_DEFINITIONS
    OPENDCC_PACKAGING_TESTS_EXPORT
    VS_FOLDER
    base/packaging)

opendcc_add_cpp_test(
    PackageValueTests
    SOURCE
    test_package_values.cpp
    TARGET
    packaging
    SUITES
    PackagingValues)

opendcc_add_python_test(
    PackagingWithoutUsdTests
    SOURCE
    test_without_usd.py
    STANDALONE
    ARGS
    "$<TARGET_FILE:packaging_py>"
    "$<TARGET_FILE_DIR:packaging>"
    "$<TARGET_FILE_DIR:logging>"
    "$<TARGET_FILE_DIR:utils>"
    "$<TARGET_FILE_DIR:test_runner>")

opendcc_make_library(
    packaging_value_tests
    TYPE
    SHARED
    CPPFILES
    fixtures/value_library.cpp
    LIBRARY_DEPENDENCIES
    packaging
    VS_FOLDER
    base/packaging)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Keep fixture-local any managers distinct so symbol interposition cannot mask unload failures.
    set_target_properties(packaging_value_tests PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    target_link_options(packaging_value_tests PRIVATE "-Wl,-Bsymbolic")
endif()
target_compile_definitions(packaging PRIVATE OPENDCC_PACKAGING_VALUE_LIBRARY="$<TARGET_FILE:packaging_value_tests>")
opendcc_add_cpp_test(
    PackageValueLibraryTests
    SOURCE
    test_package_value_library.cpp
    TARGET
    packaging
    SUITES
    PackagingValueLibrary)
