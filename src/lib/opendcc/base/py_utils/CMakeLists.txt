opendcc_make_library(
    py_utils
    TYPE
    INTERFACE
    LIBRARY_DEPENDENCIES
    logging
    pybind11::pybind11
    Python3::Python)

install(
    DIRECTORY .
    DESTINATION "include/opendcc/base/py_utils"
    FILES_MATCHING
    PATTERN "*.h")
install(
    EXPORT py_utils-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/py_utils
    NAMESPACE opendcc::)
