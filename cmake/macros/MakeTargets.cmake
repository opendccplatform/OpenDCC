if(WIN32)
    set(OS_ENV_SEPARATOR ";")
    set(OS_LIBRARY_ENV_NAME "PATH")
else()
    set(OS_ENV_SEPARATOR ":")
    set(OS_LIBRARY_ENV_NAME "LD_LIBRARY_PATH")
endif()

function(get_usd_env EXTRA_ENVIRONMENT USD_ENVIRONMENT_VAR)
    set(_result "")
    list(APPEND _result "${USD_LIBRARY_DIR}")
    list(APPEND _result "${Boost_LIBRARY_DIR_RELEASE}")
    list(APPEND _result "${TBB_INCLUDE_DIRS}/../lib")
    list(APPEND _result "${USD_GENSCHEMA_DIR}")
    list(APPEND _result "${EXTRA_ENVIRONMENT}") # ????
    if(WIN32)
        list(APPEND _result "$ENV{PATH}")
    else()
        list(APPEND _result "$ENV{LD_LIBRARY_PATH}")
        list(APPEND _result "$ENV{PATH}")
    endif()
    string(JOIN "${OS_ENV_SEPARATOR}" _fixed_paths ${_result})

    set(${USD_ENVIRONMENT_VAR}
        "${_fixed_paths}"
        PARENT_SCOPE)
endfunction()

function(
    parse_schema_file
    SCHEMA_FILE
    USD_ENV
    OUTPUT_DIR
    GEN_HEADERS
    GEN_SRC
    GEN_SRC_PY
    SCHEMA_NAME
    SCHEMA_PREFIX
    HAVE_CLASSES)
    execute_process(
        COMMAND
            ${CMAKE_COMMAND} -E env "PYTHONPATH=${USD_LIBRARY_DIR}/python${OS_ENV_SEPARATOR}$ENV{PYTHONPATH}"
            "LD_LIBRARY_PATH=${USD_ENV}${OS_ENV_SEPARATOR}" "PATH=${USD_ENV}${OS_ENV_SEPARATOR}" "${PYTHON_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/cmake/macros/parse_usd_schema.py" "${USD_GENSCHEMA_SCRIPT}" "${SCHEMA_FILE}"
            "${OUTPUT_DIR}"
        RESULT_VARIABLE _exit_code
        OUTPUT_VARIABLE result
        ERROR_VARIABLE err_result ECHO_ERROR_VARIABLE)

    if(NOT _exit_code EQUAL "0")
        message(FATAL_ERROR "Failed to parse USD schema. ${_exit_code} returned.\n" "${result}")
    endif()
    string(REPLACE "\n" ";" result ${result})

    list(GET result 0 _schema_name)
    list(GET result 1 _schema_prefix)

    list(GET result 2 _gen_headers)
    string(REPLACE "," ";" _gen_headers ${_gen_headers})
    list(GET result 3 _gen_src)
    string(REPLACE "," ";" _gen_src ${_gen_src})
    list(GET result 4 _gen_src_py)
    string(REPLACE "," ";" _gen_src_py ${_gen_src_py})
    list(GET result 5 _have_classes)

    set(${SCHEMA_NAME}
        ${_schema_name}
        PARENT_SCOPE)
    set(${SCHEMA_PREFIX}
        ${_schema_prefix}
        PARENT_SCOPE)
    set(${GEN_HEADERS}
        ${_gen_headers}
        PARENT_SCOPE)
    set(${GEN_SRC}
        ${_gen_src}
        PARENT_SCOPE)
    set(${GEN_SRC_PY}
        ${_gen_src_py}
        PARENT_SCOPE)
    set(${HAVE_CLASSES}
        ${_have_classes}
        PARENT_SCOPE)
endfunction()

