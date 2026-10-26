# Native port of Metal Slug X, on the shared runtime and frontend of the Metal Slug ports.
# Environment: . "C:\Projecto Decompile\env.ps1"
#   cmake -S repo -B repo\build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build repo\build && ctest --test-dir repo\build
cmake_minimum_required(VERSION 3.25)
project(mslugx_port LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# The recompiled program is generated at build time from the user's copy (PD_ROMS) and from the
# 68000 analysis (PD_ANALYSIS). None of that goes into the repo.
set(PD_ROMS "$ENV{PD_ROMS}" CACHE PATH "Folder with the user's Metal Slug X copy (mslugx_*, mame\\)")
set(PD_ANALYSIS "$ENV{PD_ROOT}/analysis" CACHE PATH "Metal Slug X analysis output folder (listings, MAME references)")
# The user's own icon for personal builds (mslugx.png; src/CMakeLists.txt): outside the repo.
set(PD_ICON_DIR "$ENV{PD_ROOT}/assets" CACHE PATH "Folder with the user's icon image (mslugx.png), personal builds only")
# Python from PATH (the environment puts ..\tools\python first), not from the registry.
set(Python3_FIND_REGISTRY NEVER)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

enable_testing()

add_subdirectory(external/musashi)
add_subdirectory(external/ymfm)
add_subdirectory(src)
add_subdirectory(tests/differential)
add_subdirectory(tests/lockstep)
