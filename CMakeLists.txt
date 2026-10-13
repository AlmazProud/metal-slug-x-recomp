# Projecto Decompile: native port of Metal Slug X.
# Environment: . "C:\Projecto Decompile\env.ps1"
#   cmake -S repo -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.25)
project(mslugx_port LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# The recompiled program is generated at build time from the user's copy (PD_ROMS) and from the
# Phase 1 analysis (PD_ANALYSIS). None of that goes into the repo.
set(PD_ROMS "$ENV{PD_ROMS}" CACHE PATH "Folder with the user's ROMs (mslugx_*)")
set(PD_ANALYSIS "$ENV{PD_ROOT}/analysis" CACHE PATH "Analysis output folder (listing_p1.txt)")
# Python from PATH (the environment puts ..	oolspython first), not from the registry.
set(Python3_FIND_REGISTRY NEVER)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

enable_testing()

add_subdirectory(external/musashi)
add_subdirectory(external/ymfm)
add_subdirectory(src)
add_subdirectory(tests/differential)
add_subdirectory(tests/lockstep)