function(opendcc_make_python_module TARGET_NAME)
    set(options "")
    set(one_value_args PYMODULE_NAME VS_FOLDER PYMODULE_INSTALL_DIR PYMODULE_INSTALL_PATH)
    set(multi_value_args PYMODULE_SOURCES LIBRARY_DEPENDENCIES PUBLIC_DEFINITIONS PRIVATE_DEFINITIONS)

    cmake_parse_arguments(args "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})
    if(NOT args_PYMODULE_SOURCES OR NOT args_PYMODULE_NAME)
        return()
    endif()

    set(_pymodule_target ${TARGET_NAME})
    add_library(${_pymodule_target} SHARED ${args_PYMODULE_SOURCES})

    if(args_VS_FOLDER)
        make_folder(${args_VS_FOLDER} ${_pymodule_target})
    endif()
    if(MSVC)
        set(_pymodule_suffix ".pyd")
    else()
        set(_pymodule_suffix ".so")
    endif()

    target_compile_definitions(${_pymodule_target} PRIVATE PYMODULE_NAME=${args_PYMODULE_NAME})
    target_link_libraries(${_pymodule_target} PUBLIC pybind11::pybind11 ${Boost_PYTHON_LIBRARY} ${PYTHON_LIBRARY})
    target_link_libraries(${TARGET_NAME} PUBLIC ${args_LIBRARY_DEPENDENCIES})

    if(args_PUBLIC_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} PUBLIC ${args_PUBLIC_DEFINITIONS})
    endif()
    if(args_PRIVATE_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} PRIVATE ${args_PRIVATE_DEFINITIONS})
    endif()
    set_target_properties(
        ${_pymodule_target}
        PROPERTIES PREFIX ""
                   SUFFIX ${_pymodule_suffix}
                   OUTPUT_NAME ${args_PYMODULE_NAME}
                   ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_pymodule_target}"
                   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_pymodule_target}"
                   LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_pymodule_target}"
                   PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_pymodule_target}"
                   CLEAN_DIRECT_OUTPUT 1)

    if(args_PYMODULE_INSTALL_PATH)
        install(FILES "$<TARGET_FILE:${_pymodule_target}>"
                DESTINATION "${CMAKE_INSTALL_PREFIX}/${args_PYMODULE_INSTALL_PATH}")

        if(MSVC)
            install(
                FILES "$<TARGET_PDB_FILE:${_pymodule_target}>"
                DESTINATION "${CMAKE_INSTALL_PREFIX}/${args_PYMODULE_INSTALL_PATH}"
                OPTIONAL)
        endif()

    else()
        if(NOT args_PYMODULE_INSTALL_DIR)
            message(
                WARNING
                    "PYMODULE_INSTALL_DIR for target ${TARGET_NAME} is not set. Default directory 'opendcc' will be used."
            )
            set(args_PYMODULE_INSTALL_DIR opendcc)
        endif()

        install(FILES "$<TARGET_FILE:${_pymodule_target}>"
                DESTINATION "${DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT}/${args_PYMODULE_INSTALL_DIR}")

        if(MSVC)
            install(
                FILES "$<TARGET_PDB_FILE:${_pymodule_target}>"
                DESTINATION "${DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT}/${args_PYMODULE_INSTALL_DIR}"
                OPTIONAL)
        endif()
    endif()
endfunction()

