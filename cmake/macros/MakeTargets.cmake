if(WIN32)
    set(OS_ENV_SEPARATOR ";")
    set(OS_LIBRARY_ENV_NAME "PATH")
else()
    set(OS_ENV_SEPARATOR ":")
    set(OS_LIBRARY_ENV_NAME "LD_LIBRARY_PATH")
endif()

#[=[
opendcc_add_package(PACKAGE_NAME)

Add the package subdirectory named PACKAGE_NAME, relative to the current source
directory. Inside that directory, OPENDCC_CURRENT_PACKAGE_NAME is set to the
package name. opendcc_make_library() and opendcc_make_python_module() use it to
add the private definition OPENDCC_PACKAGE_NAME="<package name>" to their targets.

On Windows, build files go to ${CMAKE_BINARY_DIR}/p/<PACKAGE_NAME> to keep paths
short. Other platforms use the normal add_subdirectory() binary directory.
This function does not install package.toml, Python files or resources; the
package's CMakeLists.txt must supply those installation rules.

Example, from src/packages/CMakeLists.txt:

    opendcc_add_package(opendcc.ui.node_editor)
]=]
function(opendcc_add_package PACKAGE_NAME)
    set(OPENDCC_CURRENT_PACKAGE_NAME "${PACKAGE_NAME}")
    if(WIN32)
        # Keep Windows build paths short without changing source include roots or install destinations.
        add_subdirectory("${PACKAGE_NAME}" "${CMAKE_BINARY_DIR}/p/${PACKAGE_NAME}")
    else()
        add_subdirectory("${PACKAGE_NAME}")
    endif()
endfunction()

#[=[
get_usd_env(EXTRA_ENVIRONMENT USD_ENVIRONMENT_VAR)

Build a search-path string for USD generator subprocesses. EXTRA_ENVIRONMENT is
one positional argument containing additional directories as a quoted CMake
list, or an empty string. USD_ENVIRONMENT_VAR is the name of the output variable;
the result is returned in the caller's scope, without changing the environment.

The result includes USD_LIBRARY_DIR, Boost_LIBRARY_DIR_RELEASE, the lib and bin
directories beside TBB_INCLUDE_DIRS, USD_GENSCHEMA_DIR, the extra directories,
and the existing PATH. On non-Windows platforms it also includes the existing
LD_LIBRARY_PATH. Entries are joined with OS_ENV_SEPARATOR: ';' on Windows and
':' elsewhere. These dependency variables must already be configured.

Example:

    set(_extra_runtime_dirs "${CMAKE_INSTALL_PREFIX}/bin;${CMAKE_INSTALL_PREFIX}/lib")
    get_usd_env("${_extra_runtime_dirs}" _usd_env)
    # Pass "PATH=${_usd_env}" to a generator via cmake -E env.
]=]
function(get_usd_env EXTRA_ENVIRONMENT USD_ENVIRONMENT_VAR)
    set(_result "")
    list(APPEND _result "${USD_LIBRARY_DIR}")
    list(APPEND _result "${Boost_LIBRARY_DIR_RELEASE}")
    # oneTBB installs runtime DLLs in bin; older TBB installs them in lib.
    list(APPEND _result "${TBB_INCLUDE_DIRS}/../lib")
    list(APPEND _result "${TBB_INCLUDE_DIRS}/../bin")
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

#[=[
parse_schema_file(
    SCHEMA_FILE USD_ENV OUTPUT_DIR
    GEN_HEADERS GEN_SRC GEN_SRC_PY SCHEMA_NAME SCHEMA_PREFIX HAVE_CLASSES)

Internal configure-time helper used by opendcc_make_usd_schema(). All arguments
are positional; the final six arguments are output VARIABLE NAMES, returned in
the caller's scope.

Inputs:
    SCHEMA_FILE    Path to the existing USD schema file.
    USD_ENV        Search-path string produced by get_usd_env(); quote it.
    OUTPUT_DIR     Directory used to calculate generated file paths.

Outputs:
    GEN_HEADERS    List of generated C++ header paths.
    GEN_SRC        List of generated C++ implementation paths.
    GEN_SRC_PY     List of generated Python-wrapper C++ paths.
    SCHEMA_NAME    Library name read from the schema metadata.
    SCHEMA_PREFIX  Library prefix read from the schema metadata.
    HAVE_CLASSES   TRUE if the schema declares classes, otherwise FALSE.

Runs parse_usd_schema.py using PYTHON_EXECUTABLE and USD_GENSCHEMA_SCRIPT, with
USD_LIBRARY_DIR/python on PYTHONPATH and USD_ENV on PATH/LD_LIBRARY_PATH.
It inspects the schema without generating the actual C++ files. Script failure
is a fatal configuration error; opendcc_make_usd_schema() handles generation.

Example, inside a schema helper:

    get_usd_env("" _usd_env)
    parse_schema_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/schema.usda" "${_usd_env}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated_src/example"
        _headers _sources _python_sources _name _prefix _have_classes)
]=]
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

#[=[
opendcc_make_python_module(TARGET_NAME
    PYMODULE_NAME <name>
    PYMODULE_SOURCES <files...>
    [NO_BOOST_PYTHON] [NO_INSTALL]
    [VS_FOLDER <folder>]
    [PYMODULE_INSTALL_DIR <directory>]
    [PYMODULE_INSTALL_PATH <path>]
    [LIBRARY_DEPENDENCIES <libraries...>]
    [PRIVATE_LIBRARY_DEPENDENCIES <libraries...>]
    [INCLUDE_DIRS <directories...>] [PRIVATE_INCLUDE_DIRS <directories...>]
    [PUBLIC_DEFINITIONS <definitions...>] [PRIVATE_DEFINITIONS <definitions...>])

