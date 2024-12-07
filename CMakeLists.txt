cmake_minimum_required(VERSION 3.23)
project(inter_process_communication C)

set(CMAKE_C_STANDARD 23)
set(PROJECT_HEADER ipc.h)
set(PROJECT_SOURCE ipc.c)

find_package(Threads REQUIRED)

add_executable(ipc ${PROJECT_HEADER} ${PROJECT_SOURCE})

target_link_libraries(ipc rt Threads::Threads)