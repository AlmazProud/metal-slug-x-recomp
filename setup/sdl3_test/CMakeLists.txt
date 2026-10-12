# Environment test: CMake + Ninja + clang (llvm-mingw) + windres + SDL3,
# built under a path containing a space ("Projecto Decompile").
cmake_minimum_required(VERSION 3.25)
project(sdl3_test LANGUAGES C RC)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(SDL3 REQUIRED CONFIG)

add_executable(hello_sdl3 main.c hello.rc)
target_link_libraries(hello_sdl3 PRIVATE SDL3::SDL3)

# Copies SDL3.dll next to the executable.
add_custom_command(TARGET hello_sdl3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:hello_sdl3> $<TARGET_FILE_DIR:hello_sdl3>
    COMMAND_EXPAND_LISTS)
