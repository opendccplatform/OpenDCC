include(ExternalProject)

if(CMAKE_COMPILER_IS_GNUCC)
    find_package(Threads)
endif()

find_package(USD REQUIRED)
if(DCC_USE_PTEX)
    find_package(PTex REQUIRED)
endif()

if(DCC_KATANA_SUPPORT)
    add_definitions(-DBOOST_ALL_DYN_LINK -D__TBB_NO_IMPLICIT_LINKAGE -DBOOST_ALL_NO_LIB)
endif()


find_package(doctest REQUIRED)
find_package(TBB REQUIRED)
find_package(GLEW REQUIRED)
find_package(ZMQ REQUIRED)
find_package(Eigen3 REQUIRED)
if(USD_VERSION VERSION_GREATER_EQUAL 0.22.05) # needed for texture painting which is enabled only for USD >= 22.05
    find_package(IGL REQUIRED)
endif()
if(USD_VERSION VERSION_GREATER_EQUAL 0.22.05 AND DCC_PACKAGE_OPENDCC_USD_EDITOR_CANVAS)
    find_package(Skia REQUIRED)
endif()
if(DCC_BUILD_BULLET_PHYSICS)
    find_package(Bullet3 REQUIRED)
endif()
find_package(Embree 3 REQUIRED)
if(DCC_BUILD_ARNOLD_SUPPORT
   OR DCC_USD_FALLBACK_PROXY_BUILD_ARNOLD_USD)
    find_package(Arnold REQUIRED)
    find_package(ArnoldUsd REQUIRED)
endif()

if(DCC_BUILD_RENDERMAN_SUPPORT)
    find_package(Renderman REQUIRED)
endif()

if(DCC_USD_FALLBACK_PROXY_BUILD_CYCLES)
    find_package(Cycles REQUIRED)
endif()

if(DCC_USE_PYTHON_3)
    if(NOT DCC_HOUDINI_SUPPORT)
        find_package(
            Python3
            COMPONENTS Development Interpreter
            REQUIRED)
    endif()

    find_package(PythonInterp 3.0 REQUIRED)
    find_package(PythonLibs 3.0 REQUIRED)
    find_package(pybind11 REQUIRED)
else()
    find_package(PythonInterp 2.7 REQUIRED)
    find_package(PythonLibs 2.7 REQUIRED)
endif()

if(WIN32)
    set(DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT ${CMAKE_INSTALL_PREFIX}/python/lib/site-packages)
else()
    set(DCC_PYTHON_INSTALL_SITE_PACKAGES_ROOT
        ${CMAKE_INSTALL_PREFIX}/lib/python${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}/site-packages)
endif()

set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

if(DCC_NODE_EDITOR)
    find_package(Graphviz REQUIRED)
endif()

if(DCC_INSTALL_BOOST AND DCC_INSTALL_OSL)
    list(APPEND BOOST_COMPONENTS wave)
endif()

# XXX: workaround for bug introduced in boost 1.70 install config: https://github.com/boostorg/boost_install/issues/5
set(BUILD_SHARED_LIBS ON)

if(DCC_HOUDINI_SUPPORT)
    if(NOT DEFINED HOUDINI_ROOT)
        message(FATAL_ERROR "HOUDINI_ROOT is not defined.")
    endif()
    string(REPLACE "\\" "/" HOUDINI_ROOT "${HOUDINI_ROOT}")
endif()

# Set up a version string for comparisons. This is available as Boost_VERSION_STRING in CMake 3.14+ Find Boost package
# before getting any boost specific components as we need to disable boost-provided cmake config, based on the boost
# version found.
find_package(Boost REQUIRED)

set(boost_version_string "${Boost_MAJOR_VERSION}.${Boost_MINOR_VERSION}.${Boost_SUBMINOR_VERSION}")

# Boost provided cmake files (introduced in boost version 1.70) result in inconsistent build failures on different
# platforms, when trying to find boost component dependencies like python, program options, etc. Refer some related
# discussions: https://github.com/boostorg/python/issues/262#issuecomment-483069294
# https://github.com/boostorg/boost_install/issues/12#issuecomment-508683006
#
# Hence to avoid issues with Boost provided cmake config, Boost_NO_BOOST_CMAKE is enabled by default for boost version
# 1.70 and above. If a user explicitly set Boost_NO_BOOST_CMAKE to Off, following will be a no-op.
if(${boost_version_string} VERSION_GREATER_EQUAL "1.70")
    option(Boost_NO_BOOST_CMAKE "Disable boost-provided cmake config" ON)
    if(Boost_NO_BOOST_CMAKE)
        message(STATUS "Disabling boost-provided cmake config")
    endif()
endif()

