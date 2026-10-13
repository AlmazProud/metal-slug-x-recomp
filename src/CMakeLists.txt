# Port core: CPU (dispatcher + fallback), recompiled program and Neo Geo runtime.

add_library(cpu68k STATIC cpu/dispatcher.c cpu/musashi_interpreter.c)
target_include_directories(cpu68k PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpu)
target_link_libraries(cpu68k PUBLIC musashi)

# Musashi's base 68000 cycle table, used by the generator.
add_executable(dump_cycles ${PROJECT_SOURCE_DIR}/tools/recomp68k/dump_cycles.c)
target_link_libraries(dump_cycles PRIVATE musashi)

# Query for the interrupt sampling rules (tools/verify_sampling.py).
add_executable(sampling_classes ${PROJECT_SOURCE_DIR}/tools/recomp68k/sampling_classes.c)
target_link_libraries(sampling_classes PRIVATE cpu68k)

# The recompiled program is generated from the user's copy and the analysis listings. Without it
# (PD_RECOMPILED=OFF, or no ROMs / listings) the port runs the 68000 in the interpreter only:
# that build contains no code derived from the game and is the one that can be distributed.
option(PD_RECOMPILED "Build the recompiled program from the user's copy (personal builds)" ON)
set(HAVE_PROGRAM OFF)
if(PD_RECOMPILED AND EXISTS "${PD_ROMS}/mslugx_game_m68k" AND EXISTS "${PD_ANALYSIS}/listing_bios.txt")
    set(HAVE_PROGRAM ON)
elseif(PD_RECOMPILED)
    message(STATUS "Recompiled program disabled: ROMs (${PD_ROMS}) or analysis listings (${PD_ANALYSIS}) are missing")
endif()

set(PD_HAVE_PROGRAM ${HAVE_PROGRAM} CACHE INTERNAL "The recompiled program is built")

if(HAVE_PROGRAM)
set(CYCLES_BIN ${CMAKE_CURRENT_BINARY_DIR}/cycles_68000.bin)
add_custom_command(OUTPUT ${CYCLES_BIN} COMMAND dump_cycles ${CYCLES_BIN} DEPENDS dump_cycles VERBATIM)

set(PROG_DIR ${CMAKE_CURRENT_BINARY_DIR}/program)
set(PROG_C ${PROG_DIR}/program_registry.c)
foreach(n RANGE 0 15)
    string(LENGTH "${n}" len)
    if(len EQUAL 1)
        set(num "00${n}")
    else()
        set(num "0${n}")
    endif()
    list(APPEND PROG_C ${PROG_DIR}/program_${num}.c)
endforeach()

set(COVERAGE ${PD_ANALYSIS}/coverage.json)
if(EXISTS "${PD_ANALYSIS}/coverage_mission1.json")
    list(APPEND COVERAGE ${PD_ANALYSIS}/coverage_mission1.json)
endif()