function(opendcc_make_library TARGET_NAME)
    set(options QT_AUTOMOC)
    set(one_value_args
        TYPE # static shared plugin
        VS_FOLDER
        PLUGIN_DIR # deprecated: use PACKAGE_DIR instead
        PACKAGE_DIR
        PYMODULE_NAME
        PYMODULE_INSTALL_DIR
        PYMODULE_INSTALL_PATH
        RESOURCES_DIR)

    set(multi_value_args
        PUBLIC_HEADERS
        PRIVATE_HEADERS
        CPPFILES
        LIBRARY_DEPENDENCIES
        INCLUDE_DIRS
        PUBLIC_DEFINITIONS
        PRIVATE_DEFINITIONS
        RESOURCES
        PYMODULE_SOURCES)

    cmake_parse_arguments(args "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT args_TYPE)
        set(args_TYPE "SHARED")
    endif()

    string(TOLOWER ${args_TYPE} args_TYPE)

    if(${args_TYPE} STREQUAL "plugin")
        set(_library_type "SHARED")
    else()
        string(TOUPPER ${args_TYPE} _library_type)
    endif()

    add_library(${TARGET_NAME} ${_library_type} ${args_PUBLIC_HEADERS} ${args_PRIVATE_HEADERS} ${args_CPPFILES})

    if(args_PUBLIC_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} PUBLIC ${args_PUBLIC_DEFINITIONS})
    endif()
    if(args_PRIVATE_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} PRIVATE ${args_PRIVATE_DEFINITIONS})
    endif()

    if(args_QT_AUTOMOC)
        set_property(TARGET ${TARGET_NAME} PROPERTY AUTOMOC ON)
        # Windows MAX_PATH: the default autogen path runs past 260 chars in a deep tree and cl.exe
        # then cannot open moc_<header>.cpp. Keep it short rather than depend on the checkout depth.
        set_property(TARGET ${TARGET_NAME} PROPERTY AUTOGEN_BUILD_DIR "${CMAKE_BINARY_DIR}/ag/${TARGET_NAME}")
    endif()

    target_include_directories(${TARGET_NAME} PUBLIC ${args_INCLUDE_DIRS})

    target_link_libraries(${TARGET_NAME} PUBLIC ${args_LIBRARY_DEPENDENCIES})

    if(args_VS_FOLDER)
        make_folder(${args_VS_FOLDER} ${TARGET_NAME})
    endif()

    # install configuration
    if(((${args_TYPE} STREQUAL "static") OR (${args_TYPE} STREQUAL "shared")) AND NOT DEFINED args_PACKAGE_DIR)
        set(_runtime_destination bin)
        set(_library_destination lib)
        set(_archive_destination lib)
        set(_plugin_dir plugin/opendcc)
    else()
        if(DEFINED args_PLUGIN_DIR)
            set(_runtime_destination plugin/${args_PLUGIN_DIR})
            set(_library_destination plugin/${args_PLUGIN_DIR})
            set(_archive_destination plugin/${args_PLUGIN_DIR})
            set(_plugin_dir plugin/${args_PLUGIN_DIR})
        elseif(DEFINED args_PACKAGE_DIR)
            set(_runtime_destination packages/${args_PACKAGE_DIR})
            set(_library_destination packages/${args_PACKAGE_DIR})
            set(_archive_destination packages/${args_PACKAGE_DIR})
            set(_plugin_dir packages/${args_PACKAGE_DIR})
        else()
            message(FATAL_ERROR "args_PLUGIN_DIR nor args_PACKAGE_DIR is not set.")
        endif()
    endif()

    install(
        TARGETS ${TARGET_NAME}
        EXPORT ${TARGET_NAME}-targets
        RUNTIME DESTINATION ${_runtime_destination}
        LIBRARY DESTINATION ${_library_destination}
        ARCHIVE DESTINATION ${_archive_destination})
    if(MSVC)
        if(${_library_type} STREQUAL "SHARED")
            install(
                FILES "$<TARGET_PDB_FILE:${TARGET_NAME}>"
                DESTINATION ${_runtime_destination}
                OPTIONAL)
        else()
            install(
                FILES "$<TARGET_FILE_DIR:${TARGET_NAME}>/$<TARGET_FILE_NAME:${TARGET_NAME}>.pdb"
                DESTINATION ${_library_destination}
                OPTIONAL)
        endif()
    endif()

    # resource install
    set(_target_install_plugin_dir "${CMAKE_INSTALL_PREFIX}/${_plugin_dir}/${TARGET_NAME}")

    if(WIN32)
        set(_binary_install_dir "${CMAKE_INSTALL_PREFIX}/${_runtime_destination}")
    else()
        set(_binary_install_dir "${CMAKE_INSTALL_PREFIX}/${_library_destination}")
    endif()
    set(_resources_install_path "${_target_install_plugin_dir}")
    if(DEFINED args_RESOURCES_DIR)
        set(_resources_install_path "${CMAKE_INSTALL_PREFIX}/${args_RESOURCES_DIR}")
    endif()
    foreach(_resource_file ${args_RESOURCES})
        get_filename_component(_resource_dir ${_resource_file} PATH)
        get_filename_component(_resource_name ${_resource_file} NAME)

        if(${_resource_name} STREQUAL "plugInfo.json")
            file(RELATIVE_PATH LIBRARY_LOCATION "${_resources_install_path}/.." "${_binary_install_dir}")
            configure_file(${_resource_file} "${CMAKE_CURRENT_BINARY_DIR}/${_resource_file}" @ONLY)
            set(_resource_file "${CMAKE_CURRENT_BINARY_DIR}/${_resource_file}")
        endif()
        install(FILES ${_resource_file} DESTINATION "${_resources_install_path}")
    endforeach()
    if(${_library_type} STREQUAL "SHARED")
        opendcc_make_python_module(
            ${TARGET_NAME}_py
            PYMODULE_NAME
            ${args_PYMODULE_NAME}
            PYMODULE_SOURCES
            ${args_PYMODULE_SOURCES}
            PYMODULE_INSTALL_DIR
            "${args_PYMODULE_INSTALL_DIR}"
            PYMODULE_INSTALL_PATH
            "${args_PYMODULE_INSTALL_PATH}"
            VS_FOLDER
            ${args_VS_FOLDER}
            LIBRARY_DEPENDENCIES
            ${TARGET_NAME})
    endif()
endfunction()

