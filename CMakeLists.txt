cmake_minimum_required(VERSION 3.10.0)
project(ShareThisFolder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ShareThisFolder
    src/main.cpp
    src/server.cpp
    src/i18n.cpp
    src/network.cpp
)

target_include_directories(ShareThisFolder PRIVATE include)

target_compile_options(ShareThisFolder PRIVATE /utf-8)

if(WIN32)
    target_link_libraries(ShareThisFolder PRIVATE ws2_32)
endif()
