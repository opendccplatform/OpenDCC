qt_add_resources(resources ${PROJECT_SOURCE_DIR}/icons/icons.qrc)

opendcc_make_executable(
    dcc_base
    LINUX_LAUNCHER
    QT_AUTOMOC
    VS_DEBUG_FROM_BUILD_TREE
    CPPFILES
    main.cpp
    ${PROJECT_SOURCE_DIR}/icons/opendcc_resources.rc
    ${resources}
    LIBRARY_DEPENDENCIES
    opendcc_lib
    common_widgets
    ${CMAKE_THREAD_LIBS_INIT}
    INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/src/lib
    ${TBB_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
    ${PXR_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS})