Create a SHARED Python extension target. TARGET_NAME identifies the CMake target;
PYMODULE_NAME sets its output filename and the private PYMODULE_NAME definition.
The source must supply a matching Python initialization function. If either
PYMODULE_NAME or PYMODULE_SOURCES is empty, no target is created.

Flags (present = enabled; omit to disable, rather than passing ON/OFF):
    NO_BOOST_PYTHON
        Link pybind11::pybind11 and Python3::Python instead of the default
        pybind11::pybind11, Boost_PYTHON_LIBRARY and PYTHON_LIBRARY. Use this for
        extensions that do not need Boost.Python; it does not remove Boost
        dependencies inherited from other linked targets.
    NO_INSTALL
        Build the extension without registering installation or PDB rules.

Single-value arguments:
    PYMODULE_NAME
        Required Python module basename, without a platform suffix.
    VS_FOLDER
        Optional Visual Studio solution folder; ignored by other generators.
    PYMODULE_INSTALL_DIR
        Directory below DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT. Defaults to
        'opendcc', with a warning if no installation destination was supplied.
    PYMODULE_INSTALL_PATH
        Path relative to CMAKE_INSTALL_PREFIX, e.g. packages/example/python.
        Takes precedence over PYMODULE_INSTALL_DIR. Do not pass an absolute path.

List arguments:
    PYMODULE_SOURCES                 Extension sources and headers.
    LIBRARY_DEPENDENCIES             Additional PUBLIC link dependencies.
    PRIVATE_LIBRARY_DEPENDENCIES     Additional PRIVATE link dependencies.
    INCLUDE_DIRS                    PUBLIC compiler include directories.
    PRIVATE_INCLUDE_DIRS            PRIVATE compiler include directories.
    PUBLIC_DEFINITIONS              PUBLIC compile definitions, without -D.
    PRIVATE_DEFINITIONS             PRIVATE compile definitions, without -D.

The output has no library prefix and uses .pyd with MSVC, .so otherwise. Library,
archive, runtime and PDB output directories are set to
${CMAKE_CURRENT_BINARY_DIR}/<TARGET_NAME>; multi-config generators append the
configuration directory. Installation uses install(TARGETS), including install
RPATH handling; MSVC PDB installation is optional. Python .py files and package
__init__.py files require separate install() rules. The enclosing package name,
when set by opendcc_add_package(), becomes a private OPENDCC_PACKAGE_NAME define.

Example, for the existing app_config wrapper:

    opendcc_make_python_module(
        py_app_config
        NO_BOOST_PYTHON
        PYMODULE_NAME app_config
        PYMODULE_SOURCES wrap_config.cpp
        LIBRARY_DEPENDENCIES app_config
        PYMODULE_INSTALL_DIR opendcc
        VS_FOLDER base/app_config)
]=]
function(opendcc_make_python_module TARGET_NAME)
    set(options NO_BOOST_PYTHON NO_INSTALL)
    set(one_value_args PYMODULE_NAME VS_FOLDER PYMODULE_INSTALL_DIR PYMODULE_INSTALL_PATH)
    set(multi_value_args
        PYMODULE_SOURCES
        LIBRARY_DEPENDENCIES
        PRIVATE_LIBRARY_DEPENDENCIES
        INCLUDE_DIRS
        PRIVATE_INCLUDE_DIRS
        PUBLIC_DEFINITIONS
        PRIVATE_DEFINITIONS)

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
    if(OPENDCC_CURRENT_PACKAGE_NAME)
        target_compile_definitions(${_pymodule_target} PRIVATE OPENDCC_PACKAGE_NAME="${OPENDCC_CURRENT_PACKAGE_NAME}")
    endif()
    if(args_NO_BOOST_PYTHON)
        target_link_libraries(${_pymodule_target} PUBLIC pybind11::pybind11 Python3::Python)
    else()
        target_link_libraries(${_pymodule_target} PUBLIC pybind11::pybind11 ${Boost_PYTHON_LIBRARY} ${PYTHON_LIBRARY})
    endif()
    target_link_libraries(${TARGET_NAME} PUBLIC ${args_LIBRARY_DEPENDENCIES})
    target_link_libraries(${TARGET_NAME} PRIVATE ${args_PRIVATE_LIBRARY_DEPENDENCIES})
    target_include_directories(
        ${TARGET_NAME}
        PUBLIC ${args_INCLUDE_DIRS}
        PRIVATE ${args_PRIVATE_INCLUDE_DIRS})

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

    if(args_NO_INSTALL)
        return()
    endif()
    if(args_PYMODULE_INSTALL_PATH)
        set(_pymodule_install_dir "${CMAKE_INSTALL_PREFIX}/${args_PYMODULE_INSTALL_PATH}")
    else()
        if(NOT args_PYMODULE_INSTALL_DIR)
            message(
                WARNING
                    "PYMODULE_INSTALL_DIR for target ${TARGET_NAME} is not set. Default directory 'opendcc' will be used."
            )
            set(args_PYMODULE_INSTALL_DIR opendcc)
        endif()

        set(_pymodule_install_dir "${DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT}/${args_PYMODULE_INSTALL_DIR}")
    endif()
    # Target installation also applies the install RPATH on Unix platforms.
    install(
        TARGETS ${_pymodule_target}
        RUNTIME DESTINATION "${_pymodule_install_dir}"
        LIBRARY DESTINATION "${_pymodule_install_dir}")
    if(MSVC)
        install(
            FILES "$<TARGET_PDB_FILE:${_pymodule_target}>"
            DESTINATION "${_pymodule_install_dir}"
            OPTIONAL)
    endif()
