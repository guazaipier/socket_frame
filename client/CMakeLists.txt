# cmake 3.15 支持 c++20
cmake_minimum_required(VERSION 3.15)

project(SOCKET_FRAME_SERVER)
set(CMAKE_CXX_STANDARD 20)
# add_definitions("-Wall -Wextra -Werror -pedantic -pthread")
add_definitions("-g -Wall")


add_executable(server main.cpp)

