find_package(Threads REQUIRED)
target_link_libraries(logging PRIVATE test_runner Threads::Threads)
opendcc_add_cpp_test(
    LoggingNativeTests
    SOURCE
    test_logging.cpp
    TARGET
    logging
    SUITES
    LoggingNative)