endfunction()

#[=[
opendcc_make_library(TARGET_NAME
    [TYPE <STATIC|SHARED|PLUGIN|INTERFACE>] [QT_AUTOMOC] [VS_FOLDER <folder>]
    [PACKAGE_DIR <path>] [PLUGIN_DIR <path>] [RESOURCES_DIR <path>]
    [RUNTIME_INSTALL_DIR <path>] [LIBRARY_INSTALL_DIR <path>]
    [ARCHIVE_INSTALL_DIR <path>] [PDB_INSTALL_DIR <path>]
    [PUBLIC_HEADERS <files...>] [PRIVATE_HEADERS <files...>] [CPPFILES <files...>]
    [LIBRARY_DEPENDENCIES <libraries...>]
    [PRIVATE_LIBRARY_DEPENDENCIES <libraries...>]
    [INCLUDE_DIRS <directories...>] [PRIVATE_INCLUDE_DIRS <directories...>]
    [PUBLIC_DEFINITIONS <definitions...>] [PRIVATE_DEFINITIONS <definitions...>]
    [RESOURCES <files...>]
    [PYMODULE_NAME <name>] [PYMODULE_SOURCES <files...>]
    [PYMODULE_INSTALL_DIR <directory>] [PYMODULE_INSTALL_PATH <path>]
    [PYMODULE_NO_BOOST_PYTHON])

Create a library and register binary/PDB installation rules. Paths supplied as
sources are normally relative to the current source directory. Installation
paths below are relative to CMAKE_INSTALL_PREFIX.

Flags (omit to disable):
    QT_AUTOMOC
        Enable AUTOMOC for this target, with generated files under
        ${CMAKE_BINARY_DIR}/ag/<TARGET_NAME> to keep Windows paths short.
        Qt resources still require qt_add_resources() or other explicit rules.
    PYMODULE_NO_BOOST_PYTHON
        Forward NO_BOOST_PYTHON to the optional Python extension helper.

Single-value arguments:
    TYPE
        STATIC, SHARED (default), PLUGIN, or INTERFACE; case-insensitive. PLUGIN
        creates a SHARED library with plugin/package installation rules, not a CMake
        MODULE library. PLUGIN requires PACKAGE_DIR or the legacy PLUGIN_DIR.
        INTERFACE creates a usage-requirements target with no binary. It accepts
        LIBRARY_DEPENDENCIES, INCLUDE_DIRS and PUBLIC_DEFINITIONS as INTERFACE
        requirements, plus VS_FOLDER. Other arguments are rejected for this type.
        The target is registered in the usual export set; install headers and
        the export file separately.
    VS_FOLDER
        Optional Visual Studio solution folder for the library and its optional
        Python extension; ignored by other generators.
    PACKAGE_DIR
        Install library artifacts into packages/<path>. Also applies to TYPE
        STATIC or SHARED when supplied. Include a '/lib' suffix if desired.
    PLUGIN_DIR
        Deprecated legacy destination plugin/<path>; prefer PACKAGE_DIR for new
        packages. With TYPE STATIC/SHARED and no PACKAGE_DIR it is ignored.
        In the plugin/package branch, PLUGIN_DIR wins if both are supplied;
        specify only one of the two arguments.
    RESOURCES_DIR
        Override the resource destination, relative to CMAKE_INSTALL_PREFIX.
        Without it, resources go to <plugin-root>/<TARGET_NAME>; no automatic
        '/resources' suffix is added.
    RUNTIME_INSTALL_DIR / LIBRARY_INSTALL_DIR / ARCHIVE_INSTALL_DIR
        Override individual binary destinations relative to CMAKE_INSTALL_PREFIX,
        after TYPE/PACKAGE_DIR/PLUGIN_DIR defaults are resolved. For example,
        RUNTIME_INSTALL_DIR bin LIBRARY_INSTALL_DIR plugin/usd
        ARCHIVE_INSTALL_DIR plugin/usd preserves a split Windows/Unix layout.
        These overrides do not change the default resource destination.
    PDB_INSTALL_DIR
        Override the optional MSVC PDB destination relative to the install prefix.
        Defaults to the resolved runtime destination for shared libraries, or the
        resolved library destination for static libraries.
    PYMODULE_NAME
        Optional extension basename. Together with nonempty PYMODULE_SOURCES,
        creates the additional CMake target <TARGET_NAME>_py for a shared or
        plugin library. STATIC libraries do not create an extension.
    PYMODULE_INSTALL_DIR / PYMODULE_INSTALL_PATH
        Forwarded to opendcc_make_python_module(). The former is below the Python
        site-packages root (default 'opendcc'); the latter is below the install
        prefix and takes precedence. The C++ library's PACKAGE_DIR does not
        automatically relocate the Python extension.

