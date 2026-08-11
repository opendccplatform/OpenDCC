set(PROJECT_NAME ndrCycles)

set(HEADERS api.h discovery.h node_definitions.h parser.h sdr_compat.h)

set(SRC discovery.cpp node_definitions.cpp parser.cpp)

add_library(${PROJECT_NAME} SHARED ${HEADERS} ${SRC})

include(${USD_CONFIG_FILE})
link_directories(${USD_LIBRARY_DIR})
target_include_directories(
    ${PROJECT_NAME}
    PUBLIC ${PROJECT_SOURCE_DIR}/src/lib/
           ${USD_FALLBACK_PROXY_INCLUDE_DIR}
           ${TBB_INCLUDE_DIRS}
           ${Boost_INCLUDE_DIR}
           ${PYTHON_INCLUDE_DIR}
           ${OIIO_INCLUDE_DIR}
           ${ILMBASE_INCLUDE_DIR}
           ${ILMBASE_INCLUDE_DIR}/OpenEXR
           ${OPENSUBDIV_INCLUDE_DIR}
           ${OCIO_INCLUDE_DIR})

target_link_libraries(
    ${PROJECT_NAME}
    usd
    sdr
    usdShade
    Cycles::cycles
    ${OIIO_OpenImageIO_LIBRARY}
    ${OIIO_OpenImageIO_Util_LIBRARY}
    ${ILMBASE_LIBRARIES}
    ${EMBREE_LIBRARY}
    ${OSL_oslexec_LIBRARY}
    ${OPENSUBDIV_LIBRARIES}
    ${OCIO_LIBRARY}
    ${ALEMBIC_LIBRARIES})
# 'ndr' was merged into 'sdr' in USD 25.05 and no longer exists there.
if(TARGET ndr)
    target_link_libraries(${PROJECT_NAME} ndr)
endif()
target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE -DNDR_CYCLES_EXPORT
    PUBLIC -DCCL_NAMESPACE_BEGIN=namespace\ ccl\ { -DCCL_NAMESPACE_END=})

install(
    TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION plugin/cycles
    LIBRARY DESTINATION plugin/cycles
    ARCHIVE DESTINATION plugin/cycles)
file(RELATIVE_PATH LIBRARY_LOCATION ${CMAKE_INSTALL_PREFIX}/plugin/cycles/${PROJECT_NAME}/resources/..
     ${CMAKE_INSTALL_PREFIX}/plugin/cycles)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.json.in" "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json" @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json ${CMAKE_CURRENT_SOURCE_DIR}/node_definitions.usda
        DESTINATION ${CMAKE_INSTALL_PREFIX}/plugin/cycles/${PROJECT_NAME}/resources/)
if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${PROJECT_NAME}>
        DESTINATION plugin/cycles
        OPTIONAL)
endif()
