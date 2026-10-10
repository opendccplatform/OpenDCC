set(_opendcc_test_application "${CMAKE_INSTALL_PREFIX}/bin/dcc_base${CMAKE_EXECUTABLE_SUFFIX}")
if(UNIX AND NOT APPLE)
    set(_opendcc_test_binary "${CMAKE_INSTALL_PREFIX}/bin/dcc_base.bin")
else()
    set(_opendcc_test_binary "${_opendcc_test_application}")
endif()

set(DCC_TEST_RUNTIME_PATHS
    ""
    CACHE STRING "Additional runtime library directories for prebuilt test dependencies")

function(_opendcc_register_test TEST_NAME)
    cmake_parse_arguments(args "" "" "COMMAND;ENV" ${ARGN})

    set(_test_dir "${PROJECT_BINARY_DIR}/test/${TEST_NAME}")
    file(MAKE_DIRECTORY "${_test_dir}")
    set(_runtime_paths
        "${CMAKE_INSTALL_PREFIX}/bin"
        "${CMAKE_INSTALL_PREFIX}/lib"
        "${USD_LIBRARY_DIR}"
        "${Boost_LIBRARY_DIR_RELEASE}"
        "${TBB_INCLUDE_DIRS}/../bin"
        "${TBB_INCLUDE_DIRS}/../lib"
        "$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>"
        "$<TARGET_FILE_DIR:OpenEXR::OpenEXR>"
        ${DCC_TEST_RUNTIME_PATHS})
    list(REMOVE_DUPLICATES _runtime_paths)
    set(_launcher "${PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/launch_test.py")
    foreach(_path IN LISTS _runtime_paths)
        if(_path)
            list(APPEND _launcher --runtime-path "${_path}")
        endif()
    endforeach()
    add_test(NAME ${TEST_NAME} COMMAND ${_launcher} -- ${args_COMMAND})
    set_tests_properties(${TEST_NAME} PROPERTIES WORKING_DIRECTORY "${_test_dir}" TIMEOUT 120 ENVIRONMENT
                                                 "PYTHONNOUSERSITE=1;${args_ENV}")
endfunction()

# Compile the source into its owning library and run only its named doctest suites.
function(opendcc_add_cpp_test TEST_NAME)
    cmake_parse_arguments(args "" "SOURCE;TARGET" "SUITES" ${ARGN})
    if(args_UNPARSED_ARGUMENTS
       OR NOT args_SOURCE
       OR NOT args_TARGET
       OR NOT args_SUITES)
        message(FATAL_ERROR "${TEST_NAME}: expected SOURCE <file.cpp> TARGET <library> SUITES <doctest suite names>")
    endif()
    target_sources(${args_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/${args_SOURCE}")
    list(JOIN args_SUITES "," _suites)
    _opendcc_register_test(
        ${TEST_NAME}
        COMMAND
        "${_opendcc_test_binary}"
        --with-tests
        --no-breaks
        --exit=true
        "--test-suite=${_suites}")
    # doctest returns success when its filter matches no cases.
    set_tests_properties(${TEST_NAME} PROPERTIES FAIL_REGULAR_EXPRESSION "test cases:[ ]+0[ ]")
endfunction()

# Embedded Python is the default; STANDALONE is for tests that launch the application themselves.
function(opendcc_add_python_test TEST_NAME)
    cmake_parse_arguments(args "GUI;STANDALONE" "SOURCE" "ARGS;ENV" ${ARGN})
    if(args_UNPARSED_ARGUMENTS OR NOT args_SOURCE)
        message(FATAL_ERROR "${TEST_NAME}: expected SOURCE <file.py> [GUI] [STANDALONE] [ARGS ...] [ENV ...]")
    endif()
    if(args_ARGS AND NOT args_STANDALONE)
        message(FATAL_ERROR "${TEST_NAME}: ARGS requires STANDALONE; embedded tests are unittest modules")
    endif()
    if(args_GUI AND NOT DCC_TESTS_GUI)
        return()
    endif()
    set(_source "${CMAKE_CURRENT_SOURCE_DIR}/${args_SOURCE}")
    if(args_STANDALONE)
        set(_command "${PYTHON_EXECUTABLE}" "${_source}" ${args_ARGS})
    else()
        set(_command "${_opendcc_test_application}")
        if(args_GUI)
            list(APPEND _command --gui)
        endif()
        list(APPEND _command --script "${PROJECT_SOURCE_DIR}/tests/run_tests.py" "${_source}")
    endif()
    _opendcc_register_test(${TEST_NAME} COMMAND ${_command} ENV ${args_ENV})
    if(args_GUI)
        set_tests_properties(${TEST_NAME} PROPERTIES RESOURCE_LOCK OpenDCCGui)
    endif()
endfunction()
