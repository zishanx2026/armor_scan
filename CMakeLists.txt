cmake_minimum_required(VERSION 3.10)
 project(scan
    VERSION 1.0
    DESCRIPTION "scan"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

add_subdirectory(lib)
add_subdirectory(app)