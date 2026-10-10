
file(GLOB LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

set(LIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(scan_lib STATIC ${LIB_SOURCES})


target_include_directories(scan_lib PUBLIC
    ${LIB_INCLUDE_DIR}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(scan_lib PUBLIC
    ${OpenCV_LIBS}
)
target_compile_definitions(scan_lib PUBLIC
    VIDEO_PATH="${CMAKE_SOURCE_DIR}/video.mp4"
)