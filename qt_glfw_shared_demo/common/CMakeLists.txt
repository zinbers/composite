cmake_minimum_required(VERSION 3.20)
add_library(common STATIC
    src/ipc_bridge_linux.cpp
    src/dmabuf_channel.cpp
)
target_include_directories(common PUBLIC include)
# rt  → shm_open / shm_unlink / clock_gettime (older glibc)
# pthread → sem_open / sem_timedwait
target_link_libraries(common PUBLIC rt pthread)
