set(TARGET_NAME test_runner)

opendcc_make_library(
    ${TARGET_NAME}
    CPPFILES
    main.cpp
    VS_FOLDER
    base
    LIBRARY_DEPENDENCIES
    doctest::doctest)

install(
    EXPORT test_runner-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/test_runner
    NAMESPACE opendcc::)
