cmake_minimum_required(VERSION 3.0)
project(michael_scott_queue)

set(CMAKE_CXX_STANDARD 17)

add_executable(michael_scott_queue
        main.cpp
        )

set(CMAKE_CXX_FLAGS -pthread)
