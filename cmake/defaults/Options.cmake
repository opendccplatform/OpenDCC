option(DCC_BUILD_ANIM_ENGINE "" ON)
option(DCC_BUILD_EXPRESSIONS_ENGINE "" ON)

option(DCC_BUILD_RENDER_VIEW "" ON)
option(DCC_RENDER_VIEW_WIN_GUI_EXECUTABLE "" ON)

option(DCC_EMBEDDED_PYTHON_HOME "" ON)

option(DCC_PYSIDE_CMAKE_FIND
       "PySide2 could be build via setup.py build or cmake. Option will look for cmake exports file" OFF)
option(DCC_BUILD_ARNOLD_SUPPORT "" ON)
option(DCC_BUILD_TESTS "" OFF)
# needs the crashpad client libraries and headers, which only a crashpad-backend sentry-native ships
option(DCC_BUILD_CRASH_REPORTER "Build the crash reporter" ON)


option(DCC_USD_FALLBACK_PROXY_BUILD_ARNOLD_USD "" ON)
option(DCC_USD_FALLBACK_PROXY_BUILD_MOONRAY "" OFF)
option(DCC_NODE_EDITOR "" ON)
option(DCC_USD_FALLBACK_PROXY_BUILD_CYCLES "" ON)
option(DCC_USE_PYTHON_3 "" ON)
option(DCC_USE_PTEX "" OFF)
option(DCC_BUILD_RENDERMAN_SUPPORT "" OFF)
option(DCC_BUILD_CYCLES_SUPPORT "" OFF)

option(DCC_HOUDINI_SUPPORT "" OFF)
option(DCC_KATANA_SUPPORT "" OFF)
set(DCC_DEFAULT_CONFIG
    "opendcc.usd_editor.toml"
    CACHE STRING "Default .toml config file")
set(DCC_ADDITIONAL_CONFIGS
    ""
    CACHE STRING "List of additional .toml config files")

option(DCC_BUILD_BULLET_PHYSICS "" ON)
option(DCC_VERBOSE_SHIBOKEN_OUTPUT "Enable rich debugging shiboken generator output." OFF)
option(DCC_DEBUG_BUILD "Enable debug mode" OFF)
if(DCC_VERBOSE_SHIBOKEN_OUTPUT)
    set(_verbose_shiboken_output "VERBOSE")
else()
    set(_verbose_shiboken_output "")
endif()

if(CMAKE_COMPILER_IS_GNUCC)
    option(WITH_LINKER_GOLD "Use ld.gold linker which is usually faster than ld.bfd" ON)
    mark_as_advanced(WITH_LINKER_GOLD)
endif()

if("${CMAKE_GENERATOR}" MATCHES "Ninja")
    option(WITH_WINDOWS_SCCACHE "Use sccache to speed up builds (Ninja builder only)" OFF)
    mark_as_advanced(WITH_WINDOWS_SCCACHE)
endif()

option(
    DCC_USE_HYDRA_FRAMING_API
    "Use new (starting with USD 21.08) Hydra Framing API. Unfortunately not all render delegates support it yet. Use old SetRenderViewport by default"
    OFF)

option(DCC_TESTS_USD_RENDER
       "Test that starts USD render in out of process, renders to disk and compares it with reference image" OFF)
set(DCC_LANG
    "all"
    CACHE STRING "Specify language to build app with (en, all)")


option(DCC_BUILD_HYDRA_OP "" OFF)

# Packages
option(DCC_PACKAGE_OPENDCC_USD_EDITOR_UV_EDITOR "opendcc.usd_editor.uv_editor" ON)
option(DCC_PACKAGE_OPENDCC_USD_EDITOR_PAINT_PRIMVAR_TOOL "opendcc.usd_editor.paint_primvar_tool" ON)
option(DCC_PACKAGE_OPENDCC_USD_EDITOR_SCULPT_TOOL "opendcc.usd_editor.sculpt_tool" ON)
option(DCC_PACKAGE_OPENDCC_USD_EDITOR_POINT_INSTANCER_TOOL "opendcc.usd_editor.point_instancer_tool" ON)
option(DCC_PACKAGE_OPENDCC_USD_EDITOR_TEXTURE_PAINT_TOOL "opendcc.usd_editor.texture_paint" ON)
option(DCC_PACKAGE_OPENDCC_USD_EDITOR_BEZIER_TOOL "opendcc.usd_editor.bezier_tool" ON)
option(DCC_PACKAGE_OPENDCC_USD_EDITOR_LIGHT_OUTLINER "opendcc.usd_editor.light_outliner" ON)
option(DCC_PACKAGE_OPENDCC_USD_EDITOR_LIGHT_LINKING_EDITOR "opendcc.usd_editor.light_linking_editor" ON)
option(DCC_PACKAGE_OPENDCC_USD_EDITOR_LIVE_SHARE "opendcc.usd_editor.live_share" ON)

option(DCC_PACKAGE_OPENDCC_HYDRA_OP_SCENE_GRAPH "opendcc.hydra_op_scene_graph" ON)
option(DCC_PACKAGE_OPENDCC_HYDRA_OP_ATTRIBUTE_VIEW "opendcc.hydra_op_attribute_view" ON)
