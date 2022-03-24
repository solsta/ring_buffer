cmake_minimum_required(VERSION 3.21)
project(ring_buffer C)

set(CMAKE_C_STANDARD 99)
link_libraries(pmemobj)

add_executable(ring_buffer main.c)
