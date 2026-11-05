opendcc_make_shiboken_bindings(
    common_widgets_shiboken
    ${_verbose_shiboken_output}
    TARGET
    common_widgets
    SOURCE_DIR
    .
    VS_FOLDER
    widgets
    PRIVATE_DEFINITIONS
    QT_NOT_GEN)

target_include_directories(common_widgets_shiboken PRIVATE ${Boost_INCLUDE_DIRS}
                                                           ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty)
