set(_viewport_test_environment "PYTHONPATH=${PROJECT_SOURCE_DIR}/tests/test_utils" HD_DEFAULT_RENDERER=GL
                               QT_SCREEN_SCALE_FACTORS=1 QT_SCALE_FACTOR=1)

opendcc_add_python_test(ViewportSelectionTests SOURCE test_viewport_selection.py GUI ENV ${_viewport_test_environment})
opendcc_add_python_test(ViewportWidgetTests SOURCE test_viewport_widget.py GUI ENV ${_viewport_test_environment})
opendcc_add_python_test(ApplicationUiCallbackTests SOURCE test_application_ui_callbacks.py GUI ENV
                        ${_viewport_test_environment})
