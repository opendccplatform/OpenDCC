set(TARGET_NAME color_theme)

opendcc_make_library(
    ${TARGET_NAME}
    CPPFILES
    api.h
    color_theme.h
    color_theme.cpp
    VS_FOLDER
    opendcc/ui
    PRIVATE_DEFINITIONS
    OPENDCC_COLOR_THEME_EXPORT)

install(
    EXPORT color_theme-targets
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/color_theme
    NAMESPACE opendcc::)

add_subdirectory(wrap_color_theme)
