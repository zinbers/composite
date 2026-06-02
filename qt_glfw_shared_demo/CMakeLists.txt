cmake_minimum_required(VERSION 3.20)
project(qt_glfw_shared_demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt requires these for auto-generated source files
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_subdirectory(common)
add_subdirectory(producer_qt)
add_subdirectory(consumer_glfw)
