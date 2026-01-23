cmake_minimum_required(VERSION 3.10)
project(TCPChatApp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

include_directories(${CMAKE_SOURCE_DIR}/common)
include_directories(${CMAKE_SOURCE_DIR}/Client-side)
include_directories(${CMAKE_SOURCE_DIR}/Server-side)

add_executable(server
    Server-side/Server_main.cpp
    Server-side/server_header_definition.cpp
)

add_executable(client
    Client-side/Client-main.cpp
)
