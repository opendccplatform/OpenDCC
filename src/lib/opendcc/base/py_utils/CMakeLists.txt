add_library(py_utils INTERFACE)
target_link_libraries(py_utils INTERFACE logging pybind11::pybind11 Python3::Python)

install(TARGETS py_utils EXPORT py_utils-targets)
install(
    DIRECTORY .
    DESTINATION "include/opendcc/base/py_utils"
    FILES_MATCHING
    PATTERN "*.h")
install(
    EXPORT py_utils-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/py_utils
    NAMESPACE opendcc::)
