cmake_minimum_required(VERSION 4.3)
project(WallpaperChanger)

set(CMAKE_CXX_STANDARD 20)

add_executable(WallpaperChanger WIN32 main.cpp)

target_link_libraries(WallpaperChanger user32 urlmon)

target_link_options(WallpaperChanger PRIVATE -static-libgcc -static-libstdc++)