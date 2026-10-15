set(TARGET_NAME app_config)

add_library(${TARGET_NAME} SHARED api.h config.h config.cpp)
make_folder(base/app_config ${TARGET_NAME})
target_compile_definitions(${TARGET_NAME} PRIVATE OPENDCC_BASE_APP_CONFIG_EXPORT)
target_link_libraries(${TARGET_NAME} logging)

install(TARGETS ${TARGET_NAME})
if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:${TARGET_NAME}>
        DESTINATION bin
        OPTIONAL)
endif()
install(FILES api.h config.h DESTINATION include/opendcc/base)

add_subdirectory(python_bindings)
