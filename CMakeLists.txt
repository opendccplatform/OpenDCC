cmake_minimum_required(VERSION 3.18)

if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CXX_FLAGS_HYBRID
        "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Od /Ob0"
        CACHE STRING "FLAGS" FORCE)
    set(CMAKE_C_FLAGS_HYBRID
        "${CMAKE_C_FLAGS_RELWITHDEBINFO} /Od /Ob0"
        CACHE STRING "FLAGS" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_HYBRID
        "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO}"
        CACHE STRING "FLAGS" FORCE)
    set(CMAKE_SHARED_LINKER_FLAGS_HYBRID
        "${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO}"
        CACHE STRING "CXX FLAGS" FORCE)
    set(CMAKE_CONFIGURATION_TYPES
        "Release;RelWithDebInfo;Debug;Hybrid"
        CACHE STRING "Configurations" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(OpenDCC VERSION 0.4.0.0)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake/defaults ${CMAKE_SOURCE_DIR}/cmake/modules
                      ${CMAKE_SOURCE_DIR}/cmake/macros)

include_directories(${PROJECT_SOURCE_DIR}/src/lib)

include(Version)
include(MakeTargets)
include(MakeFolder)
include(CXXDefaults)
include(Options)
include(Packages)
include(VsStudioUtils)
include(InstallCmakeConfig)
include(UpdateTranslations)

if(DCC_BUILD_TESTS)
    include(MakeTests)
    enable_testing()
endif()

add_subdirectory(src)
if(DCC_BUILD_TESTS)
    add_subdirectory(tests)
endif()

include(InstallDependencies)
include(InstallStartupConfigs)
