set(TARGET_NAME py_commands_api)
set(PYMODULE_NAME cmds)

opendcc_make_library(
    ${TARGET_NAME}
    CPPFILES
    api.h
    utils.h
    python_command_interface.h
    python_command_interface.cpp
    python.h
    python.cpp
    INCLUDE_DIRS
    ${PYTHON_INCLUDE_DIR}
    LIBRARY_DEPENDENCIES
    commands_api
    py_utils
    pybind11::pybind11
    ${PYTHON_LIBRARY}
    PRIVATE_DEFINITIONS
    OPENDCC_BASE_PY_COMMANDS_EXPORT
    PYMODULE_NAME_STR="opendcc.${PYMODULE_NAME}"
    VS_FOLDER
    base/commands_api)
target_precompile_headers(${TARGET_NAME} PRIVATE ${PRECOMPILED_INCLUDES})

set(WRAP_TARGET_NAME wrap_${TARGET_NAME})
opendcc_make_python_module(
    ${WRAP_TARGET_NAME}
    NO_BOOST_PYTHON
    PYMODULE_NAME
    _${PYMODULE_NAME}
    PYMODULE_SOURCES
    utils.h
    wrap.cpp
    LIBRARY_DEPENDENCIES
    ${TARGET_NAME}
    PYMODULE_INSTALL_DIR
    opendcc/${PYMODULE_NAME}
    VS_FOLDER
    base/commands_api)
target_precompile_headers(${WRAP_TARGET_NAME} PRIVATE ${PRECOMPILED_INCLUDES})

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/__init__.py.in" "${CMAKE_CURRENT_BINARY_DIR}/__init__.py" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/__init__.py"
        DESTINATION "${DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT}/opendcc/${PYMODULE_NAME}")
