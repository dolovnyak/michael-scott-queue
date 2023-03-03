cmake_minimum_required(VERSION 3.0)
project(michael-scott-queue)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-pthread -D MSQ_DEBUG")
#set(CMAKE_CXX_FLAGS "-pthread -fsanitize=thread -g -O3")
#set(CMAKE_CXX_FLAGS "-pthread -fsanitize=address -g -O3")

# example
set(EXAMPLE ${PROJECT_NAME}-example)
add_executable(${EXAMPLE}
        example/main.cpp
        )

# in the example, two queues are launched, this and the boost queue
find_package(Boost REQUIRED COMPONENTS thread program_options REQUIRED)

target_include_directories(${EXAMPLE} PUBLIC
        ${Boost_INCLUDE_DIR}
        include
        )

target_link_libraries(${EXAMPLE} LINK_PUBLIC ${Boost_LIBRARIES})

set_target_properties(${EXAMPLE}
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
        )