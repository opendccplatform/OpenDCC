set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(WIN32)
    set(qtsingleapplication_platform_specific_cpp qtlockedfile_win.cpp)
else()
    set(qtsingleapplication_platform_specific_cpp qtlockedfile_unix.cpp)
endif()

set(qtsingleapplication_src src/qtlocalpeer.cpp src/qtlockedfile.cpp src/${qtsingleapplication_platform_specific_cpp}
                            src/qtsingleapplication.cpp)

set(qtsingleapplication_headers src/qtlocalpeer.h src/qtsingleapplication.h src/qtlockedfile.h)
set(CMAKE_AUTOMOC ON)
source_group("Headers" FILES ${qtsingleapplication_headers})
source_group("MOC" FILES ${qtsingleapplication_moc})

include_directories(src)

add_library(qtsingleapplication ${qtsingleapplication_headers} ${qtsingleapplication_moc} ${qtsingleapplication_src})
target_link_libraries(
    qtsingleapplication
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network)
