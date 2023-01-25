cmake_minimum_required(VERSION 3.0)
project(michael_scott_queue)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-pthread -O2")

add_executable(michael_scott_queue
        main.cpp
        )

find_package(Boost REQUIRED COMPONENTS thread program_options REQUIRED)

target_include_directories(michael_scott_queue PUBLIC ${Boost_INCLUDE_DIR})

target_link_libraries(michael_scott_queue LINK_PUBLIC ${Boost_LIBRARIES})

set_target_properties(${PROJECT_NAME}
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
        )