find_package(
    Boost
    COMPONENTS ${BOOST_COMPONENTS}
    REQUIRED)

# USD 24.11+ can be built with a vendored boost.python (pxr_boost::python, baked into pxr.h as
# PXR_USE_INTERNAL_BOOST_PYTHON); newer USD defines it unconditionally. In that mode real
# boost.python must not be found or linked. A '#if 0'-guarded define means USD was built against
# real boost.python.
set(USD_USES_INTERNAL_BOOST_PYTHON FALSE)
if(USD_PXR_VERSION GREATER_EQUAL 2411 AND EXISTS "${USD_INCLUDE_DIR}/pxr/pxr.h")
    file(READ "${USD_INCLUDE_DIR}/pxr/pxr.h" _pxr_h_text)
    if(_pxr_h_text MATCHES "#define[ \t]+PXR_USE_INTERNAL_BOOST_PYTHON"
       AND NOT _pxr_h_text MATCHES "#if[ \t]+0[ \t\r\n]+#define[ \t]+PXR_USE_INTERNAL_BOOST_PYTHON")
        set(USD_USES_INTERNAL_BOOST_PYTHON TRUE)
    endif()
    unset(_pxr_h_text)
endif()

if(USD_USES_INTERNAL_BOOST_PYTHON)
    # USD exports its vendored boost.python as the imported target 'python' (libpython);
    # route all ${Boost_PYTHON_LIBRARY} link references to it.
    message(STATUS "USD uses internal pxr boost.python; skipping boost.python")
    set(Boost_PYTHON_LIBRARY python)
elseif(${boost_version_string} VERSION_GREATER_EQUAL "1.67")
    set(python_version_nodot "${PYTHON_VERSION_MAJOR}${PYTHON_VERSION_MINOR}")
    find_package(
        Boost
        COMPONENTS python${python_version_nodot}
        REQUIRED)
    set(Boost_PYTHON_LIBRARY "${Boost_PYTHON${python_version_nodot}_LIBRARY}")
    find_package(
        Boost
        COMPONENTS python
        REQUIRED)
else()
    find_package(
        Boost
        COMPONENTS python
        REQUIRED)
endif()
if(Python3_FOUND AND NOT USD_USES_INTERNAL_BOOST_PYTHON)
    find_package(
        Boost
        COMPONENTS python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}
        REQUIRED)
endif()

if(DCC_HOUDINI_SUPPORT)
    file(STRINGS "${HOUDINI_ROOT}/toolkit/include/SYS/SYS_Version.h" _houdini_version_str
         REGEX "^#define SYS_VERSION_FULL .*$")
    string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _houdini_version_match ${_houdini_version_str})
    set(HOUDINI_MAJOR_VERSION ${CMAKE_MATCH_1})
    set(HOUDINI_MINOR_VERSION ${CMAKE_MATCH_2})
    set(HOUDINI_BUILD_VERSION ${CMAKE_MATCH_3})
    set(HOUDINI_VERSION "${HOUDINI_MAJOR_VERSION}.${HOUDINI_MINOR_VERSION}.${HOUDINI_BUILD_VERSION}")

    # Using Houdini's version of usdGenSchema starts hython and hserver which can store handles to the some directories
    # in binary dir. To avoid this we copy and patch usdGenSchema so it can be used with basic python.
    list(GET USD_GENSCHEMA 1 _houdini_usd_gen_script)
    file(READ "${_houdini_usd_gen_script}" _houdini_usd_gen_script_text)
    string(REPLACE "hou.exit(" "sys.exit(" _houdini_usd_gen_script_text "${_houdini_usd_gen_script_text}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/usdGenSchema" "${_houdini_usd_gen_script_text}")
    list(REMOVE_AT USD_GENSCHEMA 1)
    list(APPEND USD_GENSCHEMA "${CMAKE_CURRENT_BINARY_DIR}/usdGenSchema")
    set(USD_GENSCHEMA
        ${USD_GENSCHEMA}
        CACHE STRING "" FORCE)
    unset(_houdini_usd_gen_script)
    unset(_houdini_usd_gen_script_text)

    execute_process(COMMAND "${CMAKE_COMMAND}" -E create_symlink "${Boost_INCLUDE_DIR}/hboost"
                            "${PROJECT_BINARY_DIR}/include/boost")
    add_definitions(-DOPENDCC_HOUDINI_SUPPORT -DHBOOST_ALL_DYN_LINK -DHBOOST_ALL_NO_LIB)
    add_library(_houdini_compatibility INTERFACE)
    target_precompile_headers(_houdini_compatibility INTERFACE "${CMAKE_CURRENT_LIST_DIR}/houdini_compatibility_pch.h")
    export(TARGETS _houdini_compatibility FILE _houdini_compatibility-targets.cmake)
    install(TARGETS _houdini_compatibility EXPORT _houdini_compatibility-targets)
    install(
        EXPORT _houdini_compatibility-targets
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/_houdini_compatibility
        NAMESPACE opendcc::)
    link_libraries(_houdini_compatibility)

