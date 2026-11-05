opendcc_make_shiboken_bindings(
    timeline_widget_py
    ${_verbose_shiboken_output}
    TARGET
    timeline_widget
    SOURCE_DIR
    .
    LEGACY_HEADER_FILEPATH
    timeline_widget_py.h
    VS_FOLDER
    widgets
    PRIVATE_DEFINITIONS
    QT_NOT_GEN)

target_include_directories(timeline_widget_py PRIVATE ${PROJECT_SOURCE_DIR}/src/lib/thirdparty)
