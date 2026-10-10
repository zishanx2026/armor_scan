set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
add_executable(scan ${APP_SOURCES})
target_link_libraries(scan PRIVATE scan_lib)
add_custom_command(TARGET scan POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/video.mp4
        $<TARGET_FILE_DIR:scan>/video.mp4
)