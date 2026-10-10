cmake_minimum_required(VERSION 3.20)
project(WallpaperChanger CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# WIN32 builds a windowed app (no console), replacing the need for -mwindows
add_executable(WallpaperChanger WIN32 main.cpp)

target_link_libraries(WallpaperChanger PRIVATE winhttp urlmon)

# Bundle MinGW's runtime into the exe so it doesn't need extra DLLs
if(MINGW)
    target_link_options(WallpaperChanger PRIVATE -static-libgcc -static-libstdc++)
endif()