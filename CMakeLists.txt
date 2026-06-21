cmake_minimum_required(VERSION 3.16)

project(TCPChatApp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
)

option(BUILD_CLIENT "Build client" ON)
option(BUILD_SERVER "Build server" ON)

find_package(PkgConfig REQUIRED)
find_package(simpleini REQUIRED)

if(BUILD_SERVER)
    pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
    # Swapped find_package for pkg_check_modules to locate the package correctly
    pkg_check_modules(SIMPLEINI REQUIRED IMPORTED_TARGET simpleini)
endif()

#
# Common library
#

add_library(common STATIC
    common/Logger/logger.cpp
)

target_include_directories(common
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

#
# Client
#

if(BUILD_CLIENT)

    add_executable(client
        Client-side/Client-main.cpp
    )

    target_link_libraries(client
        PRIVATE
            common
    )

endif()

#
# Server
#

if(BUILD_SERVER)

    add_executable(server
        Server-side/Server_main.cpp
        Server-side/server_header_definition.cpp
    )

    target_link_libraries(server
        PRIVATE
            common
            PkgConfig::SODIUM
            PkgConfig::SIMPLEINI  # Links the imported pkg-config target safely
    )

endif()