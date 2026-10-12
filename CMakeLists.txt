# Projecto Decompile: port nativo de Metal Slug X.
# Entorno: . "C:\Projecto Decompile\env.ps1"
#   cmake -S repo -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.25)
project(mslugx_port LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# El programa recompilado se genera al compilar a partir de la copia del usuario (PD_ROMS) y del
# análisis de la Fase 1 (PD_ANALYSIS). Nada de eso entra al repo.
set(PD_ROMS "$ENV{PD_ROMS}" CACHE PATH "Carpeta con los ROMs del usuario (mslugx_*)")
set(PD_ANALYSIS "$ENV{PD_ROOT}/analysis" CACHE PATH "Carpeta de output del análisis (listing_p1.txt)")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

enable_testing()

add_subdirectory(external/musashi)
add_subdirectory(external/ymfm)
add_subdirectory(src)
add_subdirectory(tests/differential)
add_subdirectory(tests/lockstep)
