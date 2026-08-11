if(NOT DEFINED GRAPHVIZ_ROOT)
    message(WARNING "GRAPHVIZ_ROOT is not set")
endif()

find_path(
    GRAPHVIZ_INCLUDE_DIR
    NAMES graphviz/gvc.h
    HINTS ${GRAPHVIZ_ROOT}/include)

set(_graphviz_libs cdt cgraph gvc pathplan xdot)
set(GRAPHVIZ_LIBRARIES "")

find_program(
    GRAPHVIZ_dot_PATH dot
    HINTS ${GRAPHVIZ_ROOT}
    PATH_SUFFIXES bin NO_CACHE)

get_filename_component(GRAPHVIZ_BINARY_DIR ${GRAPHVIZ_dot_PATH} DIRECTORY)

foreach(_lib ${_graphviz_libs})
    find_library(
        ${_lib}_LIBRARY
        NAMES ${_lib}
        HINTS ${GRAPHVIZ_ROOT}
        PATH_SUFFIXES lib)
    if(${_lib}_LIBRARY)
        add_library(Graphviz::${_lib} STATIC IMPORTED GLOBAL)
        set_target_properties(Graphviz::${_lib} PROPERTIES IMPORTED_LOCATION "${${_lib}_LIBRARY}")

        if(WIN32)
            set_target_properties(Graphviz::${_lib} PROPERTIES IMPORTED_IMPLIB "${${_lib}_LIBRARY}")
        else()
            set_target_properties(Graphviz::${_lib} PROPERTIES IMPORTED_NO_SONAME TRUE)
        endif()

        list(APPEND GRAPHVIZ_LIBRARIES Graphviz::${_lib})
    endif()
endforeach()

get_filename_component(GRAPHVIZ_LIBRARY_DIR "${gvc_LIBRARY}" DIRECTORY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    Graphviz
    REQUIRED_VARS
        GRAPHVIZ_INCLUDE_DIR
        GRAPHVIZ_BINARY_DIR
        GRAPHVIZ_LIBRARY_DIR
        cdt_LIBRARY
        cgraph_LIBRARY
        pathplan_LIBRARY
        xdot_LIBRARY)

if(NOT TARGET Graphviz)
    add_library(Graphviz INTERFACE IMPORTED)
    set_target_properties(Graphviz PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GRAPHVIZ_INCLUDE_DIR};"
                                              INTERFACE_LINK_LIBRARIES "${GRAPHVIZ_LIBRARIES}")
    # graphviz headers declare data symbols such as Agdirected dllimport only under GVDLL
    if(WIN32)
        set_target_properties(Graphviz PROPERTIES INTERFACE_COMPILE_DEFINITIONS GVDLL)
    endif()
endif()