function(opendcc_make_usd_schema TARGET_NAME)
    set(options "")
    set(one_value_args
        VS_FOLDER
        PLUGIN_DIR # deprecated: use PACKAGE_DIR instead
        PACKAGE_DIR
        CUSTOM_RUNTIME_DIR
        SCHEMA_FILE # default schema.usda
        HEADER_INSTALL_DIR # TODO: Reconsider this mechanism. Evaluate this variable based on source directory.
        SRC_GENERATION_DIR # TODO: Reconsider this mechanism. Evaluate this variable based on source directory.
        PYTHON_INSTALL_DIR)
    set(multi_value_args
        PYMODULE_CPPFILES
        EXTRA_USD_ENVIRONMENT
        PUBLIC_HEADERS
        PRIVATE_HEADERS
        CPPFILES
        AFTER_GEN_SCRIPT
        INCLUDE_DIRS
        SCHEMA_DEPENDENCIES
        LIBRARY_DEPENDENCIES
        PUBLIC_DEFINITIONS
        PRIVATE_DEFINITIONS
        PYTHON_FILES)

    cmake_parse_arguments(args "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT args_SCHEMA_FILE)
        set(args_SCHEMA_FILE "schema.usda")
    endif()

    if(args_SRC_GENERATION_DIR)
        set(_output_dir_name "${args_SRC_GENERATION_DIR}")
    else()
        get_filename_component(_output_dir_name "${CMAKE_CURRENT_BINARY_DIR}" NAME)
    endif()
    set(_output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_src/${_output_dir_name}")
    set(_schema_sources_include_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_src")
    set(_schema_file "${CMAKE_CURRENT_SOURCE_DIR}/${args_SCHEMA_FILE}")

    if(DEFINED args_PLUGIN_DIR)
        set(_schema_install_dir "plugin/${args_PLUGIN_DIR}")
    elseif(DEFINED args_PACKAGE_DIR)
        set(_schema_install_dir "packages/${args_PACKAGE_DIR}")
    else()
        message(FATAL_ERROR "args_PLUGIN_DIR nor args_PACKAGE_DIR is not set.")
    endif()
    get_usd_env(${args_EXTRA_USD_ENVIRONMENT} "" _usd_env)

    parse_schema_file(
        ${_schema_file}
        "${_usd_env}"
        ${_output_dir}
        _gen_headers
        _gen_src
        _gen_src_py
        _schema_name
        _schema_prefix
        _have_classes)

    file(
        GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/configure_plugInfo.cmake"
        CONTENT
            "configure_file(\"${_output_dir}/plugInfo.json.in\" \"${CMAKE_CURRENT_BINARY_DIR}/${_schema_install_dir}/${_schema_name}/resources/plugInfo.json\" @ONLY)"
    )

    # usdGenSchema currently doesn't generate plugInfo.json file if the schema.usda doesn't have classes defined as
    # workaround, we call the python script that generates it for such cases
    if(NOT _have_classes)
        set(_run_pre_gen_script "${PYTHON_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/macros/generate_empty_plug_info.py"
                                "${_schema_name}" "${_output_dir}/plugInfo.json")
    else()
        set(_run_pre_gen_script "${CMAKE_COMMAND}" -E true)
    endif()

    if(args_AFTER_GEN_SCRIPT)
        list(GET args_AFTER_GEN_SCRIPT 0 _after_gen_script_path)
        list(SUBLIST args_AFTER_GEN_SCRIPT 1 -1 _after_gen_script_args)
        set(_run_after_gen_script "${PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/${_after_gen_script_path}"
                                  ${_after_gen_script_args})
    else()
        set(_run_after_gen_script "${CMAKE_COMMAND}" -E true)
    endif()

    if(DCC_KATANA_SUPPORT)
        set(_pxr_path_name FNPXR_PLUGINPATH)
    else()
        set(_pxr_path_name PXR_PLUGINPATH_NAME)
    endif()

    if(args_SCHEMA_DEPENDENCIES)
        set(_pxr_deps "")
        foreach(_target ${args_SCHEMA_DEPENDENCIES})
            list(APPEND _pxr_deps "$<TARGET_FILE_DIR:${_target}>")
        endforeach()
    endif()

    set(_target_install_plugin_dir "${CMAKE_INSTALL_PREFIX}/${_schema_install_dir}/${_schema_name}")

    if(args_CUSTOM_RUNTIME_DIR)
        set(_runtime_dir ${args_CUSTOM_RUNTIME_DIR})
    else()
        set(_runtime_dir ${_schema_install_dir})
    endif()

    file(RELATIVE_PATH LIBRARY_LOCATION "${_target_install_plugin_dir}" "${CMAKE_INSTALL_PREFIX}/${_runtime_dir}")
    set(PLUG_INFO_ROOT ..)
    set(PLUG_INFO_RESOURCE_PATH resources)
    set(PLUG_INFO_LIBRARY_PATH
        "${LIBRARY_LOCATION}/${CMAKE_SHARED_LIBRARY_PREFIX}${TARGET_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")

    add_custom_command(
        OUTPUT "${_output_dir}/plugInfo.json" "${_output_dir}/generatedSchema.usda"
               "${CMAKE_CURRENT_BINARY_DIR}/${_schema_install_dir}/${_schema_name}/resources/plugInfo.json"
               ${_gen_headers} ${_gen_src} ${_gen_src_py}
        DEPENDS ${_schema_file} "${_after_gen_script_path}" ${args_SCHEMA_DEPENDENCIES}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_output_dir}"
        COMMAND
            ${CMAKE_COMMAND} -E env "PYTHONPATH=${USD_LIBRARY_DIR}/python${OS_ENV_SEPARATOR}$ENV{PYTHONPATH}"
            "LD_LIBRARY_PATH=${_usd_env}${OS_ENV_SEPARATOR}" "PATH=${_usd_env}${OS_ENV_SEPARATOR}"
            "${_pxr_path_name}=${_pxr_deps}" ${_run_pre_gen_script}
        COMMAND
            ${CMAKE_COMMAND} -E env "PYTHONPATH=${USD_LIBRARY_DIR}/python${OS_ENV_SEPARATOR}$ENV{PYTHONPATH}"
            "LD_LIBRARY_PATH=${_usd_env}${OS_ENV_SEPARATOR}" "PATH=${_usd_env}${OS_ENV_SEPARATOR}"
            "${_pxr_path_name}=${_pxr_deps}" ${USD_GENSCHEMA} "${_schema_file}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_output_dir}/plugInfo.json ${_output_dir}/plugInfo.json.in
        COMMAND
            ${CMAKE_COMMAND} -E env "PYTHONPATH=${USD_LIBRARY_DIR}/python${OS_ENV_SEPARATOR}$ENV{PYTHONPATH}"
            "LD_LIBRARY_PATH=${_usd_env}${OS_ENV_SEPARATOR}" "PATH=${_usd_env}${OS_ENV_SEPARATOR}"
            "${_pxr_path_name}=${_pxr_deps}" ${_run_after_gen_script}
        COMMAND
            ${CMAKE_COMMAND} -DPLUG_INFO_ROOT=${PLUG_INFO_ROOT} -DPLUG_INFO_RESOURCE_PATH=${PLUG_INFO_RESOURCE_PATH}
            -DPLUG_INFO_LIBRARY_PATH=${PLUG_INFO_LIBRARY_PATH} -P "${CMAKE_CURRENT_BINARY_DIR}/configure_plugInfo.cmake"
        WORKING_DIRECTORY "${_output_dir}")

    set(TARGET_NAME_PY ${TARGET_NAME}_py)

    set(_public_headers ${_gen_headers} ${args_PUBLIC_HEADERS})
    set(_src ${_gen_src} ${args_CPPFILES})
    set(_src_py ${_gen_src_py} ${args_PYMODULE_CPPFILES})
    add_library(${TARGET_NAME} SHARED ${_public_headers} ${args_PRIVATE_HEADERS} ${_src})

    add_library(${TARGET_NAME_PY} SHARED ${_public_headers} ${args_PRIVATE_HEADERS} ${_src_py})

    set_target_properties(${TARGET_NAME} PROPERTIES PUBLIC_HEADER "${_public_headers}")

    target_include_directories(
        ${TARGET_NAME}
        PUBLIC ${Boost_INCLUDE_DIRS}
               ${PYTHON_INCLUDE_DIRS}
               ${USD_INCLUDE_DIR}
               ${TBB_INCLUDE_DIR}
               ${args_INCLUDE_DIRS}
               $<BUILD_INTERFACE:${_schema_sources_include_dir}>
               $<BUILD_INTERFACE:${_output_dir}>)

    target_link_libraries(
        ${TARGET_NAME}
        ${TBB_LIBRARIES}
        ${Boost_PYTHON_LIBRARY}
        ${PYTHON_LIBRARY}
        ${args_LIBRARY_DEPENDENCIES}
        usd
        usdGeom)

    if(args_VS_FOLDER)
        make_folder(${args_VS_FOLDER} ${TARGET_NAME} ${TARGET_NAME_PY})
    endif()

    string(TOUPPER ${_schema_name} _export_define)
    target_compile_definitions(${TARGET_NAME} PRIVATE ${args_PRIVATE_DEFINITIONS} ${_export_define}_EXPORTS)
    if(args_PUBLIC_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} PUBLIC ${args_PUBLIC_DEFINITIONS})
    endif()

    target_link_libraries(${TARGET_NAME_PY} ${TARGET_NAME})

    # configure another plugInfo for other targets that might use this plugin during their config
    set(PLUG_INFO_ROOT .)
    set(PLUG_INFO_RESOURCE_PATH resources)
    set(PLUG_INFO_LIBRARY_PATH "./${CMAKE_SHARED_LIBRARY_PREFIX}${TARGET_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")
    file(
        GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/configure_internal_plugInfo.cmake"
        CONTENT
            "configure_file(\"${_output_dir}/plugInfo.json.in\" \"${CMAKE_CURRENT_BINARY_DIR}/plugInfo_internal.json\" @ONLY)"
    )

    add_custom_command(
        TARGET ${TARGET_NAME}
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -DPLUG_INFO_ROOT=${PLUG_INFO_ROOT} -DPLUG_INFO_RESOURCE_PATH=${PLUG_INFO_RESOURCE_PATH}
            -DPLUG_INFO_LIBRARY_PATH=${PLUG_INFO_LIBRARY_PATH} -P
            "${CMAKE_CURRENT_BINARY_DIR}/configure_internal_plugInfo.cmake"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/plugInfo_internal.json"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/plugInfo.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_schema_file}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/resources/${_schema_name}/schema.usda")

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${_schema_install_dir}/${_schema_name}/resources/plugInfo.json"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/${_schema_install_dir}/${_schema_name}/resources/")
    install(FILES "${_output_dir}/generatedSchema.usda" DESTINATION "${_schema_install_dir}/${_schema_name}/resources/")
    export(TARGETS ${TARGET_NAME} FILE ${TARGET_NAME}.cmake)
    install(FILES ${_schema_file} DESTINATION ${_schema_install_dir}/${_schema_name}/resources/${_schema_name})
    install(
        TARGETS ${TARGET_NAME}
        EXPORT ${TARGET_NAME}-targets
        ARCHIVE DESTINATION ${_runtime_dir}
        LIBRARY DESTINATION ${_runtime_dir}
        RUNTIME DESTINATION ${_runtime_dir}
        PUBLIC_HEADER DESTINATION ${args_HEADER_INSTALL_DIR})

    if(args_PYTHON_INSTALL_DIR)
        set(_python_install_dir "${args_PYTHON_INSTALL_DIR}")
    else()
        set(_python_install_dir "${DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT}")
    endif()

    install(
        EXPORT ${TARGET_NAME}-targets
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/${TARGET_NAME}"
        NAMESPACE opendcc::)
    if(MSVC)
        install(
            FILES "$<TARGET_PDB_FILE:${TARGET_NAME}>"
            DESTINATION ${_runtime_dir}
            OPTIONAL)
        install(
            FILES "$<TARGET_PDB_FILE:${TARGET_NAME_PY}>"
            DESTINATION "${_python_install_dir}/${_schema_prefix}"
            OPTIONAL)
    endif()

    if(MSVC)
        set(_pymodule_suffix ".pyd")
    else()
        set(_pymodule_suffix ".so")
    endif()
    set_target_properties(
        ${TARGET_NAME_PY}
        PROPERTIES PREFIX ""
                   OUTPUT_NAME _${_schema_name}
                   SUFFIX ${_pymodule_suffix})
    target_compile_definitions(
        ${TARGET_NAME_PY} PRIVATE MFB_PACKAGE_NAME=${_schema_name} MFB_ALT_PACKAGE_NAME=${_schema_name}
                                  MFB_PACKAGE_MODULE=_${_schema_name})
    install(
        TARGETS ${TARGET_NAME_PY}
        LIBRARY DESTINATION "${_python_install_dir}/${_schema_prefix}"
        RUNTIME DESTINATION "${_python_install_dir}/${_schema_prefix}")
    if(args_PYTHON_FILES)
        # todo: check if __init__.py exists
        install(FILES ${args_PYTHON_FILES} DESTINATION "${_python_install_dir}/${_schema_prefix}")
    endif()

