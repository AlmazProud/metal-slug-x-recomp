# Port core: CPU (dispatcher + fallback), the recompiled program, the Neo Geo runtime and the
# executable mslugx.exe (the shared frontend plus src/games/mslugx).

add_library(cpu68k STATIC cpu/dispatcher.c cpu/musashi_interpreter.c)
target_include_directories(cpu68k PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpu)
target_link_libraries(cpu68k PUBLIC musashi)

# Musashi's base 68000 cycle table, used by the generator.
add_executable(dump_cycles ${PROJECT_SOURCE_DIR}/tools/recomp68k/dump_cycles.c)
target_link_libraries(dump_cycles PRIVATE musashi)

# Query for the interrupt sampling rules (tools/verify_sampling.py).
add_executable(sampling_classes ${PROJECT_SOURCE_DIR}/tools/recomp68k/sampling_classes.c)
target_link_libraries(sampling_classes PRIVATE cpu68k)

# YM2610 probe player: the port's chip on a tools/mame/ym_probe.lua script (docs/ADPCM_B.md).
add_executable(ym_probe ${PROJECT_SOURCE_DIR}/tools/ymprobe/ym_probe.c neogeo/ym2610.cpp)
target_include_directories(ym_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/neogeo)
target_link_libraries(ym_probe PRIVATE ymfm)
target_link_options(ym_probe PRIVATE -static-libstdc++ -static-libgcc)
set_target_properties(ym_probe PROPERTIES LINKER_LANGUAGE CXX)