List arguments:
    PUBLIC_HEADERS / PRIVATE_HEADERS
        Headers included in the target's source list. These labels do not set
        header visibility or install headers; add explicit install() rules.
    CPPFILES                        Implementation sources, including generated
                                    Qt resources when needed.
    LIBRARY_DEPENDENCIES             PUBLIC link dependencies.
    PRIVATE_LIBRARY_DEPENDENCIES     PRIVATE link dependencies.
    INCLUDE_DIRS                    PUBLIC compiler include directories.
    PRIVATE_INCLUDE_DIRS            PRIVATE compiler include directories.
    PUBLIC_DEFINITIONS              PUBLIC compile definitions, without -D.
    PRIVATE_DEFINITIONS             PRIVATE compile definitions, without -D.
    RESOURCES                       Resource files to install. Directory
                                    structure is flattened into the destination.
    PYMODULE_SOURCES                Sources for the optional Python extension.
                                    That extension links PUBLIC to this library;
                                    private library settings are not forwarded.

Default installation for ordinary STATIC/SHARED libraries: runtime DLLs to bin,
shared libraries and archives to lib, resource root plugin/opendcc/<TARGET_NAME>.
For package/plugin libraries, all binary artifact types go to PACKAGE_DIR or
PLUGIN_DIR as described above; resources default to the same root plus the target
name. RESOURCES files named exactly 'plugInfo.json' are configured with @ONLY and
LIBRARY_LOCATION set relative to the resource destination's parent directory.
A file named 'plugInfo.json.in' does not trigger that special handling.

The target is added to the <TARGET_NAME>-targets export set. This function does
not install the export file or headers: use install(EXPORT ...) and install(FILES
...)/install(DIRECTORY ...) separately. MSVC PDB installation is optional. The
enclosing package name becomes a private OPENDCC_PACKAGE_NAME definition.

Example, a Qt library with a pybind11 extension:

    opendcc_make_library(
        example_widgets
        QT_AUTOMOC
        PUBLIC_HEADERS api.h widget.h
        CPPFILES widget.cpp
        LIBRARY_DEPENDENCIES Qt${QT_VERSION_MAJOR}::Widgets
        PRIVATE_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/internal
        PRIVATE_DEFINITIONS EXAMPLE_WIDGETS_EXPORT
        PYMODULE_NO_BOOST_PYTHON
        PYMODULE_NAME example_widgets
        PYMODULE_SOURCES wrap_widget.cpp
        PYMODULE_INSTALL_DIR opendcc
        VS_FOLDER widgets)

    install(FILES api.h widget.h DESTINATION include/opendcc/example_widgets)
    install(EXPORT example_widgets-targets
        DESTINATION lib/cmake/example_widgets NAMESPACE opendcc::)

For a packaged plugin, add TYPE PLUGIN PACKAGE_DIR example.package/lib. To put
its resources in a sibling directory, use RESOURCES_DIR
packages/example.package/resources and list the files with RESOURCES.

