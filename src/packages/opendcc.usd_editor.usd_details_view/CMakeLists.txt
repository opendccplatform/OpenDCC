set(PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/packages/opendcc.usd_editor.usd_details_view/)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python"
    DESTINATION "${PLUGIN_DIR}"
    FILES_MATCHING
    PATTERN "*.py")
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/package.toml DESTINATION ${PLUGIN_DIR})
install(FILES ${PROJECT_SOURCE_DIR}/THIRD_PARTY_LICENSES.txt ${PROJECT_SOURCE_DIR}/NOTICE.txt DESTINATION .)

if(DCC_BUILD_TESTS)
    opendcc_add_python_test(UsdDetailsViewTests SOURCE tests/test_details.py GUI)
endif()
