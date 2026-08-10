opendcc_make_library(
    py_utils
    TYPE
    SHARED
    PUBLIC_HEADERS
    api.h
    lock.h
    error.h
    CPPFILES
    error.cpp
    PRIVATE_DEFINITIONS
    OPENDCC_PY_UTILS_EXPORT
    INCLUDE_DIRS
    ${Boost_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
    ${USD_INCLUDE_DIR}
    LIBRARY_DEPENDENCIES
    logging
    pybind11::pybind11
    ${Boost_PYTHON_LIBRARY}
    ${PYTHON_LIBRARY}
    VS_FOLDER
    base)

install(
    EXPORT py_utils-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/py_utils
    NAMESPACE opendcc::)