endif()
set(BUILD_SHARED_LIBS OFF)

find_package(OpenGL REQUIRED)
find_package(OpenMesh REQUIRED)
# CMAKE_PREFIX_PATH decides Qt5 or Qt6. Everything below uses Qt${QT_VERSION_MAJOR}::.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
# PySide2 binds Qt5 and PySide6 binds Qt6. There is no PySide5, so this is not QT_VERSION_MAJOR.
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    set(DCC_PYSIDE_VERSION 6)
else()
    set(DCC_PYSIDE_VERSION 2)
endif()

message(STATUS "Building against Qt ${QT_VERSION} (PySide${DCC_PYSIDE_VERSION})")

set(DCC_QT_COMPONENTS
    Core
    Gui
    Widgets
    OpenGL
    Svg
    Multimedia
    Network
    LinguistTools)
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    # Qt6 split QOpenGLWidget out of QtOpenGL, and QSvgWidget out of QtSvg
    list(APPEND DCC_QT_COMPONENTS OpenGLWidgets SvgWidgets)
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${DCC_QT_COMPONENTS})

# Qt6 has no _qt5Core_install_prefix, so take the Qt root from the Core target.
# Used for the shiboken generator's PATH and for lupdate/lrelease.
get_target_property(_dcc_qt_core_loc Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION_RELEASE)
if(NOT _dcc_qt_core_loc)
    get_target_property(_dcc_qt_core_loc Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION_RELWITHDEBINFO)
endif()
if(NOT _dcc_qt_core_loc)
    get_target_property(_dcc_qt_core_loc Qt${QT_VERSION_MAJOR}::Core LOCATION)
endif()
get_filename_component(_dcc_qt_core_dir "${_dcc_qt_core_loc}" DIRECTORY)
get_filename_component(DCC_QT_INSTALL_PREFIX "${_dcc_qt_core_dir}" DIRECTORY)
message(STATUS "Qt install prefix: ${DCC_QT_INSTALL_PREFIX}")

# Qt6 does not populate Qt5Widgets_INCLUDE_DIRS et al, and shiboken's clang parser needs
# real -I paths.
set(DCC_QT_INCLUDE_DIRS)
foreach(_dcc_qt_mod Core Gui Widgets)
    get_target_property(_dcc_qt_mod_inc Qt${QT_VERSION_MAJOR}::${_dcc_qt_mod} INTERFACE_INCLUDE_DIRECTORIES)
    if(_dcc_qt_mod_inc)
        list(APPEND DCC_QT_INCLUDE_DIRS ${_dcc_qt_mod_inc})
    endif()
endforeach()
list(REMOVE_DUPLICATES DCC_QT_INCLUDE_DIRS)

# Link instead of Qt::OpenGL where QOpenGLWidget is used; Qt6 moved it to QtOpenGLWidgets.
# Qt6 makes QVector an alias of QList, so shiboken6 registers only QList. Typesystem XMLs are
# configure_file'd with this placeholder.
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    set(DCC_QT_SEQ_CONTAINER QList)
else()
    set(DCC_QT_SEQ_CONTAINER QVector)
endif()

# pxr is a using-directive into PXR_INTERNAL_NS, not an alias, so clang reports the internal
# name. shiboken6 matches on that and silently drops methods spelled pxr::; shiboken2 did not.
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    file(STRINGS "${USD_ROOT}/include/pxr/pxr.h" _pxr_internal_ns_line REGEX "^#define PXR_INTERNAL_NS ")
    string(REGEX REPLACE "^#define PXR_INTERNAL_NS[ 	]+" "" DCC_PXR_TS_NS "${_pxr_internal_ns_line}")
    string(STRIP "${DCC_PXR_TS_NS}" DCC_PXR_TS_NS)
    if(NOT DCC_PXR_TS_NS)
        message(FATAL_ERROR "Could not read PXR_INTERNAL_NS from ${USD_ROOT}/include/pxr/pxr.h")
    endif()
else()
    set(DCC_PXR_TS_NS pxr)
endif()
message(STATUS "Typesystem PXR namespace: ${DCC_PXR_TS_NS}")

# shiboken6 emits protected static fields from module init, where they are unreachable.
# shiboken2 does not, and rejects modify-field, so suppress on Qt6 only.
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    set(DCC_TS_COLORWIDGET_FIELDS "<modify-field name=\"m_palette\" remove=\"yes\"/>")
else()
    set(DCC_TS_COLORWIDGET_FIELDS "")
