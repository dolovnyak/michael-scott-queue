cmake_minimum_required(VERSION 3.0)
project(michael_scott_queue)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-pthread")
#set(CMAKE_CXX_FLAGS "-pthread -fsanitize=thread -g -O3")
#set(CMAKE_CXX_FLAGS "-pthread -fsanitize=address -g -O3")

add_executable(${PROJECT_NAME}
        example/main.cpp
        )

find_package(Boost REQUIRED COMPONENTS thread program_options REQUIRED)

target_include_directories(${PROJECT_NAME} PUBLIC
        ${Boost_INCLUDE_DIR}
        include
        )

target_link_libraries(${PROJECT_NAME} LINK_PUBLIC ${Boost_LIBRARIES})

set_target_properties(${PROJECT_NAME}
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
        )