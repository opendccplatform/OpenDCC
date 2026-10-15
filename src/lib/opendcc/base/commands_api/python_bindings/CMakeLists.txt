set(TARGET_NAME py_commands_api)
set(PYMODULE_NAME cmds)

add_library(${TARGET_NAME} SHARED api.h utils.h python_command_interface.h python_command_interface.cpp python.h
                                  python.cpp)

target_include_directories(${TARGET_NAME} PUBLIC $${PYTHON_INCLUDE_DIR})
target_precompile_headers(${TARGET_NAME} PRIVATE ${PRECOMPILED_INCLUDES})
target_link_libraries(${TARGET_NAME} PUBLIC commands_api py_utils pybind11::pybind11 ${PYTHON_LIBRARY})
target_compile_definitions(${TARGET_NAME} PRIVATE -DOPENDCC_BASE_PY_COMMANDS_EXPORT
                                                  -DPYMODULE_NAME_STR="opendcc.${PYMODULE_NAME}")
install(TARGETS ${TARGET_NAME})
if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${TARGET_NAME}>
        DESTINATION bin
        OPTIONAL)
endif()

set(WRAP_TARGET_NAME wrap_${TARGET_NAME})
add_library(${WRAP_TARGET_NAME} SHARED utils.h wrap.cpp)

target_compile_definitions(${WRAP_TARGET_NAME} PRIVATE -DPYMODULE_NAME=_${PYMODULE_NAME})
target_precompile_headers(${WRAP_TARGET_NAME} PRIVATE ${PRECOMPILED_INCLUDES})
target_link_libraries(${WRAP_TARGET_NAME} PUBLIC ${TARGET_NAME})
set_target_properties(
    ${WRAP_TARGET_NAME}
    PROPERTIES PREFIX ""
               OUTPUT_NAME _${PYMODULE_NAME}
               CLEAN_DIRECT_OUTPUT 1)
if(WIN32)
    set_target_properties(${WRAP_TARGET_NAME} PROPERTIES SUFFIX ".pyd")
else()
    set_target_properties(${WRAP_TARGET_NAME} PROPERTIES SUFFIX ".so")
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/__init__.py.in" "${CMAKE_CURRENT_BINARY_DIR}/__init__.py" @ONLY)
install(
    TARGETS ${WRAP_TARGET_NAME}
    RUNTIME DESTINATION "${DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT}/opendcc/${PYMODULE_NAME}"
    LIBRARY DESTINATION "${DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT}/opendcc/${PYMODULE_NAME}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/__init__.py"
        DESTINATION "${DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT}/opendcc/${PYMODULE_NAME}")

make_folder("base/commands_api" ${TARGET_NAME} ${WRAP_TARGET_NAME})
