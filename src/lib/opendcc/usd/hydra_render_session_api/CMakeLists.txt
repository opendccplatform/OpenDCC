set(OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_src/hydra_render_session_api")
set(SCHEMA_SOURCES_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_src")
set(SCHEMA_FILE "${CMAKE_CURRENT_SOURCE_DIR}/schema.usda")

execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR} RESULT_VARIABLE ret)

if(ret EQUAL "1")
    message(FATAL_ERROR)
endif()

set(_USD_DEPS_ENV "")
list(APPEND _USD_DEPS_ENV "${USD_LIBRARY_DIR}")
list(APPEND _USD_DEPS_ENV "${Boost_LIBRARY_DIR_RELEASE}")
# Keep the schema generator runtime paths in sync with get_usd_env().
list(APPEND _USD_DEPS_ENV "${TBB_INCLUDE_DIRS}/../lib")
list(APPEND _USD_DEPS_ENV "${TBB_INCLUDE_DIRS}/../bin")
list(APPEND _USD_DEPS_ENV "${USD_GENSCHEMA_DIR}")

if(WIN32)
    list(APPEND _USD_DEPS_ENV "$ENV{PATH}")
    string(JOIN ";" USD_DEPS_ENV ${_USD_DEPS_ENV})
else()
    list(APPEND _USD_DEPS_ENV "$ENV{LD_LIBRARY_PATH}")
    list(APPEND _USD_DEPS_ENV "$ENV{PATH}")
    string(JOIN ":" USD_DEPS_ENV ${_USD_DEPS_ENV})
endif()

if(WIN32)
    set(OS_ENV_SEPARATOR ";")
elseif(APPLE)
    set(OS_ENV_SEPARATOR ":")
else()
    set(OS_ENV_SEPARATOR ":")
endif()

set(GEN_HEADERS ${OUTPUT_DIR}/api.h ${OUTPUT_DIR}/renderSessionAPI.h ${OUTPUT_DIR}/tokens.h)

set(GEN_SRC ${OUTPUT_DIR}/renderSessionAPI.cpp ${OUTPUT_DIR}/tokens.cpp)

set(GEN_SRC_PY ${OUTPUT_DIR}/wrapRenderSessionAPI.cpp ${OUTPUT_DIR}/wrapTokens.cpp)

file(RELATIVE_PATH LIBRARY_LOCATION ${CMAKE_INSTALL_PREFIX}/plugin/usd/hydra_render_session_api
     ${CMAKE_INSTALL_PREFIX}/bin)

set(PLUG_INFO_ROOT ..)
set(PLUG_INFO_RESOURCE_PATH resources)
set(PLUG_INFO_LIBRARY_PATH
    "${LIBRARY_LOCATION}/${CMAKE_SHARED_LIBRARY_PREFIX}hydra_render_session_api${CMAKE_SHARED_LIBRARY_SUFFIX}")

file(
    GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/configure_plugInfo.cmake"
    CONTENT
        "configure_file(\"${OUTPUT_DIR}/plugInfo.json.in\" \"${CMAKE_CURRENT_BINARY_DIR}/plugin/usd/hydra_render_session_api/resources/plugInfo.json\" @ONLY)"
)

add_custom_command(
    OUTPUT ${OUTPUT_DIR}/plugInfo.json ${OUTPUT_DIR}/generatedSchema.usda
           "${CMAKE_CURRENT_BINARY_DIR}/plugin/usd/hydra_render_session_api/resources/plugInfo.json" ${GEN_HEADERS}
           ${GEN_SRC} ${GEN_SRC_PY}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/schema.usda"
    COMMAND
        ${CMAKE_COMMAND} -E env "PYTHONPATH=\"${USD_LIBRARY_DIR}/python\"${OS_ENV_SEPARATOR}$ENV{PYTHONPATH}"
        "LD_LIBRARY_PATH=${USD_DEPS_ENV}${OS_ENV_SEPARATOR}" "PATH=${USD_DEPS_ENV}${OS_ENV_SEPARATOR}"
        "PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:hydra_render_session_api>" ${USD_GENSCHEMA} "${SCHEMA_FILE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUTPUT_DIR}/plugInfo.json ${OUTPUT_DIR}/plugInfo.json.in
    COMMAND ${CMAKE_COMMAND} -DPLUG_INFO_ROOT=${PLUG_INFO_ROOT} -DPLUG_INFO_RESOURCE_PATH=${PLUG_INFO_RESOURCE_PATH}
            -DPLUG_INFO_LIBRARY_PATH=${PLUG_INFO_LIBRARY_PATH} -P "${CMAKE_CURRENT_BINARY_DIR}/configure_plugInfo.cmake"
    WORKING_DIRECTORY "${OUTPUT_DIR}")

set(headers ${GEN_HEADERS})

set(src ${GEN_SRC})

set(src_py module.cpp ${GEN_SRC_PY})

opendcc_make_library(
    hydra_render_session_api
    PUBLIC_HEADERS
    ${headers}
    CPPFILES
    ${src}
    INCLUDE_DIRS
    ${Boost_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
    ${USD_INCLUDE_DIR}
    ${TBB_INCLUDE_DIRS}
    $<BUILD_INTERFACE:${SCHEMA_SOURCES_INCLUDE_DIR}>
    LIBRARY_DEPENDENCIES
    ${TBB_LIBRARIES}
    ${Boost_PYTHON_LIBRARY}
    ${PYTHON_LIBRARY}
    usd
    usdGeom
    tf
    sdf
    vt
    PRIVATE_DEFINITIONS
    USDHYDRAEXT_EXPORTS
    LIBRARY_INSTALL_DIR
    plugin/usd
    ARCHIVE_INSTALL_DIR
    plugin/usd
    PDB_INSTALL_DIR
    plugin/usd)

opendcc_make_python_module(
    hydra_render_session_api_py
    PYMODULE_NAME
    UsdHdSession
    PYMODULE_SOURCES
    ${headers}
    ${src_py}
    LIBRARY_DEPENDENCIES
    hydra_render_session_api
    PRIVATE_DEFINITIONS
    MFB_PACKAGE_NAME=UsdHdSession
    MFB_ALT_PACKAGE_NAME=UsdHdSession
    MFB_PACKAGE_MODULE=UsdHdSession
    PYMODULE_INSTALL_DIR
    opendcc/UsdHdSession)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plugin/usd/hydra_render_session_api/resources/plugInfo.json
        DESTINATION plugin/usd/hydra_render_session_api/resources)
install(FILES ${headers} DESTINATION include/opendcc/hydra_render_session_api)
install(FILES "${OUTPUT_DIR}/generatedSchema.usda" DESTINATION plugin/usd/hydra_render_session_api/resources)
export(TARGETS hydra_render_session_api FILE hydra_render_session_api.cmake)
install(FILES schema.usda DESTINATION plugin/usd/hydra_render_session_api/resources/hydra_render_session_api)
install(FILES __init__.py DESTINATION "${DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT}/opendcc/UsdHdSession")
