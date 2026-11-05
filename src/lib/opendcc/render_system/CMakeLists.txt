get_filename_component(SOURCE_PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

opendcc_make_library(
    render_system
    PUBLIC_HEADERS
    api.h
    irender.h
    render_factory.h
    render_system.h
    CPPFILES
    render_factory.cpp
    render_system.cpp
    PRIVATE_INCLUDE_DIRS
    ${TBB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PYTHON_INCLUDE_DIR}
    ${SOURCE_PARENT_DIR}
    ${PROJECT_SOURCE_DIR}/src
    PRIVATE_DEFINITIONS
    OPENDCC_RENDER_SYSTEM_EXPORT)
if(NOT MSVC)
    target_link_libraries(render_system PRIVATE ${CMAKE_DL_LIBS})
endif()

install(FILES api.h irender.h render_factory.h render_system.h DESTINATION "include/opendcc/render_system/")
install(
    EXPORT render_system-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/render_system
    NAMESPACE opendcc::)

opendcc_make_python_module(
    render_system_py
    NO_BOOST_PYTHON
    PYMODULE_NAME
    rendersystem
    PYMODULE_SOURCES
    bind.cpp
    PRIVATE_INCLUDE_DIRS
    ${TBB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PYTHON_INCLUDE_DIR}
    ${SOURCE_PARENT_DIR}
    ${PROJECT_SOURCE_DIR}/src
    LIBRARY_DEPENDENCIES
    render_system
    PYMODULE_INSTALL_DIR
    opendcc)