Example, an interface target that propagates Python usage requirements:

    opendcc_make_library(
        py_utils
        TYPE INTERFACE
        LIBRARY_DEPENDENCIES logging pybind11::pybind11 Python3::Python)
]=]
function(opendcc_make_library TARGET_NAME)
    set(options QT_AUTOMOC PYMODULE_NO_BOOST_PYTHON)
    set(one_value_args
        TYPE # static shared plugin
        VS_FOLDER
        PLUGIN_DIR # deprecated: use PACKAGE_DIR instead
        PACKAGE_DIR
        PYMODULE_NAME
        PYMODULE_INSTALL_DIR
        PYMODULE_INSTALL_PATH
        RESOURCES_DIR
        RUNTIME_INSTALL_DIR
        LIBRARY_INSTALL_DIR
        ARCHIVE_INSTALL_DIR
        PDB_INSTALL_DIR)

    set(multi_value_args
        PUBLIC_HEADERS
        PRIVATE_HEADERS
        CPPFILES
        LIBRARY_DEPENDENCIES
        PRIVATE_LIBRARY_DEPENDENCIES
        INCLUDE_DIRS
        PRIVATE_INCLUDE_DIRS
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

    if(_library_type STREQUAL "INTERFACE")
        foreach(_argument IN LISTS options one_value_args multi_value_args)
            if(NOT _argument MATCHES "^(TYPE|VS_FOLDER|LIBRARY_DEPENDENCIES|INCLUDE_DIRS|PUBLIC_DEFINITIONS)$"
               AND DEFINED args_${_argument})
                if(args_${_argument})
                    message(FATAL_ERROR "${TARGET_NAME}: ${_argument} is not supported for TYPE INTERFACE")
                endif()
            endif()
        endforeach()
        add_library(${TARGET_NAME} INTERFACE)
        target_link_libraries(${TARGET_NAME} INTERFACE ${args_LIBRARY_DEPENDENCIES})
        target_include_directories(${TARGET_NAME} INTERFACE ${args_INCLUDE_DIRS})
        target_compile_definitions(${TARGET_NAME} INTERFACE ${args_PUBLIC_DEFINITIONS})
        if(args_VS_FOLDER)
            make_folder(${args_VS_FOLDER} ${TARGET_NAME})
        endif()
        install(TARGETS ${TARGET_NAME} EXPORT ${TARGET_NAME}-targets)
        return()
    endif()

    add_library(${TARGET_NAME} ${_library_type} ${args_PUBLIC_HEADERS} ${args_PRIVATE_HEADERS} ${args_CPPFILES})

    if(OPENDCC_CURRENT_PACKAGE_NAME)
        target_compile_definitions(${TARGET_NAME} PRIVATE OPENDCC_PACKAGE_NAME="${OPENDCC_CURRENT_PACKAGE_NAME}")
    endif()

    if(args_PUBLIC_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} PUBLIC ${args_PUBLIC_DEFINITIONS})
    endif()
    if(args_PRIVATE_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} PRIVATE ${args_PRIVATE_DEFINITIONS})
    endif()

    if(args_QT_AUTOMOC)
        set_property(TARGET ${TARGET_NAME} PROPERTY AUTOMOC ON)
        # Windows MAX_PATH: the default autogen path runs past 260 chars in a deep tree and cl.exe then cannot open
        # moc_<header>.cpp. Keep it short rather than depend on the checkout depth.
        set_property(TARGET ${TARGET_NAME} PROPERTY AUTOGEN_BUILD_DIR "${CMAKE_BINARY_DIR}/ag/${TARGET_NAME}")
    endif()

    target_include_directories(
        ${TARGET_NAME}
        PUBLIC ${args_INCLUDE_DIRS}
        PRIVATE ${args_PRIVATE_INCLUDE_DIRS})

    target_link_libraries(${TARGET_NAME} PUBLIC ${args_LIBRARY_DEPENDENCIES})
    target_link_libraries(${TARGET_NAME} PRIVATE ${args_PRIVATE_LIBRARY_DEPENDENCIES})

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

    foreach(_artifact RUNTIME LIBRARY ARCHIVE)
        if(DEFINED args_${_artifact}_INSTALL_DIR)
            string(TOLOWER ${_artifact} _artifact_lower)
            set(_${_artifact_lower}_destination "${args_${_artifact}_INSTALL_DIR}")
        endif()
    endforeach()
    if(DEFINED args_PDB_INSTALL_DIR)
        set(_pdb_destination "${args_PDB_INSTALL_DIR}")
    elseif(_library_type STREQUAL "SHARED")
        set(_pdb_destination "${_runtime_destination}")
    else()
        set(_pdb_destination "${_library_destination}")
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
                DESTINATION ${_pdb_destination}
                OPTIONAL)
        else()
            install(
                FILES "$<TARGET_FILE_DIR:${TARGET_NAME}>/$<TARGET_FILE_NAME:${TARGET_NAME}>.pdb"
                DESTINATION ${_pdb_destination}
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
        set(_pymodule_options)
        if(args_PYMODULE_NO_BOOST_PYTHON)
            list(APPEND _pymodule_options NO_BOOST_PYTHON)
        endif()
        opendcc_make_python_module(
            ${TARGET_NAME}_py
            ${_pymodule_options}
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

#[=[
opendcc_make_executable(TARGET_NAME
    CPPFILES <files...>
    [LINUX_LAUNCHER] [WIN32] [QT_AUTOMOC] [VS_DEBUG_FROM_BUILD_TREE]
    [VS_FOLDER <folder>]
    [LIBRARY_DEPENDENCIES <libraries...>] [INCLUDE_DIRS <directories...>]
    [PRIVATE_DEFINITIONS <definitions...>])

Create an executable and install it to bin below CMAKE_INSTALL_PREFIX. TARGET_NAME
is the logical application name. Flags are enabled by their presence; omit a flag
to disable it, rather than passing ON/OFF.

Flags:
    LINUX_LAUNCHER
        On UNIX AND NOT APPLE, create the actual CMake target <TARGET_NAME>.bin
        and install a shell launcher named <TARGET_NAME>. The launcher sources
        opendcc_setup.sh from the install prefix and executes the .bin file.
        On Windows/macOS, the target keeps TARGET_NAME and no launcher is added.
        If configuring the target after this call on Linux, use its .bin name.
    WIN32
        Pass WIN32 to add_executable(), selecting the GUI subsystem on Windows.
        This does not automatically add Qt5::WinMain; link it explicitly when
        the entry point requires it. Omit the flag for a console application.
    QT_AUTOMOC
        Enable AUTOMOC with AUTOGEN_BUILD_DIR under
        ${CMAKE_BINARY_DIR}/ag/<actual-target-name>. Qt resources still need
        explicit qt_add_resources() calls.
    VS_DEBUG_FROM_BUILD_TREE
        With MSVC, run the built executable in its build output directory when
        debugging. By default the debugger runs the installed executable from
        ${CMAKE_INSTALL_PREFIX}/bin. Has no effect outside the MSVC branch.

Single-value argument:
    VS_FOLDER                       Optional Visual Studio solution folder.

List arguments:
    CPPFILES                        Executable sources, headers and generated
                                    resources; supply the sources to build.
    LIBRARY_DEPENDENCIES             PRIVATE link dependencies.
    INCLUDE_DIRS                    PRIVATE compiler include directories.
    PRIVATE_DEFINITIONS             PRIVATE compile definitions, without -D.

On Windows, WIN32_MANIFEST is added as a source independently of the WIN32 flag.
With MSVC, PDB installation is optional. If DCC_MAKE_VSDEBUG_LAUNCHER is enabled,
VS_ENVIRONMENT is also assigned to VS_DEBUGGER_ENVIRONMENT. The helper does not
install runtime dependencies or generate opendcc_setup.sh; the root project's
installation machinery supplies them.

Example:

    opendcc_make_executable(
        usd_render
        LINUX_LAUNCHER
        CPPFILES main.cpp
        LIBRARY_DEPENDENCIES usd_render_app
        INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/src/bin
        VS_FOLDER usd)
]=]
function(opendcc_make_executable TARGET_NAME)
    set(options LINUX_LAUNCHER WIN32 QT_AUTOMOC VS_DEBUG_FROM_BUILD_TREE)
    set(one_value_args VS_FOLDER)
    set(multi_value_args CPPFILES LIBRARY_DEPENDENCIES INCLUDE_DIRS PRIVATE_DEFINITIONS)
    cmake_parse_arguments(args "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    set(_binary_name ${TARGET_NAME})
    if(args_LINUX_LAUNCHER
       AND UNIX
       AND NOT APPLE)
        set(_binary_name "${TARGET_NAME}.bin")
    endif()
    set(_executable_options)
    if(args_WIN32)
        list(APPEND _executable_options WIN32)
    endif()
    add_executable(${_binary_name} ${_executable_options} ${args_CPPFILES})
    if(WIN32)
        target_sources(${_binary_name} PRIVATE ${WIN32_MANIFEST})
    endif()
    target_link_libraries(${_binary_name} PRIVATE ${args_LIBRARY_DEPENDENCIES})
    target_include_directories(${_binary_name} PRIVATE ${args_INCLUDE_DIRS})
    target_compile_definitions(${_binary_name} PRIVATE ${args_PRIVATE_DEFINITIONS})
    if(args_QT_AUTOMOC)
        set_target_properties(${_binary_name} PROPERTIES AUTOMOC ON AUTOGEN_BUILD_DIR
                                                                    "${CMAKE_BINARY_DIR}/ag/${_binary_name}")
    endif()
    if(args_VS_FOLDER)
        make_folder(${args_VS_FOLDER} ${_binary_name})
    endif()

    install(TARGETS ${_binary_name} RUNTIME DESTINATION bin)
    if(MSVC)
        if(args_VS_DEBUG_FROM_BUILD_TREE)
            set(_debugger_command "$<TARGET_FILE:${_binary_name}>")
            set(_debugger_directory "$<TARGET_FILE_DIR:${_binary_name}>")
        else()
            set(_debugger_command "${CMAKE_INSTALL_PREFIX}/bin/$<TARGET_FILE_NAME:${_binary_name}>")
            set(_debugger_directory "${CMAKE_INSTALL_PREFIX}/bin")
        endif()
        set_target_properties(${_binary_name} PROPERTIES VS_DEBUGGER_COMMAND "${_debugger_command}"
                                                         VS_DEBUGGER_WORKING_DIRECTORY "${_debugger_directory}")
        if(DCC_MAKE_VSDEBUG_LAUNCHER)
            set_target_properties(${_binary_name} PROPERTIES VS_DEBUGGER_ENVIRONMENT "${VS_ENVIRONMENT}")
        endif()
        install(
            FILES "$<TARGET_PDB_FILE:${_binary_name}>"
            DESTINATION bin
            OPTIONAL)
    elseif(
        args_LINUX_LAUNCHER
        AND UNIX
        AND NOT APPLE)
        configure_file("${PROJECT_SOURCE_DIR}/cmake/macros/launch_script.sh"
                       "${CMAKE_CURRENT_BINARY_DIR}/launch_script.sh" @ONLY NEWLINE_STYLE UNIX)
        install(
            PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/launch_script.sh"
            DESTINATION bin
            RENAME ${TARGET_NAME})
    endif()
endfunction()

#[=[
opendcc_make_usd_schema(TARGET_NAME
    [PACKAGE_DIR <path>] [PLUGIN_DIR <path>]
    [SCHEMA_FILE <file>] [VS_FOLDER <folder>]
    [CUSTOM_RUNTIME_DIR <path>] [HEADER_INSTALL_DIR <path>]
    [SRC_GENERATION_DIR <path>] [PYTHON_INSTALL_DIR <path>]
    [PUBLIC_HEADERS <files...>] [PRIVATE_HEADERS <files...>]
    [CPPFILES <files...>] [PYMODULE_CPPFILES <files...>]
    [INCLUDE_DIRS <directories...>] [LIBRARY_DEPENDENCIES <libraries...>]
    [SCHEMA_DEPENDENCIES <targets...>] [EXTRA_USD_ENVIRONMENT <directories...>]
    [PUBLIC_DEFINITIONS <definitions...>] [PRIVATE_DEFINITIONS <definitions...>]
    [AFTER_GEN_SCRIPT <script> <arguments...>] [PYTHON_FILES <files...>])

Parse a USD schema at configure time, then generate and build its C++ and Python
bindings at build time. Creates SHARED targets TARGET_NAME and <TARGET_NAME>_py.
There are no boolean flags. USD, Boost.Python, Python and TBB dependencies must
already be configured, including USD_GENSCHEMA and USD_GENSCHEMA_SCRIPT.

Single-value arguments:
    PACKAGE_DIR
        Schema plugin root packages/<path>, relative to the install prefix.
    PLUGIN_DIR
        Deprecated alternative root plugin/<path>. One of PACKAGE_DIR or
        PLUGIN_DIR is required; PLUGIN_DIR wins if both are given. Prefer
        PACKAGE_DIR for new packages and do not specify both.
    SCHEMA_FILE
        Path relative to the current source directory; defaults to schema.usda.
    VS_FOLDER
        Optional Visual Studio solution folder for both generated targets.
    CUSTOM_RUNTIME_DIR
        Destination for all C++ binary artifact types and its MSVC PDB, relative
        to CMAKE_INSTALL_PREFIX. Defaults to the schema plugin root above.
    HEADER_INSTALL_DIR
        Installation destination for generated headers and PUBLIC_HEADERS.
        Supply it explicitly; this helper provides no project-specific default.
    SRC_GENERATION_DIR
        Generated source subdirectory below
        ${CMAKE_CURRENT_BINARY_DIR}/generated_src. Defaults to the basename of
        CMAKE_CURRENT_BINARY_DIR. Use a relative path, including nested include
        directories when required by generated #include directives.
    PYTHON_INSTALL_DIR
        Base destination for the Python package; defaults to
        DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT. The schema's library prefix is
        appended as a subdirectory. Unlike PYMODULE_INSTALL_PATH on the regular
        module helper, this is a direct install destination, not an automatically
        prefix-prepended path. An absolute path or a prefix-relative path works.

List arguments:
    PUBLIC_HEADERS                  Extra C++ headers, installed alongside the
                                    generated headers via PUBLIC_HEADER.
    PRIVATE_HEADERS                 Extra headers in both target source lists;
                                    not installed by this helper.
    CPPFILES                        Extra C++ implementation sources.
    PYMODULE_CPPFILES               Extra Python-wrapper C++ sources, such as
                                    the module entry point.
    INCLUDE_DIRS                    Additional PUBLIC C++ include directories.
    LIBRARY_DEPENDENCIES             Additional transitive C++ link dependencies;
                                    the Python wrapper links to the C++ target.
    SCHEMA_DEPENDENCIES              Targets needed by schema generation. Their
                                    binary directories are exposed through the
                                    USD plugin-path environment variable, and
                                    they are dependencies of the generation rule.
                                    Add them to LIBRARY_DEPENDENCIES as well if
                                    the resulting library must link to them.
    EXTRA_USD_ENVIRONMENT            Declared extra generator runtime directories.
                                    Current limitation: the get_usd_env() call
                                    forwards this list without quoting and with
                                    an extra argument. Nonempty values do not
                                    reliably populate _usd_env; leave unset until
                                    that forwarding is corrected.
    PUBLIC_DEFINITIONS              PUBLIC definitions on the C++ target.
    PRIVATE_DEFINITIONS             PRIVATE definitions on the C++ target.
                                    Neither definition list is passed as command
                                    line options to usdGenSchema.
    AFTER_GEN_SCRIPT                Python script relative to the current source
                                    directory, followed by its arguments. Runs
                                    after usdGenSchema and after plugInfo.json
                                    has been copied to plugInfo.json.in, before
                                    final plugInfo configuration. Its working
                                    directory is the generated source directory.
    PYTHON_FILES                    Python files installed beside the extension;
                                    normally includes __init__.py.

The schema metadata supplies the library name and prefix; they need not equal
TARGET_NAME. The C++ target receives <UPPERCASE_SCHEMA_NAME>_EXPORTS. The Python
extension is named _<schema-name>.pyd with MSVC, .so otherwise, and receives the
MFB_PACKAGE_NAME, MFB_ALT_PACKAGE_NAME and MFB_PACKAGE_MODULE definitions.

Plugin metadata and generatedSchema.usda are installed under
<schema-plugin-root>/<schema-name>/resources. The input schema is installed in a
further <schema-name> directory there. A build-tree plugInfo.json is also written
beside the C++ library. The helper installs the C++ target export under
lib/cmake/<TARGET_NAME> with namespace opendcc:: and optionally installs MSVC PDBs.
Package manifests and unrelated Python directories need separate install rules.

Example, in a package containing schema.usda and module.cpp:

    opendcc_make_usd_schema(
        example_schema
        PACKAGE_DIR example.package/pxr_plugins
        SCHEMA_FILE schema.usda
        SRC_GENERATION_DIR opendcc/example/schema
        HEADER_INSTALL_DIR include/opendcc/example/schema
        CUSTOM_RUNTIME_DIR packages/example.package/lib
        PYTHON_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/packages/example.package/python"
        PYMODULE_CPPFILES module.cpp
        PYTHON_FILES __init__.py
        VS_FOLDER packages/example)
]=]
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

