cmake_minimum_required(VERSION 3.30)
project(static_collections)

set(CMAKE_CXX_STANDARD 20)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(static_collections INTERFACE)
target_include_directories(static_collections INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
