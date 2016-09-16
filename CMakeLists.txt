cmake_minimum_required(VERSION 2.8.4)
project(leptjson_test C)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}  -pedantic -Wall")
endif ()

add_subdirectory(tutorial01)