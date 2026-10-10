opendcc_add_cpp_test(
    CommandBlockTests
    SOURCE
    test_block.cpp
    TARGET
    commands_api
    SUITES
    "CommandBlock/UndoCommandBlock")