endfunction()

function(
    parse_pyside_typesystem
    TYPESYSTEM_FILE
    MODULE_NAME
    SRC_PATH
    HEADERS
    SOURCES
    TYPESYSTEMS)
    execute_process(
        COMMAND
            ${CMAKE_COMMAND} -E env "${PYTHON_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/macros/make_shiboken_target.py"
            parse_typesystem -t "${TYPESYSTEM_FILE}" -o "${CMAKE_CURRENT_BINARY_DIR}/wrap"
        RESULT_VARIABLE _exit_code
        OUTPUT_VARIABLE result
        ERROR_VARIABLE err_result ECHO_ERROR_VARIABLE)

    if(NOT _exit_code EQUAL "0")
        message(FATAL_ERROR "Failed to parse typesystem file. ${_exit_code} returned.\n" "${result}")
    endif()
    string(REPLACE "\n" ";" result ${result})

    list(GET result 0 _module_name)
    list(GET result 1 _src_path)
    list(GET result 2 _headers)
    string(REPLACE "," ";" _headers ${_headers})
    list(GET result 3 _sources)
    string(REPLACE "," ";" _sources ${_sources})
    list(GET result 4 _typesystems)
    string(REPLACE "," ";" _typesystems ${_typesystems})

    set(${MODULE_NAME}
        ${_module_name}
        PARENT_SCOPE)
    set(${SRC_PATH}
        ${_src_path}
        PARENT_SCOPE)
    set(${HEADERS}
        ${_headers}
        PARENT_SCOPE)
    set(${SOURCES}
        ${_sources}
        PARENT_SCOPE)
    set(${TYPESYSTEMS}
        ${_typesystems}
        PARENT_SCOPE)
