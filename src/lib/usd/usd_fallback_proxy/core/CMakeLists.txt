add_definitions(-DNOMINMAX -DFALLBACK_PROXY_EXPORT)

set(HEADERS
    api.h
    usd_property_proxy.h
    usd_prim_fallback_proxy.h
    source_registry.h
    property_factory.h
    usd_prim_property_factory.h
    property_gatherer.h
    usd_fallback_proxy_watcher.h
    usd_property_source.h
    utils.h)

set(SRC
    usd_property_proxy.cpp
    usd_prim_fallback_proxy.cpp
    source_registry.cpp
    usd_prim_property_factory.cpp
    property_gatherer.cpp
    usd_fallback_proxy_watcher.cpp
    utils.cpp
    module_deps.cpp)

add_library(usd_fallback_proxy SHARED ${HEADERS} ${SRC})

target_include_directories(
    usd_fallback_proxy
    PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/lib/>" "$<INSTALL_INTERFACE:include>"
           "$<BUILD_INTERFACE:${USD_FALLBACK_PROXY_INCLUDE_DIR}>" "$<BUILD_INTERFACE:${USD_INCLUDE_DIR}>"
           "$<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}>" "$<BUILD_INTERFACE:${PYTHON_INCLUDE_DIRS}>")

target_link_libraries(
    usd_fallback_proxy
    usd
    ar
    sdr
    sdf
    usdUtils
    usdShade
    usdUIExt
    utils)
# The 'ndr' library was merged into 'sdr' in USD 25.05 and no longer exists there;
# link it only when the imported target is present (i.e. older USD like 25.02a).
if(TARGET ndr)
    target_link_libraries(usd_fallback_proxy ndr)
endif()

install(
    TARGETS usd_fallback_proxy
    EXPORT usd_fallback_proxy-targets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION plugin/usd
    ARCHIVE DESTINATION plugin/usd)

install(
    EXPORT usd_fallback_proxy-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/usd_fallback_proxy
    NAMESPACE opendcc::)
set(USD_FALLBACK_PROXY_CORE_LIBRARY
    ${CMAKE_INSTALL_PREFIX}/plugin/usd/usd_fallback_proxy.lib
    CACHE INTERNAL "Usd Fallback Proxy Core Library")

if(WIN32)
    file(RELATIVE_PATH LIBRARY_LOCATION ${CMAKE_INSTALL_PREFIX}/plugin/usd/usd_fallback_proxy/resources/..
         ${CMAKE_INSTALL_PREFIX}/bin)
else()
    file(RELATIVE_PATH LIBRARY_LOCATION ${CMAKE_INSTALL_PREFIX}/plugin/usd/usd_fallback_proxy/resources/..
         ${CMAKE_INSTALL_PREFIX}/plugin/usd)
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.json.in" "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json" @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json
        DESTINATION ${CMAKE_INSTALL_PREFIX}/plugin/usd/usd_fallback_proxy/resources/)

if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:usd_fallback_proxy>
        DESTINATION bin
        OPTIONAL)
endif()