#[=[
parse_pyside_typesystem(
    TYPESYSTEM_FILE MODULE_NAME SRC_PATH HEADERS SOURCES TYPESYSTEMS)

Internal configure-time helper used by opendcc_make_shiboken_bindings(). Runs
make_shiboken_target.py parse_typesystem using PYTHON_EXECUTABLE. All arguments
are positional. TYPESYSTEM_FILE is the existing XML file path; the remaining
arguments are output VARIABLE NAMES returned in the caller's scope:

    MODULE_NAME    Module basename from the XML package attribute.
    SRC_PATH       Relative wrapper directory beneath the build's wrap directory.
    HEADERS        Expected generated wrapper header paths.
    SOURCES        Expected generated wrapper C++ source paths.
    TYPESYSTEMS    Names from load-typesystem elements in the XML.

The parser assumes the OpenDCC Python namespace and calculates file names from
the XML object-type/value-type declarations. It does not run Shiboken or generate
the wrapper files. Script failure is a fatal configuration error.

Example:

    parse_pyside_typesystem(
        "${CMAKE_CURRENT_SOURCE_DIR}/common_widgets.xml"
        _module_name _source_path _headers _sources _dependent_typesystems)
]=]
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

#[=[
opendcc_make_shiboken_bindings(TARGET_NAME
    TARGET <cpp-target> SOURCE_DIR <directory>
    [VERBOSE] [VS_FOLDER <folder>]
    [LEGACY_HEADER_FILEPATH <file>] [CUSTOM_PROJECT_FILE <file>]
    [PYTHON_INSTALL_DIR <path>]
    [GLUE_FILES <files...>] [LIBRARY_DEPENDENCIES <libraries...>]
    [INCLUDE_DIRS <directories...>] [GEN_CONFIG_INCLUDE_DIRS <directories...>]
    [PUBLIC_DEFINITIONS <definitions...>] [PRIVATE_DEFINITIONS <definitions...>])

