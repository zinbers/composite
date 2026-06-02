cmake_minimum_required(VERSION 3.20)

find_package(OpenGL REQUIRED)

# GLFW – prefer CMake config; fall back to pkg-config
find_package(glfw3 QUIET)
if (NOT glfw3_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW3 REQUIRED glfw3)
endif()

# GLEW – prefer CMake config; fall back to pkg-config
find_package(GLEW QUIET)
if (NOT GLEW_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLEW REQUIRED glew)
endif()

# EGL (required for DMA-BUF zero-copy path)
find_package(PkgConfig REQUIRED)
pkg_check_modules(EGL REQUIRED egl)

add_executable(consumer_glfw
    src/main.cpp
    src/shared_texture_receiver.cpp
    src/compositor.cpp
    src/egl_dmabuf_import.cpp
    src/dmabuf_glfw_receiver.cpp
)

target_include_directories(consumer_glfw PRIVATE src ${EGL_INCLUDE_DIRS})

# Link libraries (handle both CMake-config and pkg-config paths)
target_link_libraries(consumer_glfw
    PRIVATE
        OpenGL::GL
        common
        $<IF:$<TARGET_EXISTS:glfw>,glfw,${GLFW3_LIBRARIES}>
        $<IF:$<TARGET_EXISTS:GLEW::GLEW>,GLEW::GLEW,${GLEW_LIBRARIES}>
        ${EGL_LIBRARIES}
)

if (NOT TARGET glfw)
    target_include_directories(consumer_glfw PRIVATE ${GLFW3_INCLUDE_DIRS})
endif()
if (NOT TARGET GLEW::GLEW)
    target_include_directories(consumer_glfw PRIVATE ${GLEW_INCLUDE_DIRS})
endif()
