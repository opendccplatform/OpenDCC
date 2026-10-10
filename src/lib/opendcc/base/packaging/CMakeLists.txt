opendcc_make_library(
    packaging
    TYPE
    SHARED
    PUBLIC_HEADERS
    api.h
    package.h
    package_parser.h
    toml_parser.h
    package_provider.h
    filesystem_package_provider.h
    package_entry_point.h
    package_registry.h
    PRIVATE_HEADERS
    package_resolver.h
    core_properties.h
    package_loader.h
    CPPFILES
    package.cpp
    filesystem_package_provider.cpp
    toml_parser.cpp
    package_entry_point.cpp
    package_registry.cpp
    package_resolver.cpp
    core_properties.cpp
    package_loader.cpp
    LIBRARY_DEPENDENCIES
    logging
    test_runner
    vt
    sdf
    utils
    pybind11::pybind11
    PRIVATE_DEFINITIONS
    OPENDCC_PACKAGING_EXPORT
    PYMODULE_NAME
    packaging
    PYMODULE_SOURCES
    wrap/main.cpp
    PYMODULE_INSTALL_DIR
    opendcc
    VS_FOLDER
    base/packaging)

install(
    EXPORT packaging-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/packaging
    NAMESPACE opendcc::)