Generate a SHARED Python extension with the configured Shiboken/PySide toolchain.
TARGET_NAME names the new wrapper target; TARGET names the existing C++ target
being wrapped. Qt, Python, PySide and Shiboken must already be configured.

Flag (omit to disable):
    VERBOSE
        Print parsed module, source and typesystem information during CMake
        configuration. For an automatically generated project file, request
        Shiboken warnings instead of passing the silent option. Usually supplied
        via ${_verbose_shiboken_output}, controlled by DCC_VERBOSE_SHIBOKEN_OUTPUT.

Single-value arguments:
    TARGET
        Required existing C++ target to wrap and link against.
    SOURCE_DIR
        Directory relative to the current source directory, containing
        <TARGET>.xml and, by default, the umbrella header <TARGET>.h. Use '.' for
        the current directory. XML package metadata determines the module name.
    LEGACY_HEADER_FILEPATH
        Override the umbrella header name/path relative to SOURCE_DIR. The XML
        filename remains <TARGET>.xml.
    CUSTOM_PROJECT_FILE
        Existing Shiboken project file, passed directly to --project-file. Use
        an absolute path to avoid build/source directory ambiguity. Skips automatic
        side_config generation, but the XML is still parsed and configured to
        calculate wrapper outputs. The custom file must provide its own paths
        and parser configuration.
    VS_FOLDER
        Optional Visual Studio solution folder for the wrapper target.
    PYTHON_INSTALL_DIR
        Direct installation destination for the extension and optional MSVC PDB.
        Defaults to ${DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT}/opendcc. Use an absolute
        path or an install-prefix-relative path. The XML package directory is not
        automatically appended; supply the complete destination directory.

