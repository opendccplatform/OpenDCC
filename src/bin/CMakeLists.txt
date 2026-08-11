set(WIN32_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/win32.manifest")
add_subdirectory(dcc_base)
add_subdirectory(usd_ipc_broker)

if(DCC_BUILD_RENDER_VIEW)
    add_subdirectory(render_view)
    if(USD_VERSION VERSION_GREATER 0.19.11)
        add_subdirectory(usd_render)
    endif()
endif()

if(DCC_BUILD_CRASH_REPORTER)
    add_subdirectory(crash_reporter)
endif()
