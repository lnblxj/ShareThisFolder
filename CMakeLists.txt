cmake_minimum_required(VERSION 3.10.0)
project(ShareThisFolder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ShareThisFolder
    src/app/console.cpp
    src/app/main.cpp
    src/app/quick_launch.cpp
    src/app/settings_menu.cpp
    src/http/server.cpp
    src/net/network.cpp
    src/net/stun_client.cpp
    src/net/upnp_client.cpp
    src/util/i18n.cpp
    include/share_this_folder/app/console.h
    include/share_this_folder/app/quick_launch.h
    include/share_this_folder/app/settings_menu.h
    include/share_this_folder/http/server.h
    include/share_this_folder/net/network.h
    include/share_this_folder/net/stun_client.h
    include/share_this_folder/net/upnp_client.h
    include/share_this_folder/util/i18n.h
)

target_include_directories(ShareThisFolder PRIVATE include)

if(MSVC)
    target_compile_options(ShareThisFolder PRIVATE /utf-8)
endif()

if(WIN32)
    target_link_libraries(ShareThisFolder PRIVATE ws2_32 urlmon user32 shell32 advapi32)
endif()