List arguments:
    GLUE_FILES                      Additional inputs that trigger wrapper
                                    regeneration when changed. Also included in
                                    the target source list, so C++ glue files
                                    are compiled into the extension.
    LIBRARY_DEPENDENCIES             Additional transitive wrapper link libraries.
    INCLUDE_DIRS                    Additional PUBLIC include directories for
                                    compiling the wrapper, not for parsing it.
    GEN_CONFIG_INCLUDE_DIRS         Include paths passed to Shiboken through the
                                    generated project file, not compiler include
                                    paths. Supply both lists when both phases
                                    need a directory. With CUSTOM_PROJECT_FILE,
                                    place generator include paths in that file.
    PUBLIC_DEFINITIONS              PUBLIC wrapper compile definitions, without -D.
    PRIVATE_DEFINITIONS             PRIVATE wrapper compile definitions, without -D.
                                    These definitions are not automatically
                                    forwarded to the Shiboken parser.

The helper reads known Qt/PySide/USD/Boost/Python include paths, discovers loaded
typesystems, configures the XML with @ONLY, and generates wrappers under
${CMAKE_CURRENT_BINARY_DIR}/wrap. An automatically generated side_config.txt uses
CMAKE_CXX_STANDARD. The target links to TARGET, the configured PySide/Shiboken
libraries, pybind11 and Python. It has no output prefix; its suffix is .pyd with
MSVC and .so otherwise. PYSIDE_TYPESYSTEM_PATH and PYSIDE_SOURCES_DIR target
properties expose its configured typesystem and generated source directory.

QT_AUTOMOC is not a supported flag here, and PYMODULE_INSTALL_DIR is not an
accepted argument; use PYTHON_INSTALL_DIR. The helper does not install .py files.

Example, with common_widgets.xml and common_widgets.h in the current directory:

    opendcc_make_shiboken_bindings(
        common_widgets_py
        TARGET common_widgets
        SOURCE_DIR .
        GEN_CONFIG_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/src/lib"
        INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/src/lib"
        PRIVATE_DEFINITIONS QT_NOT_GEN
        VS_FOLDER widgets)
]=]
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

    # The typesystem differs between Qt5 and Qt6 (DCC_QT_SEQ_CONTAINER), so configure it into the build tree and point
    # shiboken and PYSIDE_TYPESYSTEM_PATH at that copy.
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

    # Shiboken does not inherit compiler flags; include profiling before Qt's emit macro.
    set(_shiboken_clang_args "")
    if(TBB_INCLUDE_DIRS AND EXISTS "${TBB_INCLUDE_DIRS}/oneapi/tbb/profiling.h")
        set(_shiboken_clang_args "--clang-options=-include,${TBB_INCLUDE_DIRS}/oneapi/tbb/profiling.h")
    endif()

    add_custom_command(
        OUTPUT ${_headers} ${_sources}
        COMMAND
            ${CMAKE_COMMAND} -E env
            "${OS_LIBRARY_ENV_NAME}=${_qt_bin}${OS_ENV_SEPARATOR}${SHIBOKEN_CLANG_INSTALL_DIR}/bin${OS_ENV_SEPARATOR}$ENV{${OS_LIBRARY_ENV_NAME}}"
            "${shiboken_bin}" "--output-directory=${_output_dir}/wrap" ${_shiboken_clang_args}
            "--project-file=${_side_config_file}"
        DEPENDS ${_generator_dependencies})

    add_library(${TARGET_NAME} SHARED ${_headers} ${_sources} ${_generator_dependencies})

    set_target_properties(${TARGET_NAME} PROPERTIES PYSIDE_TYPESYSTEM_PATH "${_typesystem_generated}"
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
