target_link_libraries(opendcc.ui.scene_outliner PRIVATE test_runner)

opendcc_add_python_test(scene_outliner_standalone SOURCE test_standalone.py STANDALONE ARGS "${CMAKE_INSTALL_PREFIX}")

opendcc_add_cpp_test(
    scene_outliner_model
    SOURCE
    model_tests.cpp
    TARGET
    opendcc.ui.scene_outliner
    SUITES
    SceneOutlinerModel)
opendcc_add_python_test(scene_outliner_extensions SOURCE test_extensions.py GUI)
foreach(
    _suite IN
    ITEMS model_callbacks
          source_updates
          adapter_lifetime
          filtering
          fetching
          expansion
          drag_view)
    opendcc_add_python_test(scene_outliner_${_suite} SOURCE test_${_suite}.py GUI)
endforeach()
foreach(_suite IN ITEMS drag_data drag_gestures)
    opendcc_add_python_test(scene_outliner_${_suite} SOURCE test_${_suite}.py GUI ENV "QT_QPA_PLATFORM=offscreen")
endforeach()
target_sources(opendcc.ui.scene_outliner PRIVATE proxy_model_tests.cpp notice_batcher_tests.cpp)
if(DCC_TESTS_GUI)
    opendcc_add_cpp_test(
        scene_outliner_widget
        SOURCE
        widget_tests.cpp
        TARGET
        opendcc.ui.scene_outliner
        SUITES
        SceneOutlinerWidget)
    set_tests_properties(scene_outliner_widget PROPERTIES RESOURCE_LOCK OpenDCCGui)
endif()
