set(TARGET_NAME app_config)

opendcc_make_library(
    ${TARGET_NAME}
    CPPFILES
    api.h
    config.h
    config.cpp
    VS_FOLDER
    base/app_config
    PRIVATE_DEFINITIONS
    OPENDCC_BASE_APP_CONFIG_EXPORT
    LIBRARY_DEPENDENCIES
    logging)

install(FILES api.h config.h DESTINATION include/opendcc/base)

add_subdirectory(python_bindings)
