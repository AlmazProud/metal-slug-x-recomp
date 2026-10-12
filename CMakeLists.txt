# Projecto Decompile: port nativo de Metal Slug X.
# Entorno: . "C:\Projecto Decompile\env.ps1"
#   cmake -S repo -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.25)
project(mslugx_port LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_subdirectory(externo/musashi)
add_subdirectory(tests/diferencial)
add_subdirectory(tests/lockstep)