file(GLOB RECOMP_PY ${PROJECT_SOURCE_DIR}/tools/recomp68k/*.py)
add_custom_command(
    OUTPUT ${PROG_C}
    COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/recomp68k/program.py
            --roms ${PD_ROMS}
            --listings ${PD_ANALYSIS}/listing_p1.txt ${PD_ANALYSIS}/listing_bios.txt
            --coverage ${COVERAGE}
            --cycles ${CYCLES_BIN} --output ${PROG_DIR} --files 16
    DEPENDS ${RECOMP_PY} ${CYCLES_BIN}
    COMMENT "Recompiling the user's P1 and BIOS to C"
    VERBATIM)

# Code generated from the user's copy: it lives only in build/.
add_library(recompiled_program STATIC ${PROG_C})
target_link_libraries(recompiled_program PUBLIC cpu68k)
target_compile_options(recompiled_program PRIVATE -O1 -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-label)
else()
    message(STATUS "Interpreter-only build (no code derived from the game)")
    add_library(recompiled_program STATIC cpu/program_none.c)
    target_link_libraries(recompiled_program PUBLIC cpu68k)
endif()

# Neo Geo hardware runtime (Phase 3).
add_library(neogeo STATIC
    neogeo/bus.c neogeo/savestate.c neogeo/machine.c neogeo/roms.c neogeo/rtc.c neogeo/sha256.c
    neogeo/hashes.c neogeo/unzip.c
    neogeo/sound.c neogeo/video.c neogeo/z80_impl.c neogeo/ym2610.cpp)
target_include_directories(neogeo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/neogeo
                           PRIVATE ${PROJECT_SOURCE_DIR}/external/chips)
target_link_libraries(neogeo PUBLIC cpu68k ymfm)
set_target_properties(neogeo PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(neogeo PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra>)
# z80.h (third party) produces its own warnings; it is compiled without them.
set_source_files_properties(neogeo/z80_impl.c PROPERTIES COMPILE_OPTIONS "-w")

# Port executable (SDL3).
find_package(SDL3 CONFIG)
if(NOT SDL3_FOUND)
    message(STATUS "Port disabled: SDL3 not found (CMAKE_PREFIX_PATH)")
    return()
endif()
set(PORT_SOURCES port/main.c port/headless.c port/window.c port/config.c port/debug.c port/font.c port/find_game.c port/lang.c port/profile.c port/cheats.c port/logo.c port/menu.c port/sounds.c port/profile_sounds.c)
if(WIN32)
    enable_language(RC)
    list(APPEND PORT_SOURCES port/mslugx.rc)
endif()
add_executable(mslugx WIN32 ${PORT_SOURCES})
target_link_libraries(mslugx PRIVATE neogeo recompiled_program SDL3::SDL3)
target_compile_options(mslugx PRIVATE -Wall -Wextra)
# C++ runtime (for ymfm) linked statically: the executable only needs SDL3.dll.
target_link_options(mslugx PRIVATE -static-libstdc++ -static-libgcc)
set_target_properties(mslugx PROPERTIES LINKER_LANGUAGE CXX)
add_custom_command(TARGET mslugx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:mslugx> $<TARGET_FILE_DIR:mslugx>
    COMMAND_EXPAND_LISTS)

# Distribution (Phase 5): cmake --install build --prefix <folder> installs the executable and SDL3.dll.
# The executable contains code translated from the user's copy: it is for personal use.
install(TARGETS mslugx RUNTIME DESTINATION .)
install(FILES $<TARGET_RUNTIME_DLLS:mslugx> DESTINATION .)

# Tests: they need the user's copy (PD_ROMS).
if(NOT EXISTS "${PD_ROMS}/mslugx_game_m68k")
    message(STATUS "Port tests disabled: the user's copy (PD_ROMS) is missing")
    return()
endif()

# Phase 9: a MAME set (the one tools/extract_roms.py rebuilds in PD_ROMS/mame), as loose files and
# as zips (made at test time in the build folder), loads exactly the same data as the PC release
# files; and only the game's zip finds neogeo.zip next to it.
if(EXISTS "${PD_ROMS}/mame/mslugx" AND EXISTS "${PD_ROMS}/mame/neogeo")
    add_test(NAME roms_mame_folder COMMAND mslugx --roms ${PD_ROMS} --headless --frames 1 --no-audio)
    set_tests_properties(roms_mame_folder PROPERTIES ENVIRONMENT "PD_COMPARE_ROMS=${PD_ROMS}/mame")
    set(ZIPS ${CMAKE_CURRENT_BINARY_DIR}/mame_zips)
    add_test(NAME roms_mame_zips_make
             COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tests/loader/zip_set.py ${PD_ROMS}/mame ${ZIPS})
    set_tests_properties(roms_mame_zips_make PROPERTIES FIXTURES_SETUP mame_zips)
    add_test(NAME roms_mame_zip COMMAND mslugx --roms ${PD_ROMS} --headless --frames 1 --no-audio)
    set_tests_properties(roms_mame_zip PROPERTIES ENVIRONMENT "PD_COMPARE_ROMS=${ZIPS}/mslugx.zip"
                         FIXTURES_REQUIRED mame_zips)
endif()

# Phase 5: a save state, loaded into a reset machine, reproduces RAM, image and audio.
add_test(NAME savestate_roundtrip
         COMMAND mslugx --roms ${PD_ROMS} --headless --frames 3000 --test-state 600)
# Phase 6B: the sound menu is audible and, after closing it, the game stays identical.
add_test(NAME sound_menu
         COMMAND mslugx --roms ${PD_ROMS} --headless --frames 3000 --test-sounds)

# Phase 4: the user's recorded game (Mission 1) replayed against MAME, with no divergences in RAM,
# palette or image. The MAME references and the inputs live in PD_ANALYSIS/phase4 (outside the repo;
# how to generate them: tools/verify_mission.py).
if(EXISTS "${PD_ANALYSIS}/phase4/mission1_fingerprints_mame.txt" AND EXISTS "${PD_ANALYSIS}/phase4/mission1_inputs.txt")
    add_test(NAME mission1_vs_mame
             COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                     --exe $<TARGET_FILE:mslugx> --roms ${PD_ROMS}
                     --inputs ${PD_ANALYSIS}/phase4/mission1_inputs.txt
                     --reference ${PD_ANALYSIS}/phase4/mission1_fingerprints_mame.txt
                     --frames 11970 --date 2026-10-11-20-28-31-0
                     --output ${CMAKE_CURRENT_BINARY_DIR}/mission1_fingerprints_port.txt)
    # Phase 9: the same with the interpreter only (the distributable build's CPU path).
    add_test(NAME mission1_interpreter_vs_mame
             COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                     --exe $<TARGET_FILE:mslugx> --roms ${PD_ROMS}
                     --inputs ${PD_ANALYSIS}/phase4/mission1_inputs.txt
                     --reference ${PD_ANALYSIS}/phase4/mission1_fingerprints_mame.txt
                     --frames 11970 --date 2026-10-11-20-28-31-0
                     --output ${CMAKE_CURRENT_BINARY_DIR}/mission1_interpreter_fingerprints_port.txt)
    set_tests_properties(mission1_interpreter_vs_mame PROPERTIES ENVIRONMENT "PD_INTERPRETER=1")
endif()

# Phase 8: scripted runs of the developers' stage select against MAME (tools/stage_run.py writes
# the references to PD_ANALYSIS/phase8, outside the repo). Stages 2 and 6 still have known
# differences (docs/PHASE8_STAGES.md) and are not tests yet.
foreach(stage 03 04 05 07 08 09 10 11 12 13)
    set(base ${PD_ANALYSIS}/phase8/stage${stage})
    if(EXISTS "${base}_fingerprints.txt" AND EXISTS "${base}_date.txt" AND EXISTS "${base}_frames.txt")
        file(READ "${base}_date.txt" date)
        file(READ "${base}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        add_test(NAME stage${stage}_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslugx> --roms ${PD_ROMS} --devmode --dips 8000
                         --inputs ${base}_inputs.txt --reference ${base}_fingerprints.txt
                         --frames ${frames} --date ${date}
                         --output ${CMAKE_CURRENT_BINARY_DIR}/stage${stage}_fingerprints_port.txt)
    endif()
endforeach()
