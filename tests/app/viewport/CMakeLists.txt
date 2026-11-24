set(_viewport_test_environment "PYTHONPATH=${PROJECT_SOURCE_DIR}/tests/test_utils" HD_DEFAULT_RENDERER=GL
                               QT_SCREEN_SCALE_FACTORS=1 QT_SCALE_FACTOR=1)

opendcc_add_python_test(ViewportSelectionTests SOURCE test_viewport_selection.py GUI ENV ${_viewport_test_environment})
set(_viewport_ocio_test_environment
    ${_viewport_test_environment} "OCIO=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/colors.ocio"
    OCIO_ACTIVE_DISPLAYS=FixtureDisplay OCIO_ACTIVE_VIEWS=FixtureView:Raw)
opendcc_add_python_test(ViewportColorTests SOURCE test_viewport_colors.py GUI ENV ${_viewport_ocio_test_environment})
opendcc_add_python_test(ViewportOcioTests SOURCE test_viewport_ocio.py GUI ENV ${_viewport_ocio_test_environment})
opendcc_add_python_test(ViewportColorUtilsTests SOURCE test_viewport_color_utils.py ENV ${_viewport_test_environment})
opendcc_add_python_test(ViewportInstanceTests SOURCE test_viewport_instances.py GUI ENV ${_viewport_test_environment})
opendcc_add_python_test(ViewportWidgetTests SOURCE test_viewport_widget.py GUI ENV ${_viewport_test_environment})
opendcc_add_python_test(ViewportGridTests SOURCE test_viewport_grid.py GUI ENV ${_viewport_test_environment})
opendcc_add_python_test(ApplicationUiCallbackTests SOURCE test_application_ui_callbacks.py GUI ENV
                        ${_viewport_test_environment})

opendcc_add_python_test(ViewportPanelTests SOURCE test_viewport_panel.py GUI ENV ${_viewport_test_environment})

if(DCC_TESTS_GUI)
    set_tests_properties(ViewportPanelTests PROPERTIES TIMEOUT 180)
endif()
