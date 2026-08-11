# plugInfo bases name TfTypes, which USD 25.08 renamed. Set before add_subdirectory so ndrCycles inherits.
if(USD_PXR_VERSION GREATER_EQUAL 2508)
    set(SDR_DISCOVERY_PLUGIN_BASE "SdrDiscoveryPlugin")
    set(SDR_PARSER_PLUGIN_BASE "SdrParserPlugin")
else()
    set(SDR_DISCOVERY_PLUGIN_BASE "NdrDiscoveryPlugin")
    set(SDR_PARSER_PLUGIN_BASE "NdrParserPlugin")
endif()

add_subdirectory(ndrCycles)

set(PROJECT_NAME cycles_property_proxy)

set(HEADERS cycles_property_factory.h)

set(SRC cycles_property_factory.cpp)

add_library(${PROJECT_NAME} SHARED ${HEADERS} ${SRC})

include(${USD_CONFIG_FILE})
link_directories(${USD_LIBRARY_DIR})
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/src/lib/ ${USD_FALLBACK_PROXY_INCLUDE_DIR})

target_link_libraries(${PROJECT_NAME} usd_fallback_proxy usdShade ndrCycles)

install(
    TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION plugin/cycles
    LIBRARY DESTINATION plugin/cycles
    ARCHIVE DESTINATION plugin/cycles)
file(RELATIVE_PATH LIBRARY_LOCATION ${CMAKE_INSTALL_PREFIX}/plugin/cycles/${PROJECT_NAME}/resources/..
     ${CMAKE_INSTALL_PREFIX}/plugin/cycles)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.json.in" "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json" @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json
        DESTINATION ${CMAKE_INSTALL_PREFIX}/plugin/cycles/${PROJECT_NAME}/resources/)
if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${PROJECT_NAME}>
        DESTINATION plugin/cycles
        OPTIONAL)
endif()
