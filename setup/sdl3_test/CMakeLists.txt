# Prueba del entorno: CMake + Ninja + clang (llvm-mingw) + windres + SDL3,
# compilada bajo una ruta con espacio ("Projecto Decompile").
cmake_minimum_required(VERSION 3.25)
project(sdl3_test LANGUAGES C RC)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(SDL3 REQUIRED CONFIG)

add_executable(hola_sdl3 main.c hello.rc)
target_link_libraries(hola_sdl3 PRIVATE SDL3::SDL3)

# Copia SDL3.dll junto al ejecutable.
add_custom_command(TARGET hola_sdl3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:hola_sdl3> $<TARGET_FILE_DIR:hola_sdl3>
    COMMAND_EXPAND_LISTS)
