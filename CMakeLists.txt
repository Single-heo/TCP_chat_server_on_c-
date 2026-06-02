cmake_minimum_required(VERSION 3.10)
project(TCPChatApp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

include_directories(${CMAKE_SOURCE_DIR}/common)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)

# Build options
option(BUILD_CLIENT "Build client component" ON)
option(BUILD_SERVER "Build server component" ON)

# Ensure at least one target is selected
if(NOT BUILD_CLIENT AND NOT BUILD_SERVER)
    message(FATAL_ERROR "At least one of BUILD_CLIENT or BUILD_SERVER must be ON")
endif()

# Client
if(BUILD_CLIENT)
    include_directories(${CMAKE_SOURCE_DIR}/Client-side)

    add_executable(client
        Client-side/Client-main.cpp
    )
endif()

# Server
if(BUILD_SERVER AND EXISTS "${CMAKE_SOURCE_DIR}/Server-side")
    include_directories(${CMAKE_SOURCE_DIR}/Server-side)

    add_executable(server
        Server-side/Server_main.cpp
        Server-side/server_header_definition.cpp
    )

    target_include_directories(server PRIVATE ${SODIUM_INCLUDE_DIRS})
    target_link_libraries(server ${SODIUM_LIBRARIES})
endif()