endfunction()

function(opendcc_make_shiboken_bindings TARGET_NAME)
    set(options VERBOSE
                # QT_AUTOMOC
    )
    set(one_value_args TARGET SOURCE_DIR LEGACY_HEADER_FILEPATH CUSTOM_PROJECT_FILE VS_FOLDER PYTHON_INSTALL_DIR)
    set(multi_value_args GLUE_FILES LIBRARY_DEPENDENCIES INCLUDE_DIRS PUBLIC_DEFINITIONS PRIVATE_DEFINITIONS
                         GEN_CONFIG_INCLUDE_DIRS)

    cmake_parse_arguments(args "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})
    if(args_VERBOSE)
        message(STATUS "Generating sources for ${TARGET_NAME}:")
    endif()

    if(args_PYMODULE_INSTALL_DIR)
        set(_py_install_dir "${args_PYMODULE_INSTALL_DIR}")
    else()
        set(_py_install_dir opendcc)
    endif()

    # gather include paths for generation wrap files
    get_target_property(PYSIDE_INCLUDE_DIR ${DCC_PYSIDE_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    set(_gen_side_config_includes
        "${PROJECT_BINARY_DIR}/include"
        "${PROJECT_SOURCE_DIR}/src/lib"
        "${PYTHON_INCLUDE_DIR}"
        "${PYSIDE_INCLUDE_DIR}"
        "${DCC_QT_INCLUDE_DIRS}"
        "${PXR_INCLUDE_DIRS}"
        "${Boost_INCLUDE_DIRS}"
        "${TBB_INCLUDE_DIRS}"
        "${OIIO_INCLUDE_DIR}"
        "${pybind11_INCLUDE_DIRS}")

    if(args_GEN_CONFIG_INCLUDE_DIRS)
        foreach(include_dir ${args_GEN_CONFIG_INCLUDE_DIRS})
            list(APPEND _gen_side_config_includes ${include_dir})
        endforeach()
    endif()

    # find all dependent typesystems and generate list of source files
    set(_typesystem_filepath "${args_SOURCE_DIR}/${args_TARGET}.xml")
    if(args_LEGACY_HEADER_FILEPATH)
        set(_shiboken_header_filepath "${args_SOURCE_DIR}/${args_LEGACY_HEADER_FILEPATH}")
    else()
        set(_shiboken_header_filepath "${args_SOURCE_DIR}/${args_TARGET}.h")
    endif()
    parse_pyside_typesystem("${CMAKE_CURRENT_SOURCE_DIR}/${_typesystem_filepath}" _module_name _src_path _headers
                            _sources _dependent_typesystems)
    if(args_VERBOSE)
        message(STATUS "    Parsed ${_typesystem_filepath}:")
        message(STATUS "        Module name: ${_module_name}")
        message(STATUS "        Generated sources path: ${CMAKE_CURRENT_BINARY_DIR}/wrap/${_src_path}")
        message(STATUS "        Expected headers: ${_headers}")
        message(STATUS "        Expected sources: ${_sources}")
        message(STATUS "        Dependent typesystems: ${_dependent_typesystems}")
    endif()

    # gather dependent typesystem paths
    foreach(_ts ${_dependent_typesystems})
        get_filename_component(_target_name ${_ts} NAME_WLE)
        set(_target_name ${_target_name}_py)
        if(TARGET ${_target_name})
            get_target_property(_ts_path ${_target_name} PYSIDE_TYPESYSTEM_PATH)
            if(NOT _ts_path)
                message(
                    WARNING
                        "Typesystem file for target ${_target_name} not found. Shiboken generation error might happen.")
            else()
                get_target_property(_ts_sources_dir ${_target_name} PYSIDE_SOURCES_DIR)
                get_filename_component(_ts_directory "${_ts_path}" DIRECTORY)
                list(APPEND _typesystem_paths "${_ts_directory}")
                list(APPEND _gen_side_config_includes "${_ts_sources_dir}")
                list(APPEND _typesystem_includes "${_ts_sources_dir}")
                list(APPEND _dependent_pyside_modules ${target_name})
            endif()
        endif()
    endforeach()

    set(_output_dir "${CMAKE_CURRENT_BINARY_DIR}")

    # The typesystem differs between Qt5 and Qt6 (DCC_QT_SEQ_CONTAINER), so configure it into the
    # build tree and point shiboken and PYSIDE_TYPESYSTEM_PATH at that copy.
    set(_typesystem_source "${CMAKE_CURRENT_SOURCE_DIR}/${_typesystem_filepath}")
    get_filename_component(_typesystem_name "${_typesystem_filepath}" NAME)
    set(_typesystem_generated "${_output_dir}/${_typesystem_name}")
    configure_file("${_typesystem_source}" "${_typesystem_generated}" @ONLY)
    # generate side_config.txt.in with all dependent typesystem and includes
    if(args_CUSTOM_PROJECT_FILE)
        message(STATUS "Using custom project file ${args_CUSTOM_PROJECT_FILE}...")
        set(_side_config_file "${args_CUSTOM_PROJECT_FILE}")
    else()
        if(args_VERBOSE)
            set(_verbose "no-suppress-warnings")
        else()
            set(_verbose "silent")
        endif()

        if(_typesystem_paths)
            set(_typesystem_paths_arg "-t" ${_typesystem_paths})
        else()
            set(_typesystem_paths_arg "")
        endif()

        execute_process(
            COMMAND
                ${CMAKE_COMMAND} -E env "${PYTHON_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/cmake/macros/make_shiboken_target.py" make_side_config -o
                "${CMAKE_CURRENT_BINARY_DIR}" ${_verbose} "-i" ${_gen_side_config_includes} ${_typesystem_paths_arg}
            RESULT_VARIABLE _exit_code
            OUTPUT_VARIABLE result
            ERROR_VARIABLE err_result ECHO_ERROR_VARIABLE)

        if(NOT _exit_code EQUAL "0")
            message(FATAL_ERROR "Failed to generate side_config.txt.in file. ${_exit_code} returned.\n" "${result}")
        elseif(args_VERBOSE)
            message(STATUS ${result})
        endif()

        set(TYPESYSTEM_FILE_PATH "${_typesystem_generated}")
        set(HEADER_FILE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${_shiboken_header_filepath}")
        configure_file("${_output_dir}/side_config.txt.in" "${_output_dir}/side_config.txt" @ONLY)
        set(_side_config_file "${_output_dir}/side_config.txt")
    endif()

    set(_generator_dependencies "${args_GLUE_FILES}" "${CMAKE_CURRENT_SOURCE_DIR}/${_shiboken_header_filepath}"
                                "${CMAKE_CURRENT_SOURCE_DIR}/${_typesystem_filepath}")
    get_target_property(shiboken_bin ${DCC_SHIBOKEN_BIN_TARGET} LOCATION)
    if(WIN32)
        set(_qt_bin "${DCC_QT_INSTALL_PREFIX}/bin")
    else()
        set(_qt_bin "${DCC_QT_INSTALL_PREFIX}/lib")
    endif()
    add_custom_command(
        OUTPUT ${_headers} ${_sources}
        COMMAND
            ${CMAKE_COMMAND} -E env
            "${OS_LIBRARY_ENV_NAME}=${_qt_bin}${OS_ENV_SEPARATOR}${SHIBOKEN_CLANG_INSTALL_DIR}/bin${OS_ENV_SEPARATOR}$ENV{${OS_LIBRARY_ENV_NAME}}"
            "${shiboken_bin}" "--output-directory=${_output_dir}/wrap" "--project-file=${_side_config_file}"
        DEPENDS ${_generator_dependencies})

    add_library(${TARGET_NAME} SHARED ${_headers} ${_sources} ${_generator_dependencies})

    set_target_properties(
        ${TARGET_NAME} PROPERTIES PYSIDE_TYPESYSTEM_PATH "${_typesystem_generated}"
                                  PYSIDE_SOURCES_DIR "${_output_dir}/wrap/${_src_path}")

    # Since we have no guarantees how #includes in generated file will be look like add source dir of dependent
    # typesystems to include paths, it also simplifies search for other module that will link with current target
    get_target_property(_target_source_dir ${args_TARGET} SOURCE_DIR)

    target_include_directories(
        ${TARGET_NAME}
        PUBLIC "${_target_source_dir}"
               "${_typesystem_includes}"
               "${PYTHON_INCLUDE_DIR}"
               "${PYSIDE_INCLUDE_DIR}"
               "${PYSIDE_INCLUDE_DIR}/QtCore"
               "${PYSIDE_INCLUDE_DIR}/QtWidgets"
               "${PYSIDE_INCLUDE_DIR}/QtGui")

    if(args_INCLUDE_DIRS)
        target_include_directories(${TARGET_NAME} PUBLIC "${args_INCLUDE_DIRS}")
    endif()
    target_link_libraries(
        ${TARGET_NAME}
        ${args_TARGET}
        ${DCC_SHIBOKEN_LIB_TARGET}
        ${DCC_PYSIDE_TARGET}
        pybind11::pybind11
        ${PYTHON_LIBRARY}
        ${_dependent_pyside_modules}
        ${args_LIBRARY_DEPENDENCIES})

    if(args_PRIVATE_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} PRIVATE ${args_PRIVATE_DEFINITIONS})
    endif()
    if(args_PUBLIC_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} PUBLIC ${args_PUBLIC_DEFINITIONS})
    endif()

    if(MSVC)
        set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".pyd")
    else()
        set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".so")
    endif()
    set_target_properties(${TARGET_NAME} PROPERTIES PREFIX "" OUTPUT_NAME ${_module_name})

    if(args_PYTHON_INSTALL_DIR)
        set(_python_install_dir "${args_PYTHON_INSTALL_DIR}")
    else()
        set(_python_install_dir "${DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT}/opendcc")
    endif()

    install(
        TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION "${_python_install_dir}"
        LIBRARY DESTINATION "${_python_install_dir}")

    if(MSVC)
        install(
            FILES $<TARGET_PDB_FILE:${TARGET_NAME}>
            DESTINATION "${_python_install_dir}"
            OPTIONAL)
    endif()
    if(args_VS_FOLDER)
        make_folder("${args_VS_FOLDER}" ${TARGET_NAME})
    endif()
endfunction()
