# Native port of Metal Slug (and Metal Slug 4, 5, 3 and X, kept as regression targets of the
# shared Neo Geo runtime).
# Environment: . "C:\Projecto Decompile\MS1_Port\env.ps1"
#   cmake -S repo -B repo\build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build repo\build && ctest --test-dir repo\build
cmake_minimum_required(VERSION 3.25)
project(metal_slug_port LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# The recompiled program is generated at build time from the user's copy (PD_ROMS) and from the
# 68000 analysis (PD_ANALYSIS). None of that goes into the repo.
set(PD_ROMS "$ENV{PD_ROMS}" CACHE PATH "Folder with the user's Metal Slug copy (mslug_*, mslug\\, neogeo\\)")
set(PD_ANALYSIS "$ENV{PD_ROOT}/analysis" CACHE PATH "Metal Slug analysis output folder")
# The user's own icons for personal builds (<id>.png; src/CMakeLists.txt): outside the repo.
set(PD_ICON_DIR "$ENV{PD_ROOT}/assets" CACHE PATH "Folder with the user's icon images (<game id>.png), personal builds only")
# Regression targets, the other ports' workspaces (read only), next to this one: Metal Slug 4 in
# <parent>\MS4_Port, Metal Slug 5 in <parent>\MS5_Port, Metal Slug 3 in <parent>\MS3_Port, Metal
# Slug X in <parent> itself. mslug4.exe / mslug5.exe / mslug3.exe / mslugx.exe and their tests are
# only built when those copies are there.
get_filename_component(PD_PARENT "$ENV{PD_ROOT}/.." ABSOLUTE)
set(PD_MS4_ROMS "${PD_PARENT}/MS4_Port/roms" CACHE PATH "Folder with the Metal Slug 4 copy (gog\\, mslug4h\\, neogeo\\)")
set(PD_MS4_ANALYSIS "${PD_PARENT}/MS4_Port/analysis" CACHE PATH "Metal Slug 4 analysis output folder (MAME references, phase3\\program)")
set(PD_MS5_ROMS "${PD_PARENT}/MS5_Port/roms" CACHE PATH "Folder with the Metal Slug 5 copy (mslug5.zip, neogeo, reference files)")
set(PD_MS5_ANALYSIS "${PD_PARENT}/MS5_Port/analysis" CACHE PATH "Metal Slug 5 analysis output folder (MAME references)")
set(PD_MS3_ROMS "${PD_PARENT}/MS3_Port/roms" CACHE PATH "Folder with the Metal Slug 3 ROMs (mslug3_*)")
set(PD_MS3_ANALYSIS "${PD_PARENT}/MS3_Port/analysis" CACHE PATH "Metal Slug 3 analysis output folder (MAME references)")
set(PD_MSX_ROMS "${PD_PARENT}/roms" CACHE PATH "Folder with the Metal Slug X ROMs (mslugx_*)")
set(PD_MSX_ANALYSIS "${PD_PARENT}/analysis" CACHE PATH "Metal Slug X analysis output folder (MAME references)")
# Python from PATH (the environment puts ..\tools\python first), not from the registry.
set(Python3_FIND_REGISTRY NEVER)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

enable_testing()

add_subdirectory(external/musashi)
add_subdirectory(external/ymfm)
add_subdirectory(src)
add_subdirectory(tests/differential)
add_subdirectory(tests/lockstep)