endif()

set(DCC_QT_SVG_TARGETS Qt${QT_VERSION_MAJOR}::Svg)
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    list(APPEND DCC_QT_SVG_TARGETS Qt6::SvgWidgets)
endif()

set(DCC_QT_OPENGL_TARGETS Qt${QT_VERSION_MAJOR}::OpenGL)
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    list(APPEND DCC_QT_OPENGL_TARGETS Qt6::OpenGLWidgets)
endif()

# forbid usage of macros slots signals (use Q_SIGNALS/Q_SLOTS instead) to avoid collitions with python/pybind11
target_compile_definitions(Qt${QT_VERSION_MAJOR}::Core INTERFACE QT_NO_SIGNALS_SLOTS_KEYWORDS)

include_directories(${QT_INCLUDES})

# ADS 4.x (used by the Qt6 deps) renamed the package and target; Qt5 stays on 3.8.4.
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    find_package(qtadvanceddocking-qt6 REQUIRED)
    set(DCC_ADS_TARGET ads::qtadvanceddocking-qt6)
else()
    find_package(qtadvanceddocking REQUIRED)
    set(DCC_ADS_TARGET ads::qtadvanceddocking)
endif()

if(DCC_BUILD_EDITORIAL)
    find_package(OpenTimelineIO REQUIRED)
endif()

find_package(Imath REQUIRED)
find_package(OpenEXR REQUIRED)

if(DCC_PYSIDE_CMAKE_FIND)
    find_package(Shiboken${DCC_PYSIDE_VERSION} REQUIRED)
    if(DCC_PYSIDE_VERSION GREATER_EQUAL 6)
        # PySide6 ships the generator in a separate Shiboken6Tools package.
        find_package(Shiboken${DCC_PYSIDE_VERSION}Tools REQUIRED)
    endif()
    find_package(PySide${DCC_PYSIDE_VERSION} REQUIRED)
else()
    # PySideConfig shells out to pyside2_config.py and is Qt5 only.
    if(QT_VERSION_MAJOR GREATER_EQUAL 6)
        message(FATAL_ERROR "Qt6 requires DCC_PYSIDE_CMAKE_FIND=ON (PySideConfig is PySide2-only)")
    endif()
    include(PySideConfig)
endif()

# Target names carry the version: PySide6::pyside6, Shiboken6::shiboken6
set(DCC_PYSIDE_TARGET PySide${DCC_PYSIDE_VERSION}::pyside${DCC_PYSIDE_VERSION})
set(DCC_SHIBOKEN_LIB_TARGET Shiboken${DCC_PYSIDE_VERSION}::libshiboken)
set(DCC_SHIBOKEN_BIN_TARGET Shiboken${DCC_PYSIDE_VERSION}::shiboken${DCC_PYSIDE_VERSION})

find_package(sentry REQUIRED)
if(NOT DEFINED SHIBOKEN_CLANG_INSTALL_DIR)
    message(FATAL_ERROR "SHIBOKEN_CLANG_INSTALL_DIR not found")
endif()

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE OPENDCC_GIT_COMMIT
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

if(MSVC)
    add_definitions(-DOPENEXR_DLL)
endif()

find_package(OSL REQUIRED)
find_package(OpenSubdiv REQUIRED)


if(NOT ALEMBIC_FOUND AND DCC_INSTALL_ALEMBIC)
    find_package(Alembic REQUIRED)
endif()


find_package(OpenColorIO REQUIRED)
find_package(OpenImageIO REQUIRED)


if(DCC_KATANA_SUPPORT)
    string(REGEX REPLACE "^([0-9]+)\.([0-9]+)\.([0-9]+)$" "\\1_\\2" OIIO_MAJOR_MINOR ${OIIO_VERSION})
    string(REGEX REPLACE "^([0-9]+)\.([0-9]+)\.([0-9]+)$" "\\1_\\2" OCIO_MAJOR_MINOR ${OCIO_VERSION})

    configure_file("${CMAKE_CURRENT_LIST_DIR}/katana_compatibility_pch.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/katana_compatibility_pch.h" @ONLY)

    add_definitions(-DOPENDCC_KATANA_SUPPORT)
    add_library(_katana_compatibility INTERFACE)
    target_precompile_headers(_katana_compatibility INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/katana_compatibility_pch.h")
    export(TARGETS _katana_compatibility FILE _katana_compatibility-targets.cmake)
    install(TARGETS _katana_compatibility EXPORT _katana_compatibility-targets)
    install(
        EXPORT _katana_compatibility-targets
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/_katana_compatibility
        NAMESPACE opendcc::)
    link_libraries(_katana_compatibility)
endif()