set(CYCLES_BIN ${CMAKE_CURRENT_BINARY_DIR}/cycles_68000.bin)
add_custom_command(OUTPUT ${CYCLES_BIN} COMMAND dump_cycles ${CYCLES_BIN} DEPENDS dump_cycles VERBATIM)
file(GLOB RECOMP_PY ${PROJECT_SOURCE_DIR}/tools/recomp68k/*.py)

# The recompiled program of a game is generated from the user's copy and the analysis listings.
# Without it (PD_RECOMPILED=OFF, or no ROMs / listings) the game runs the 68000 in the interpreter
# only: that build contains no code derived from the game and is the one that can be distributed.
option(PD_RECOMPILED "Build the recompiled programs from the user's copies (personal builds)" ON)

# recompiled_<id>: the game's recompiled program, or program_none.c. Sets PD_HAVE_PROGRAM_<id>.
function(pd_recompiled_program id roms analysis)
    set(have OFF)
    if(PD_RECOMPILED AND EXISTS "${roms}/${id}_game_m68k" AND EXISTS "${analysis}/listing_bios.txt")
        set(have ON)
    elseif(PD_RECOMPILED)
        message(STATUS "${id}: recompiled program disabled, ROMs (${roms}) or analysis listings (${analysis}) are missing")
    endif()
    set(PD_HAVE_PROGRAM_${id} ${have} CACHE INTERNAL "The recompiled program of ${id} is built")
    if(NOT have)
        message(STATUS "${id}: interpreter-only build (no code derived from the game)")
        add_library(recompiled_${id} STATIC cpu/program_none.c)
        target_link_libraries(recompiled_${id} PUBLIC cpu68k)
        return()
    endif()
    set(dir ${CMAKE_CURRENT_BINARY_DIR}/program_${id})
    set(sources ${dir}/program_registry.c)
    foreach(n RANGE 0 15)
        string(LENGTH "${n}" len)
        if(len EQUAL 1)
            set(num "00${n}")
        else()
            set(num "0${n}")
        endif()
        list(APPEND sources ${dir}/program_${num}.c)
    endforeach()
    # MAME coverage (tools/coverage.py): Metal Slug X's coverage.json and coverage_mission1.json,
    # Metal Slug 3's phase3/coverage_*.json (one per recorded run).
    file(GLOB coverage ${analysis}/coverage.json ${analysis}/coverage_*.json ${analysis}/phase3/coverage_*.json)
    add_custom_command(
        OUTPUT ${sources}
        COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/recomp68k/program.py
                --roms ${roms} --game ${id}
                --listings ${analysis}/listing_p1.txt ${analysis}/listing_bios.txt
                --coverage ${coverage}
                --cycles ${CYCLES_BIN} --output ${dir} --files 16
        DEPENDS ${RECOMP_PY} ${CYCLES_BIN} ${analysis}/listing_p1.txt ${analysis}/listing_bios.txt ${coverage}
        COMMENT "Recompiling the user's ${id} P1 and BIOS to C"
        VERBATIM)
    # Code generated from the user's copy: it lives only in build/.
    add_library(recompiled_${id} STATIC ${sources})
    target_link_libraries(recompiled_${id} PUBLIC cpu68k)
    target_compile_options(recompiled_${id} PRIVATE -O1 -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-label)
endfunction()

# Neo Geo hardware runtime (the game comes from ng_game, defined by each executable).
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

# Port executables (SDL3).
find_package(SDL3 CONFIG)
if(NOT SDL3_FOUND)
    message(STATUS "Port disabled: SDL3 not found (CMAKE_PREFIX_PATH)")
    return()
endif()
if(WIN32)
    enable_language(RC)
endif()
set(PORT_SOURCES port/main.c port/headless.c port/window.c port/config.c port/debug.c port/font.c port/ui_font.c
    port/find_game.c port/lang.c port/profile.c port/cheats.c port/logo.c port/menu.c port/sounds.c
    port/progress.c port/records.c port/bossbar.c port/bossrush.c port/fastfwd.c port/runahead.c port/crt.c
    ${PROJECT_SOURCE_DIR}/external/fonts/black_ops_one.c ${PROJECT_SOURCE_DIR}/external/fonts/chakra_petch_regular.c
    ${PROJECT_SOURCE_DIR}/external/fonts/chakra_petch_semibold.c ${PROJECT_SOURCE_DIR}/external/fonts/chakra_petch_bold.c)

# <id>.exe: the shared frontend plus the game's descriptor and profile (src/games/<id>/*.c; not
# <id>_*.c, a name the .gitignore keeps for the user's ROM files).
function(pd_game_executable id)
    file(GLOB game_sources ${CMAKE_CURRENT_SOURCE_DIR}/games/${id}/*.c)
    set(sources ${PORT_SOURCES} ${game_sources})
    if(WIN32)
        list(APPEND sources port/${id}.rc)
    endif()
    # The user's own icon (personal builds only, like the recompiled program): PD_ICON_DIR/<id>.png,
    # outside the repository (it may be the game's artwork), made into an .ico and its resource in
    # the build folder by tools/make_icon.ps1. The distributable build has no icon.
    set(icon OFF)
    if(WIN32 AND PD_RECOMPILED AND EXISTS "${PD_ICON_DIR}/${id}.png")
        set(ico ${CMAKE_CURRENT_BINARY_DIR}/${id}_icon.ico)
        set(rc ${CMAKE_CURRENT_BINARY_DIR}/${id}_icon.rc)
        add_custom_command(OUTPUT ${ico} ${rc}
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${PROJECT_SOURCE_DIR}/tools/make_icon.ps1
                    -Png ${PD_ICON_DIR}/${id}.png -Ico ${ico} -Rc ${rc}
            DEPENDS ${PD_ICON_DIR}/${id}.png ${PROJECT_SOURCE_DIR}/tools/make_icon.ps1
            COMMENT "Icon of ${id} from the user's ${id}.png"
            VERBATIM)
        list(APPEND sources ${rc})
        set(icon ON)
    endif()
    add_executable(${id} WIN32 ${sources})
    if(icon)
        target_compile_definitions(${id} PRIVATE PD_HAS_ICON=1)
    endif()
    target_include_directories(${id} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/port)
    target_link_libraries(${id} PRIVATE neogeo recompiled_${id} SDL3::SDL3)
    if(WIN32)
        target_link_libraries(${id} PRIVATE dxguid)     # IID_IDXGIDevice1 (window.c: frame latency)
    endif()
    target_compile_options(${id} PRIVATE -Wall -Wextra)
    # C++ runtime (for ymfm) linked statically: the executable only needs SDL3.dll.
    target_link_options(${id} PRIVATE -static-libstdc++ -static-libgcc)
    set_target_properties(${id} PROPERTIES LINKER_LANGUAGE CXX)
    add_custom_command(TARGET ${id} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${id}> $<TARGET_FILE_DIR:${id}>
        COMMAND_EXPAND_LISTS)
endfunction()

pd_recompiled_program(mslugx "${PD_ROMS}" "${PD_ANALYSIS}")
pd_game_executable(mslugx)
set(PD_HAVE_MSLUGX ON CACHE INTERNAL "mslugx.exe is built")

# Distribution: cmake --install build --prefix <folder> installs mslugx.exe and SDL3.dll. With the
# recompiled program the executable contains code translated from the user's copy: personal use.
install(TARGETS mslugx RUNTIME DESTINATION .)
install(FILES $<TARGET_RUNTIME_DLLS:mslugx> DESTINATION .)

# ------------------------------------------------------------------ tests
# They need the user's copy (PD_ROMS) and, for the comparisons against MAME, the references in
# PD_ANALYSIS (outside the repo). Every test carries the label mslugx.
if(NOT EXISTS "${PD_ROMS}/mslugx_game_m68k")
    message(STATUS "Tests disabled: the user's copy (PD_ROMS/mslugx_game_m68k) is missing")
    return()
endif()

# The GOG copy (DotEmu's mslugx_* files, by SHA-256) is recognized.
add_test(NAME mslugx_roms_dotemu COMMAND mslugx --roms ${PD_ROMS} --headless --frames 1 --no-audio)
set_tests_properties(mslugx_roms_dotemu PROPERTIES ENVIRONMENT "PD_ROMS_INFO=1"
                     PASS_REGULAR_EXPRESSION "PC release files \\(DotEmu\\), GOG program, BIOS us\n")

# Phase 9: a MAME set (the one tools/extract_roms.py rebuilds in PD_ROMS/mame), as loose files and
# as zips (made at test time in the build folder), loads exactly the same data as the PC release
# files; and only the game's zip finds neogeo.zip next to it.
if(EXISTS "${PD_ROMS}/mame/mslugx" AND EXISTS "${PD_ROMS}/mame/neogeo")
    add_test(NAME roms_mame_folder COMMAND mslugx --roms ${PD_ROMS} --headless --frames 1 --no-audio)
    set_tests_properties(roms_mame_folder PROPERTIES ENVIRONMENT "PD_COMPARE_ROMS=${PD_ROMS}/mame")
    set(ZIPS ${CMAKE_CURRENT_BINARY_DIR}/mame_zips)
    add_test(NAME roms_mame_zips_make
             COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tests/loader/zip_set.py ${PD_ROMS}/mame ${ZIPS} mslugx)
    set_tests_properties(roms_mame_zips_make PROPERTIES FIXTURES_SETUP mame_zips)
    add_test(NAME roms_mame_zip COMMAND mslugx --roms ${PD_ROMS} --headless --frames 1 --no-audio)
    set_tests_properties(roms_mame_zip PROPERTIES ENVIRONMENT "PD_COMPARE_ROMS=${ZIPS}/mslugx.zip"
                         FIXTURES_REQUIRED mame_zips)
endif()

# The game search finds a simulated Steam install in two libraries.
add_test(NAME mslugx_find_steam
         COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tests/loader/steam_layout.py
                 $<TARGET_FILE:mslugx> mslugx ${CMAKE_CURRENT_BINARY_DIR}/mslugx_steam)

# Phase 5: a save state, loaded into a reset machine, reproduces RAM, image and audio.
add_test(NAME savestate_roundtrip
         COMMAND mslugx --roms ${PD_ROMS} --headless --frames 3000 --test-state 600)
# Phase 6B: the sound menu is audible and, after closing it, the game stays identical.
add_test(NAME sound_menu
         COMMAND mslugx --roms ${PD_ROMS} --headless --frames 3000 --test-sounds)

# Phase 4: the user's recorded game (Mission 1) replayed against MAME, with no divergences in RAM,
# palette or image. The MAME references and the inputs live in PD_ANALYSIS/phase4 (outside the
# repo; how to generate them: tools/verify_mission.py).
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
# the references to PD_ANALYSIS/phase8, outside the repo). Stage 6 still has a known
# difference (docs/PHASE8_STAGES.md) and is not a test; stage 2 matches since Phase 11.
foreach(stage 02 03 04 05 07 08 09 10 11 12 13)
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

# Phase 12 (docs/PHASE12_MISSIONS.md): scripted runs against MAME (tools/mame_run.py, references
# in PD_ANALYSIS/phase12): phase12/missions/mission2-6 start at Missions 2-6 (PD_POKE of the start
# stage set and mission bytes right after the new game, in <run>_poke.txt); phase12/complete/
# mission1 is the bot finishing Mission 1 (development mode and debug dips in <run>_env.txt).
file(GLOB msx_phase12_runs ${PD_ANALYSIS}/phase12/*/*_fingerprints.txt)
foreach(fp ${msx_phase12_runs})
    if(fp MATCHES "_(replay|port)_fingerprints\\.txt$")
        continue()
    endif()
    string(REGEX REPLACE "_fingerprints\\.txt$" "" base "${fp}")
    get_filename_component(run "${base}" NAME)
    if(EXISTS "${base}_date.txt" AND EXISTS "${base}_frames.txt")
        file(READ "${base}_date.txt" date)
        file(READ "${base}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        set(env)
        if(EXISTS "${base}_poke.txt")
            file(READ "${base}_poke.txt" poke)
            string(STRIP "${poke}" poke)
            string(REPLACE ";" "\;" poke "${poke}")     # several writes: kept in one variable
            set(env "PD_POKE=${poke}")
        endif()
        if(EXISTS "${base}_env.txt")                  # PD_DEVMODE, PD_DIPS (mame_run.py)
            file(STRINGS "${base}_env.txt" run_env)
            list(APPEND env ${run_env})
        endif()
        add_test(NAME mslugx_${run}_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslugx> --roms ${PD_ROMS}
                         --inputs ${base}_inputs.txt --reference ${fp}
                         --frames ${frames} --date ${date}
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslugx_${run}_fingerprints_port.txt)
        set_tests_properties(mslugx_${run}_vs_mame PROPERTIES ENVIRONMENT "${env}")
    endif()
endforeach()
# The "Start mission" option (PD_START_MISSION) against the MAME run that starts at Mission 3: the
# option writes the same bytes at the same frame as the run's PD_POKE, so nothing differs.
set(m3 ${PD_ANALYSIS}/phase12/missions/mission3)
if(EXISTS "${m3}_fingerprints.txt" AND EXISTS "${m3}_date.txt")
    file(READ "${m3}_date.txt" date)
    file(READ "${m3}_frames.txt" frames)
    string(STRIP "${date}" date)
    string(STRIP "${frames}" frames)
    add_test(NAME mslugx_start_mission_option_vs_mame
             COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                     --exe $<TARGET_FILE:mslugx> --roms ${PD_ROMS}
                     --inputs ${m3}_inputs.txt --reference ${m3}_fingerprints.txt
                     --frames ${frames} --date ${date}
                     --output ${CMAKE_CURRENT_BINARY_DIR}/mslugx_start_mission_option_fingerprints_port.txt)
    set_tests_properties(mslugx_start_mission_option_vs_mame PROPERTIES ENVIRONMENT "PD_START_MISSION=3")
endif()
# Mission mode's direct start (cheats.h, cheats_mission_from): the coin right after the fast boot
# (frame 304), no "How to play" (PLAY MANUAL read as off), Mission 3 written once the new game has
# cleared the start bytes; its "Mission 3... start!" plays (voice 33) and it is played as Mission 3.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/mslugx_mission_direct.txt "305 310 coin1\n355 360 start1\n700 705 a\n1000 1005 a\n")
add_test(NAME mslugx_mission_mode_direct
         COMMAND mslugx --roms ${PD_ROMS} --headless --frames 2600 --no-audio
                 --script ${CMAKE_CURRENT_BINARY_DIR}/mslugx_mission_direct.txt --date 2026-10-26-01-00-00-1)
set_tests_properties(mslugx_mission_mode_direct PROPERTIES ENVIRONMENT "PD_MISSION_ONCE=3"
    PASS_REGULAR_EXPRESSION "start mission 3 written at frame 357")
# Progress and records: the bot finishes Mission 1 (frame 6553, 5849 frames) and unlocks Mission 2;
# with the debug dips on, the mission is not recorded ("not clean").
set(c1 ${PD_ANALYSIS}/phase12/complete/mission1)
if(EXISTS "${c1}_inputs.txt" AND EXISTS "${c1}_date.txt")
    file(READ "${c1}_date.txt" date)
    string(STRIP "${date}" date)
    add_test(NAME mslugx_records_reset
             COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_CURRENT_BINARY_DIR}/mslugx_records_bot.ini
                     ${CMAKE_CURRENT_BINARY_DIR}/mslugx_progress_bot.ini)
    set_tests_properties(mslugx_records_reset PROPERTIES FIXTURES_SETUP mslugx_records)
    add_test(NAME mslugx_records_bot
             COMMAND mslugx --roms ${PD_ROMS} --headless --no-audio --frames 7000
                     --inputs ${c1}_inputs.txt --date ${date})
    set_tests_properties(mslugx_records_bot PROPERTIES FIXTURES_REQUIRED mslugx_records
        ENVIRONMENT "PD_DEVMODE=1;PD_DIPS=8001;PD_RECORDS=${CMAKE_CURRENT_BINARY_DIR}/mslugx_records_bot.ini;PD_PROGRESS=${CMAKE_CURRENT_BINARY_DIR}/mslugx_progress_bot.ini"
        PASS_REGULAR_EXPRESSION "mission 2 unlocked at frame 6553
mission 1 completed at frame 6553: 5849 frames \\(1:38.82\\), score 149600 \\(not clean\\)")
endif()

get_property(msx_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${msx_tests} PROPERTIES LABELS mslugx)
