# Port core: CPU (dispatcher + fallback), recompiled programs, Neo Geo runtime and one executable
# per game: mslug (the product) and mslug4, mslug5, mslug3 and mslugx (the regression targets, only
# built when those ports' copies are available).

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

pd_recompiled_program(mslug "${PD_ROMS}" "${PD_ANALYSIS}")
pd_game_executable(mslug)

# Distribution: cmake --install build --prefix <folder> installs mslug.exe and SDL3.dll. With the
# recompiled program the executable contains code translated from the user's copy: personal use.
install(TARGETS mslug RUNTIME DESTINATION .)
install(FILES $<TARGET_RUNTIME_DLLS:mslug> DESTINATION .)

# Metal Slug 4 regression target (the Metal Slug 4 port's workspace, read only): built when its GOG
# copy is there, never released. Its 68000 files come from tools/make_reference.py --mslug4, in its
# analysis folder (phase3/program), outside its ROM folder.
set(PD_HAVE_MSLUG4 OFF CACHE INTERNAL "The Metal Slug 4 regression target is built")
if(EXISTS "${PD_MS4_ROMS}/gog/p1.bin")
    set(PD_HAVE_MSLUG4 ON CACHE INTERNAL "The Metal Slug 4 regression target is built")
    pd_recompiled_program(mslug4 "${PD_MS4_ANALYSIS}/phase3/program" "${PD_MS4_ANALYSIS}")
    pd_game_executable(mslug4)
else()
    message(STATUS "Metal Slug 4 regression target disabled: ${PD_MS4_ROMS}/gog/p1.bin is missing")
endif()

# Metal Slug 5 regression target (the Metal Slug 5 port's workspace, read only): built when its
# decrypted reference files are there, never released.
set(PD_HAVE_MSLUG5 OFF CACHE INTERNAL "The Metal Slug 5 regression target is built")
if(EXISTS "${PD_MS5_ROMS}/mslug5_game_m68k")
    set(PD_HAVE_MSLUG5 ON CACHE INTERNAL "The Metal Slug 5 regression target is built")
    pd_recompiled_program(mslug5 "${PD_MS5_ROMS}" "${PD_MS5_ANALYSIS}")
    pd_game_executable(mslug5)
else()
    message(STATUS "Metal Slug 5 regression target disabled: ${PD_MS5_ROMS}/mslug5_game_m68k is missing")
endif()

# Metal Slug 3 regression target (the Metal Slug 3 port's workspace, read only): built when its
# copy is there, never released.
set(PD_HAVE_MSLUG3 OFF CACHE INTERNAL "The Metal Slug 3 regression target is built")
if(EXISTS "${PD_MS3_ROMS}/mslug3_game_m68k")
    set(PD_HAVE_MSLUG3 ON CACHE INTERNAL "The Metal Slug 3 regression target is built")
    pd_recompiled_program(mslug3 "${PD_MS3_ROMS}" "${PD_MS3_ANALYSIS}")
    pd_game_executable(mslug3)
else()
    message(STATUS "Metal Slug 3 regression target disabled: ${PD_MS3_ROMS}/mslug3_game_m68k is missing")
endif()

set(PD_HAVE_MSLUGX OFF CACHE INTERNAL "The Metal Slug X regression target is built")
if(EXISTS "${PD_MSX_ROMS}/mslugx_game_m68k")
    set(PD_HAVE_MSLUGX ON CACHE INTERNAL "The Metal Slug X regression target is built")
    pd_recompiled_program(mslugx "${PD_MSX_ROMS}" "${PD_MSX_ANALYSIS}")
    pd_game_executable(mslugx)
else()
    message(STATUS "Metal Slug X regression target disabled: ${PD_MSX_ROMS}/mslugx_game_m68k is missing")
endif()

# ------------------------------------------------------------------ Metal Slug tests
# Phase 1 (docs/PHASE1_IMPORT.md): the user's GOG copy (DotEmu's mslug_* files, by SHA-256) and the
# MAME set rebuilt from it (roms\mslug, roms\neogeo; every file by CRC-32 and SHA-1, linked into
# the build folder) are recognized. ctest's -L takes a regular expression: Metal Slug's tests alone
# are -L "^mslug$".
if(EXISTS "${PD_ROMS}/mslug_game_m68k")
    add_test(NAME mslug_roms_dotemu COMMAND mslug --roms ${PD_ROMS} --headless --frames 1 --no-audio)
    set_tests_properties(mslug_roms_dotemu PROPERTIES LABELS mslug ENVIRONMENT "PD_ROMS_INFO=1"
                         PASS_REGULAR_EXPRESSION "PC release files \\(DotEmu\\), GOG program, BIOS us\n")
    # Phase 2 (docs/PHASE2_HARDWARE.md): every part the runtime loaded equals MAME's regions
    # (tools/check_load.py against PD_ROMS/decrypted, dumped by tools/mame/dump_region.lua).
    if(EXISTS "${PD_ROMS}/decrypted/sprites.bin")
        add_test(NAME mslug_roms_dotemu_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/check_load.py
                         --exe $<TARGET_FILE:mslug> --roms ${PD_ROMS}
                         --work ${CMAKE_CURRENT_BINARY_DIR}/mslug_load_dump)
        set_tests_properties(mslug_roms_dotemu_vs_mame PROPERTIES LABELS mslug)
    endif()
    # Scripted runs against MAME (tools/mame_run.py --game mslug, references in PD_ANALYSIS/phase2):
    #   attract: power-on with empty NVRAM, BIOS and the attract demo;
    #   start: a credit, START, the game's "How to play", Mission 1 with a bot to GAME OVER.
    # RAM, palette and image on every VBlank / frame; <run>_known_vblank.txt lists the VBlanks that
    # differ only in the stack, checked byte by byte, and the images known to differ.
    # Phase 4 (docs/PHASE4_PROFILE.md): phase4/missions/* the same game started at Missions 2-6
    # (<run>_poke.txt: PD_POKE), phase4/<kind>/* with the developer mode, debug switches, blood or
    # cheats (<run>_env.txt: PD_DEVMODE, PD_DIPS, PD_BLOOD...), phase4/bosses/* the boss fights
    # reached by the fast-forward study's bot (docs/BOSS_RUSH.md: lives, bombs and the Heavy
    # Machine Gun as PD_POKE, PD_KILL_ENEMIES in <run>_env.txt).
    set(ms1_runs ${PD_ANALYSIS}/phase2/attract/attract ${PD_ANALYSIS}/phase2/start/start)
    file(GLOB ms1_phase4_runs ${PD_ANALYSIS}/phase4/*/*_fingerprints.txt)
    foreach(fp ${ms1_phase4_runs})
        if(NOT fp MATCHES "_(replay|port)_fingerprints\\.txt$" AND NOT fp MATCHES "_fingerprints_port\\.txt$")
            string(REGEX REPLACE "_fingerprints\\.txt$" "" base "${fp}")
            list(APPEND ms1_runs ${base})
        endif()
    endforeach()
    foreach(base ${ms1_runs})
        get_filename_component(run "${base}" NAME)
        if(EXISTS "${base}_fingerprints.txt" AND EXISTS "${base}_date.txt" AND EXISTS "${base}_frames.txt")
            file(READ "${base}_date.txt" date)
            file(READ "${base}_frames.txt" frames)
            string(STRIP "${date}" date)
            string(STRIP "${frames}" frames)
            set(known)
            if(EXISTS "${base}_known_vblank.txt")
                set(known --known-differences ${base}_known_vblank.txt)
            endif()
            set(env)
            if(EXISTS "${base}_cheat.txt")
                # the cheats (PD_CHEAT) against the MAME run that writes the bytes they changed
                # (PD_SPAWN_POKES, in <run>_poke.txt) at the same frames
                file(READ "${base}_cheat.txt" cheat)
                string(STRIP "${cheat}" cheat)
                string(REPLACE ";" "\;" cheat "${cheat}")
                set(env "PD_CHEAT=${cheat}")
            elseif(EXISTS "${base}_poke.txt")
                file(READ "${base}_poke.txt" poke)
                string(STRIP "${poke}" poke)
                string(REPLACE ";" "\;" poke "${poke}")     # several writes: kept in one variable
                set(env "PD_POKE=${poke}")
            endif()
            if(EXISTS "${base}_env.txt")                  # PD_DEVMODE, PD_DIPS... (mame_run.py)
                file(STRINGS "${base}_env.txt" run_env)
                list(APPEND env ${run_env})
            endif()
            add_test(NAME mslug_${run}_vs_mame
                     COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                             --exe $<TARGET_FILE:mslug> --roms ${PD_ROMS}
                             --inputs ${base}_inputs.txt --reference ${base}_fingerprints.txt
                             --frames ${frames} --date ${date} ${known}
                             --output ${CMAKE_CURRENT_BINARY_DIR}/mslug_${run}_fingerprints_port.txt)
            set_tests_properties(mslug_${run}_vs_mame PROPERTIES LABELS mslug ENVIRONMENT "${env}")
        endif()
    endforeach()
    # The boss health bar in the boss runs (phase4/bosses, the same pokes and kills as their MAME
    # tests): every boss of Missions 3-6 shown by name and followed to its defeat.
    function(pd_mslug_bossbar run frames expect)
        set(base ${PD_ANALYSIS}/phase4/bosses/${run})
        if(EXISTS "${base}_inputs.txt" AND EXISTS "${base}_poke.txt" AND EXISTS "${base}_env.txt")
            file(READ "${base}_date.txt" date)
            file(READ "${base}_poke.txt" poke)
            string(STRIP "${date}" date)
            string(STRIP "${poke}" poke)
            string(REPLACE ";" "\;" poke "${poke}")
            file(STRINGS "${base}_env.txt" run_env)
            add_test(NAME mslug_bossbar_${run}
                     COMMAND mslug --roms ${PD_ROMS} --headless --no-audio --frames ${frames}
                             --inputs ${base}_inputs.txt --date ${date})
            set_tests_properties(mslug_bossbar_${run} PROPERTIES LABELS mslug
                ENVIRONMENT "PD_BOSSBAR=1;PD_POKE=${poke};${run_env}" PASS_REGULAR_EXPRESSION "${expect}")
        endif()
    endfunction()
    pd_mslug_bossbar(boss_m3 118800
        "boss 1022E0 390/400.*boss 1022E0 0/400 dead.*ALLEN O'NEIL 101FC0 690/700.*ALLEN O'NEIL 101FC0 0/700 dead.*TANI OH 1036E0 1999/2000.*TANI OH 1036E0 0/2000 dead.*SHOE & KARN 1021A0 1190/1200.*SHOE & KARN 102D80 0/1500 dead.*boss 1038C0 1100/1200.*boss 1038C0 0/1200 dead.*IRON NOKANA 105620 2440/2500.*IRON NOKANA 105620 0/2500 dead")
    pd_mslug_bossbar(boss_m6 108700 "HI-DO 1027E0 6990/7000.*HI-DO 1027E0 0/7000 dead")
    # Boss Rush (docs/BOSS_RUSH.md, stage 3): the eight bosses' states made from an empty folder
    # (the fast-forward), then the whole run with a plain bot and each boss defeated by the game's
    # damage routine 900 frames after it loads (PD_BOSSRUSH_KILL): the score carried from boss to
    # boss, the ending after Hi-Do. Then, from Shoe & Karn, the Heavy Machine Gun and 25 bombs
    # given during the fight reach Iron Nokana.
    add_test(NAME mslug_bossrush_reset
             COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_CURRENT_BINARY_DIR}/mslug_rush)
    set_tests_properties(mslug_bossrush_reset PROPERTIES LABELS mslug FIXTURES_SETUP mslug_rush)
    add_test(NAME mslug_bossrush_full
             COMMAND mslug --roms ${PD_ROMS} --headless --no-audio --frames 60000)
    set_tests_properties(mslug_bossrush_full PROPERTIES LABELS mslug FIXTURES_REQUIRED mslug_rush
        FIXTURES_SETUP mslug_rush_states
        ENVIRONMENT "PD_BOSSRUSH=1;PD_BOSSRUSH_KILL=900;PD_BOSSRUSH_DIR=${CMAKE_CURRENT_BINARY_DIR}/mslug_rush/"
        PASS_REGULAR_EXPRESSION "bossrush: prepared in.*next boss, 1/8.*mission 2, score 00007510.*next boss, 2/8.*mission 2, score 00007810.*next boss, 3/8.*mission 3, score 00012110.*next boss, 4/8.*mission 3, score 00013610.*next boss, 5/8.*mission 4, score 00013610.*next boss, 6/8.*mission 5, score 00014810.*next boss, 7/8.*mission 6, score 00032110.*cleared, 8/8 bosses defeated")
    add_test(NAME mslug_bossrush_carry
             COMMAND mslug --roms ${PD_ROMS} --headless --no-audio --frames 4000)
    set_tests_properties(mslug_bossrush_carry PROPERTIES LABELS mslug FIXTURES_REQUIRED mslug_rush_states
        ENVIRONMENT "PD_BOSSRUSH=1;PD_BOSSRUSH_KILL=900;PD_BOSSRUSH_FROM=6;PD_CHEAT=7900,weapon,0,4\;7901,bombs,0,25;PD_BOSSRUSH_DIR=${CMAKE_CURRENT_BINARY_DIR}/mslug_rush/"
        PASS_REGULAR_EXPRESSION "next boss, 1/8 bosses defeated.*mission 5, score 00001300, lives 9, bombs 25, weapon 4")
    # The "Start mission" option (PD_START_MISSION) against the MAME run that starts at Mission 4:
    # identical images; RAM differs only in the continue byte while the option has written it and
    # the MAME run has not yet (mission4_option_known_vblank.txt).
    set(m4 ${PD_ANALYSIS}/phase4/missions/mission4)
    if(EXISTS "${m4}_option_known_vblank.txt" AND EXISTS "${m4}_fingerprints.txt")
        file(READ "${m4}_date.txt" date)
        file(READ "${m4}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        add_test(NAME mslug_start_mission_option_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslug> --roms ${PD_ROMS}
                         --inputs ${m4}_inputs.txt --reference ${m4}_fingerprints.txt
                         --frames ${frames} --date ${date} --known-differences ${m4}_option_known_vblank.txt
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug_start_mission_option_fingerprints_port.txt)
        set_tests_properties(mslug_start_mission_option_vs_mame PROPERTIES LABELS mslug ENVIRONMENT "PD_START_MISSION=4")
    endif()
    # Mission mode's direct start (cheats.h, cheats_mission_from): the coin right after the fast
    # boot (frame 1065), no "How to play" (PLAY MANUAL read as off), Mission 3 written into the
    # continue byte until the game loads it (the mission index 0x106ED0 = 2, start_no_section).
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/mslug_mission_direct.txt "1066 1071 coin1\n1116 1121 start1\n")
    add_test(NAME mslug_mission_mode_direct
             COMMAND mslug --roms ${PD_ROMS} --headless --frames 2400 --no-audio
                     --script ${CMAKE_CURRENT_BINARY_DIR}/mslug_mission_direct.txt --date 2026-10-25-01-00-00-0)
    set_tests_properties(mslug_mission_mode_direct PROPERTIES LABELS mslug ENVIRONMENT "PD_MISSION_ONCE=3"
        PASS_REGULAR_EXPRESSION "start mission 3 written at frame 1120")
    # Records and progress: the user's game completes Missions 1 and 2 cleanly (both recorded) and
    # unlocks Missions 2 and 3.
    set(play1 ${PD_ANALYSIS}/phase3/play/partida1)
    if(EXISTS "${play1}_replay_inputs.txt" AND EXISTS "${play1}_date.txt")
        file(READ "${play1}_date.txt" date)
        string(STRIP "${date}" date)
        add_test(NAME mslug_records_reset
                 COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_CURRENT_BINARY_DIR}/mslug_records_clean.ini
                         ${CMAKE_CURRENT_BINARY_DIR}/mslug_progress_partida1.ini)
        set_tests_properties(mslug_records_reset PROPERTIES LABELS mslug FIXTURES_SETUP mslug_records)
        add_test(NAME mslug_records_clean
                 COMMAND mslug --roms ${PD_ROMS} --headless --no-audio --frames 27884
                         --inputs ${play1}_replay_inputs.txt --date ${date})
        set_tests_properties(mslug_records_clean PROPERTIES LABELS mslug FIXTURES_REQUIRED mslug_records
            ENVIRONMENT "PD_RECORDS=${CMAKE_CURRENT_BINARY_DIR}/mslug_records_clean.ini;PD_PROGRESS=${CMAKE_CURRENT_BINARY_DIR}/mslug_progress_partida1.ini"
            PASS_REGULAR_EXPRESSION "mission 2 unlocked at frame 10598
mission 1 completed at frame 10598: 10218 frames.*
mission 3 unlocked at frame 27484
mission 2 completed at frame 27484: 16886 frames"
            FAIL_REGULAR_EXPRESSION "not clean")
        # The sound menu (src/games/mslug/sounds.c, Metal Slug X's driver modes) is audible in the
        # middle of the user's game and, after closing it, the game stays identical.
        add_test(NAME mslug_sound_menu
                 COMMAND mslug --roms ${PD_ROMS} --headless --frames 20000 --test-sounds
                         --inputs ${play1}_replay_inputs.txt --date ${date})
        set_tests_properties(mslug_sound_menu PROPERTIES LABELS mslug)
        # The boss health bar (docs/BOSS_RUSH.md, stage 2) in the user's game: Tetsuyuki, the three
        # boats (no name), Mini-Bata and Hairbuster Riberts, each to its defeat.
        add_test(NAME mslug_bossbar_partida1
                 COMMAND mslug --roms ${PD_ROMS} --headless --no-audio --frames 27884
                         --inputs ${play1}_replay_inputs.txt --date ${date})
        set_tests_properties(mslug_bossbar_partida1 PROPERTIES LABELS mslug ENVIRONMENT "PD_BOSSBAR=1"
            PASS_REGULAR_EXPRESSION "bossbar 7839: TETSUYUKI 1038C0 2199/2200.*TETSUYUKI 1038C0 0/2200 dead.*boss 103BE0 300/350.*boss 103BE0 0/350 dead.*boss 1035A0 0/450 dead.*boss 104220 0/700 dead.*MINI-BATA 103500 1190/1200.*MINI-BATA 103500 0/1200 dead.*HAIRBUSTER RIBERTS 100D00 3890/3900.*HAIRBUSTER RIBERTS 100D00 0/3900 dead")
    endif()
    # Phase 3: games recorded by hand in MAME (grabar_partida.bat, empty NVRAM), replayed by
    # mame_run.py --replay --name NAME into PD_ANALYSIS/phase3/play: mslug_play_<NAME>_vs_mame.
    file(GLOB played ${PD_ANALYSIS}/phase3/play/*_replay_fingerprints.txt)
    foreach(fp ${played})
        string(REGEX REPLACE "_replay_fingerprints\\.txt$" "" base "${fp}")
        get_filename_component(run "${base}" NAME)
        if(EXISTS "${base}_date.txt" AND EXISTS "${base}_frames.txt")
            file(READ "${base}_date.txt" date)
            file(READ "${base}_frames.txt" frames)
            string(STRIP "${date}" date)
            string(STRIP "${frames}" frames)
            set(known)
            if(EXISTS "${base}_known_vblank.txt")
                set(known --known-differences ${base}_known_vblank.txt)
            endif()
            add_test(NAME mslug_play_${run}_vs_mame
                     COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                             --exe $<TARGET_FILE:mslug> --roms ${PD_ROMS}
                             --inputs ${base}_replay_inputs.txt --reference ${fp}
                             --frames ${frames} --date ${date} ${known}
                             --output ${CMAKE_CURRENT_BINARY_DIR}/mslug_play_${run}_fingerprints_port.txt)
            set_tests_properties(mslug_play_${run}_vs_mame PROPERTIES LABELS mslug)
        endif()
    endforeach()
else()
    message(STATUS "Metal Slug GOG test disabled: the user's copy (PD_ROMS/mslug_game_m68k) is missing")
endif()
if(EXISTS "${PD_ROMS}/mslug/201-p1.p1")
    set(MS1_SET ${CMAKE_CURRENT_BINARY_DIR}/mslug_mameset)
    add_test(NAME mslug_roms_mame_make
             COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tests/loader/link_set.py ${PD_ROMS} ${MS1_SET}
                     mslug/201-p1.p1 mslug/201-s1.s1 mslug/201-m1.m1 mslug/201-v1.v1 mslug/201-v2.v2
                     mslug/201-c1.c1 mslug/201-c2.c2 mslug/201-c3.c3 mslug/201-c4.c4
                     neogeo/sp-u2.sp1 neogeo/000-lo.lo neogeo/sfix.sfix neogeo/sm1.sm1)
    set_tests_properties(mslug_roms_mame_make PROPERTIES FIXTURES_SETUP mslug_mameset LABELS mslug)
    add_test(NAME mslug_roms_mame COMMAND mslug --roms ${MS1_SET} --headless --frames 1 --no-audio)
    set_tests_properties(mslug_roms_mame PROPERTIES LABELS mslug FIXTURES_REQUIRED mslug_mameset
                         ENVIRONMENT "PD_ROMS_INFO=1" PASS_REGULAR_EXPRESSION "MAME set, [a-z ]+ program, BIOS us\n")
else()
    message(STATUS "Metal Slug MAME test disabled: the rebuilt set (PD_ROMS/mslug) is missing")
endif()

# ------------------------------------------------------------------ Metal Slug 4 tests
# The Metal Slug 4 regression target's tests (its workspace's copy, reference files and MAME
# runs: PD_MS4_ROMS, PD_MS4_ANALYSIS).
if(PD_HAVE_MSLUG4)
# Phase 2 (docs/PHASE2_HARDWARE.md): the GOG copy (Code Mystics' Data\rom\*.bin, by SHA-256) loads
# with the BIOS files of PD_MS4_ROMS/neogeo, and every part the runtime loaded equals MAME's regions
# (tools/check_load.py against PD_MS4_ROMS/decrypted). Alone, it says which system files are missing.
if(EXISTS "${PD_MS4_ROMS}/gog/p1.bin")
    add_test(NAME mslug4_roms_gog COMMAND mslug4 --roms ${PD_MS4_ROMS} --headless --frames 1 --no-audio)
    set_tests_properties(mslug4_roms_gog PROPERTIES LABELS mslug4 ENVIRONMENT "PD_ROMS_INFO=1"
                         PASS_REGULAR_EXPRESSION "PC release files \\(Code Mystics\\), GOG program, BIOS us\n")
    add_test(NAME mslug4_roms_gog_alone COMMAND mslug4 --roms ${PD_MS4_ROMS}/gog --headless --frames 1 --no-audio)
    set_tests_properties(mslug4_roms_gog_alone PROPERTIES LABELS mslug4
                         PASS_REGULAR_EXPRESSION "GOG copy found, without the Neo Geo system files \\(sfix.sfix, 000-lo.lo\\)")
    if(EXISTS "${PD_MS4_ROMS}/decrypted/sprites.bin")
        add_test(NAME mslug4_roms_gog_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/check_load.py
                         --exe $<TARGET_FILE:mslug4> --roms ${PD_MS4_ROMS}
                         --work ${CMAKE_CURRENT_BINARY_DIR}/mslug4_load_dump)
        set_tests_properties(mslug4_roms_gog_vs_mame PROPERTIES LABELS mslug4)
    endif()
    # Scripted runs against MAME (tools/mame_run.py --game mslug4h, references in PD_MS4_ANALYSIS/phase2):
    #   attract: power-on with empty NVRAM, BIOS and the attract demo;
    #   start: a credit, START, the game's "How to play", soldier select, Mission 1 with a bot.
    # RAM, palette and image on every VBlank / frame; <run>_known_vblank.txt lists the VBlanks that
    # differ only in the stack, checked byte by byte.
    # Phase 4 (docs/PHASE4_PROFILE.md): phase4/debug/* the same game with the developer mode,
    # debug dips or red blood (<run>_env.txt: PD_DEVMODE, PD_DIPS, PD_BLOOD...), phase4/missions/*
    # started at another mission (<run>_poke.txt: PD_POKE).
    set(ms4_runs ${PD_MS4_ANALYSIS}/phase2/attract/attract ${PD_MS4_ANALYSIS}/phase2/start/start)
    # boss_rush/stage1/boss_* (docs/BOSS_RUSH.md): the same started at each boss section, invincible.
    file(GLOB ms4_phase4_runs ${PD_MS4_ANALYSIS}/phase4/debug/*_fingerprints.txt
                              ${PD_MS4_ANALYSIS}/phase4/missions/mission*_fingerprints.txt
                              ${PD_MS4_ANALYSIS}/boss_rush/stage1/boss_*_fingerprints.txt)
    foreach(fp ${ms4_phase4_runs})
        if(NOT fp MATCHES "_replay_fingerprints\\.txt$")
            string(REGEX REPLACE "_fingerprints\\.txt$" "" base "${fp}")
            list(APPEND ms4_runs ${base})
        endif()
    endforeach()
    foreach(base ${ms4_runs})
        get_filename_component(run "${base}" NAME)
        if(EXISTS "${base}_fingerprints.txt" AND EXISTS "${base}_date.txt" AND EXISTS "${base}_frames.txt")
            file(READ "${base}_date.txt" date)
            file(READ "${base}_frames.txt" frames)
            string(STRIP "${date}" date)
            string(STRIP "${frames}" frames)
            set(known)
            if(EXISTS "${base}_known_vblank.txt")
                set(known --known-differences ${base}_known_vblank.txt)
            endif()
            set(env)
            if(EXISTS "${base}_poke.txt")
                file(READ "${base}_poke.txt" poke)
                string(STRIP "${poke}" poke)
                string(REPLACE ";" "\;" poke "${poke}")     # several writes: kept in one variable
                set(env "PD_POKE=${poke}")
            endif()
            if(EXISTS "${base}_env.txt")                  # PD_DEVMODE, PD_DIPS... (mame_run.py)
                file(STRINGS "${base}_env.txt" run_env)
                list(APPEND env ${run_env})
            endif()
            add_test(NAME mslug4_${run}_vs_mame
                     COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                             --exe $<TARGET_FILE:mslug4> --roms ${PD_MS4_ROMS}
                             --inputs ${base}_inputs.txt --reference ${base}_fingerprints.txt
                             --frames ${frames} --date ${date} ${known}
                             --output ${CMAKE_CURRENT_BINARY_DIR}/mslug4_${run}_fingerprints_port.txt)
            set_tests_properties(mslug4_${run}_vs_mame PROPERTIES LABELS mslug4 ENVIRONMENT "${env}")
        endif()
    endforeach()
    # Phase 4: the cheats (PD_CHEAT) against a MAME run that writes the same bytes at the same
    # moments (PD_POKE in cheats_poke.txt, logged by PD_SPAWN_POKES): the Heavy Machine Gun and the
    # Enemy Chaser through the game's own routine, 63 bombs, 7 lives, Fio.
    set(ch ${PD_MS4_ANALYSIS}/phase4/cheats/cheats)
    if(EXISTS "${ch}_fingerprints.txt")
        file(READ "${ch}_date.txt" date)
        file(READ "${ch}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        add_test(NAME mslug4_cheats_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslug4> --roms ${PD_MS4_ROMS}
                         --inputs ${ch}_inputs.txt --reference ${ch}_fingerprints.txt
                         --frames ${frames} --date ${date}
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug4_cheats_fingerprints_port.txt)
        set_tests_properties(mslug4_cheats_vs_mame PROPERTIES LABELS mslug4
                             ENVIRONMENT "PD_CHEAT=6700,weapon,0,5\;7300,weapon,0,12\;7500,bombs,0,63\;7650,lives,0,7\;7800,soldier,0,1")
    endif()
    # The vehicle cheat (phase4/vehicles): the Metal Slug spawned at frame 6700; the form cheat
    # (phase4/forms): fat, the mummy, the zombie and the monkey, each followed by normal. The MAME
    # runs write the RAM bytes the cheats changed (PD_SPAWN_POKES) at the same frames.
    function(pd_mslug4_cheat_run run cheat)
        set(b ${PD_MS4_ANALYSIS}/phase4/${run}/${run})
        if(EXISTS "${b}_fingerprints.txt")
            file(READ "${b}_date.txt" date)
            file(READ "${b}_frames.txt" frames)
            string(STRIP "${date}" date)
            string(STRIP "${frames}" frames)
            add_test(NAME mslug4_${run}_vs_mame
                     COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                             --exe $<TARGET_FILE:mslug4> --roms ${PD_MS4_ROMS}
                             --inputs ${b}_inputs.txt --reference ${b}_fingerprints.txt
                             --frames ${frames} --date ${date}
                             --output ${CMAKE_CURRENT_BINARY_DIR}/mslug4_${run}_fingerprints_port.txt)
            string(REPLACE ";" "\;" cheat "${cheat}")
            set_tests_properties(mslug4_${run}_vs_mame PROPERTIES LABELS mslug4 ENVIRONMENT "PD_CHEAT=${cheat}")
        endif()
    endfunction()
    # Phase 4: the "Start mission" option (PD_START_MISSION) against the MAME run that starts at
    # Mission 4: identical images; RAM differs only in the start mission byte while the option has
    # written it and the MAME run has not yet (mission4_option_known_vblank.txt).
    set(m4 ${PD_MS4_ANALYSIS}/phase4/missions/mission4)
    if(EXISTS "${m4}_option_known_vblank.txt" AND EXISTS "${m4}_fingerprints.txt")
        file(READ "${m4}_date.txt" date)
        file(READ "${m4}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        add_test(NAME mslug4_start_mission_option_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslug4> --roms ${PD_MS4_ROMS}
                         --inputs ${m4}_inputs.txt --reference ${m4}_fingerprints.txt
                         --frames ${frames} --date ${date} --known-differences ${m4}_option_known_vblank.txt
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug4_start_mission_option_fingerprints_port.txt)
        set_tests_properties(mslug4_start_mission_option_vs_mame PROPERTIES LABELS mslug4 ENVIRONMENT "PD_START_MISSION=4")
    endif()
    # Mission mode's direct start (cheats.h, cheats_mission_from): the coin right after the boot, no
    # "How to play" (PLAY MANUAL read as off), Mission 3 written again after the new game clears it
    # and loaded after the soldier select.
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/mslug4_mission_direct.txt "301 306 coin1\n351 356 start1\n")
    add_test(NAME mslug4_mission_mode_direct
             COMMAND mslug4 --roms ${PD_MS4_ROMS} --headless --frames 3000 --no-audio
                     --script ${CMAKE_CURRENT_BINARY_DIR}/mslug4_mission_direct.txt --date 2026-10-24-01-00-00-6)
    set_tests_properties(mslug4_mission_mode_direct PROPERTIES LABELS mslug4 ENVIRONMENT "PD_MISSION_ONCE=3"
        PASS_REGULAR_EXPRESSION "start mission 3 written at frame 2608")
    # Phase 4: the sound menu (src/games/mslug4/sounds.c, the profile's driver modes) is audible in
    # the middle of the user's game and, after closing it, the game stays identical.
    set(play1 ${PD_MS4_ANALYSIS}/phase3/play/partida1)
    if(EXISTS "${play1}_replay_inputs.txt" AND EXISTS "${play1}_date.txt")
        file(READ "${play1}_date.txt" date)
        string(STRIP "${date}" date)
        add_test(NAME mslug4_sound_menu
                 COMMAND mslug4 --roms ${PD_MS4_ROMS} --headless --frames 20000 --test-sounds
                         --inputs ${play1}_replay_inputs.txt --date ${date})
        set_tests_properties(mslug4_sound_menu PROPERTIES LABELS mslug4)
        # Records: the user's game completes Missions 1 and 2 cleanly; both are recorded.
        add_test(NAME mslug4_records_clean
                 COMMAND mslug4 --roms ${PD_MS4_ROMS} --headless --no-audio --frames 46541
                         --inputs ${play1}_replay_inputs.txt --date ${date})
        set_tests_properties(mslug4_records_clean PROPERTIES LABELS mslug4
            ENVIRONMENT "PD_RECORDS=${CMAKE_CURRENT_BINARY_DIR}/mslug4_records_clean.ini"
            PASS_REGULAR_EXPRESSION "mission 1 completed at frame 16334: 15339 frames .*
.*mission 2 completed at frame 46069: 29735 frames"
            FAIL_REGULAR_EXPRESSION "not clean")
    endif()
    # Progress and records from Mission 1's boss section (the developers' stage table: mission byte
    # 0, record 3), invincible, the "D kills every enemy" dip and a bot pressing D
    # (analysis/boss_rush/stage0/bot_d.txt): the mission ends and unlocks Mission 2; with the
    # slowdown off it is not recorded, and says why.
    set(botd ${PD_MS4_ANALYSIS}/boss_rush/stage0/bot_d.txt)
    set(botl ${PD_MS4_ANALYSIS}/boss_rush/stage1/bot_left.txt)
    # Boss health bar (docs/BOSS_RUSH.md, stage 2; PD_BOSSBAR prints the bar's changes): Big John
    # from the first hit to its death; the airship's phases (850, then up to 1600); the Mother
    # Computer (without the "D kills" dip, which stops the final fight) to its death, its life set
    # to 10 at frame 30000.
    function(pd_mslug4_bossbar name frames script dips poke expect)
        add_test(NAME mslug4_bossbar_${name}
                 COMMAND mslug4 --roms ${PD_MS4_ROMS} --headless --frames ${frames} --no-audio --script ${script}
                         --date 2026-10-23-19-12-17-5)
        string(REPLACE ";" "\;" poke "${poke}")
        set_tests_properties(mslug4_bossbar_${name} PROPERTIES LABELS mslug4
            ENVIRONMENT "PD_BOSSBAR=1;PD_DEVMODE=1;PD_DIPS=${dips};PD_POKE=${poke}"
            PASS_REGULAR_EXPRESSION "${expect}")
    endfunction()
    # Boss Rush (docs/BOSS_RUSH.md, stage 3): the seven bosses in a row in one game, invincible,
    # with stage 3's bot (D every 100 frames until the final section, then across the screen
    # without D: the dip would stop the final fight); each boss section leads to the next and the
    # last one to the game's own ending. And the unlock: completing the game (mission 5 -> 6) from
    # the final section, the Mother Computer's life set to 10 at frame 30000.
    set(botr ${PD_MS4_ANALYSIS}/boss_rush/stage3/bot_rush.txt)
    if(EXISTS "${botr}" AND EXISTS "${botl}")
        add_test(NAME mslug4_bossrush_full
                 COMMAND mslug4 --roms ${PD_MS4_ROMS} --headless --no-audio --frames 39800 --script ${botr}
                         --date 2026-10-23-19-12-17-5)
        set_tests_properties(mslug4_bossrush_full PROPERTIES LABELS mslug4
            ENVIRONMENT "PD_BOSSRUSH=1;PD_DEVMODE=1;PD_DIPS=8009"
            PASS_REGULAR_EXPRESSION "bossrush 11090: next boss, 1/7 bosses defeated
bossrush 12442: next boss, 2/7 bosses defeated
bossrush 14196: next boss, 3/7 bosses defeated
bossrush 16229: next boss, 4/7 bosses defeated
bossrush 22012: next boss, 5/7 bosses defeated
bossrush 23411: next boss, 6/7 bosses defeated
bossrush 39684: cleared, 7/7 bosses defeated")
        add_test(NAME mslug4_progress_completed_reset
                 COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_CURRENT_BINARY_DIR}/mslug4_progress_completed.ini)
        set_tests_properties(mslug4_progress_completed_reset PROPERTIES LABELS mslug4 FIXTURES_SETUP mslug4_progress_completed)
        add_test(NAME mslug4_progress_game_completed
                 COMMAND mslug4 --roms ${PD_MS4_ROMS} --headless --no-audio --frames 32300 --script ${botl}
                         --date 2026-10-23-19-12-17-5)
        set_tests_properties(mslug4_progress_game_completed PROPERTIES LABELS mslug4 FIXTURES_REQUIRED mslug4_progress_completed
            ENVIRONMENT "PD_PROGRESS=${CMAKE_CURRENT_BINARY_DIR}/mslug4_progress_completed.ini;PD_DEVMODE=1;PD_DIPS=8000;PD_POKE=4600,10ED62,5\;4600,10ED63,A\;30000,1034EA,0\;30000,1034EB,A"
            PASS_REGULAR_EXPRESSION "game completed at frame 32171")
    endif()
    if(EXISTS "${botd}" AND EXISTS "${botl}")
        pd_mslug4_bossbar(m4 7000 ${botd} 8009 "4600,10ED62,3;4600,10ED63,6"
            "bossbar 5602: BIG JOHN 1014FC 2900/3000.*bossbar 6921: BIG JOHN 1014FC 0/3000 dead")
        pd_mslug4_bossbar(m1 11200 ${botd} 8009 "4600,10ED62,0;4600,10ED63,3"
            "bossbar 6403: BURA GARIAE 10186C 750/850.*BURA GARIAE 10186C 0/1600 dead")
        pd_mslug4_bossbar(final 30100 ${botl} 8000 "4600,10ED62,5;4600,10ED63,A;30000,1034EA,0;30000,1034EB,A"
            "MOTHER COMPUTER 10349C 10/6400.*MOTHER COMPUTER 10349C 0/6400 dead")
    endif()
    if(EXISTS "${botd}")
        add_test(NAME mslug4_progress_mission_unlocked
                 COMMAND mslug4 --roms ${PD_MS4_ROMS} --headless --no-audio --frames 11200 --script ${botd}
                         --date 2026-10-24-01-00-00-6)
        add_test(NAME mslug4_progress_m1_reset
                 COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_CURRENT_BINARY_DIR}/mslug4_progress_m1.ini)
        set_tests_properties(mslug4_progress_m1_reset PROPERTIES LABELS mslug4 FIXTURES_SETUP mslug4_progress_m1)
        set_tests_properties(mslug4_progress_mission_unlocked PROPERTIES LABELS mslug4 FIXTURES_REQUIRED mslug4_progress_m1
            ENVIRONMENT "PD_PROGRESS=${CMAKE_CURRENT_BINARY_DIR}/mslug4_progress_m1.ini;PD_DEVMODE=1;PD_DIPS=8009;PD_POKE=4600,10ED62,0\;4600,10ED63,3"
            PASS_REGULAR_EXPRESSION "mission 2 unlocked at frame 11090")
        add_test(NAME mslug4_records_slowdown_off
                 COMMAND mslug4 --roms ${PD_MS4_ROMS} --headless --no-audio --turbo 2 --frames 11200 --script ${botd}
                         --date 2026-10-24-01-00-00-6)
        set_tests_properties(mslug4_records_slowdown_off PROPERTIES LABELS mslug4
            ENVIRONMENT "PD_RECORDS=${CMAKE_CURRENT_BINARY_DIR}/mslug4_records_turbo.ini;PD_RECORDS_MIN_FRAMES=600;PD_DEVMODE=1;PD_DIPS=8009;PD_POKE=4600,10ED62,0\;4600,10ED63,3"
            PASS_REGULAR_EXPRESSION "mission 1 completed at frame [0-9]+: .*\\(not clean: slowdown off\\)")
    endif()
    pd_mslug4_cheat_run(vehicles "6700,vehicle,0,0")
    pd_mslug4_cheat_run(forms "6700,form,0,2;6900,form,0,0;7150,form,0,3;7450,form,0,0;7750,form,0,7;8050,form,0,0;8350,form,0,8;8650,form,0,0")
    # Phase 3: games recorded by hand in MAME (grabar_partida.bat, empty NVRAM), replayed by
    # mame_run.py --replay --name NAME into PD_MS4_ANALYSIS/phase3/play: mslug4_play_<NAME>_vs_mame.
    file(GLOB played ${PD_MS4_ANALYSIS}/phase3/play/*_replay_fingerprints.txt)
    foreach(fp ${played})
        string(REGEX REPLACE "_replay_fingerprints\\.txt$" "" base "${fp}")
        get_filename_component(run "${base}" NAME)
        if(EXISTS "${base}_date.txt" AND EXISTS "${base}_frames.txt")
            file(READ "${base}_date.txt" date)
            file(READ "${base}_frames.txt" frames)
            string(STRIP "${date}" date)
            string(STRIP "${frames}" frames)
            set(known)
            if(EXISTS "${base}_known_vblank.txt")
                set(known --known-differences ${base}_known_vblank.txt)
            endif()
            add_test(NAME mslug4_play_${run}_vs_mame
                     COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                             --exe $<TARGET_FILE:mslug4> --roms ${PD_MS4_ROMS}
                             --inputs ${base}_replay_inputs.txt --reference ${fp}
                             --frames ${frames} --date ${date} ${known}
                             --output ${CMAKE_CURRENT_BINARY_DIR}/mslug4_play_${run}_fingerprints_port.txt)
            set_tests_properties(mslug4_play_${run}_vs_mame PROPERTIES LABELS mslug4)
        endif()
    endforeach()
else()
    message(STATUS "Metal Slug 4 GOG test disabled: the user's copy (PD_MS4_ROMS/gog) is missing")
endif()
if(EXISTS "${PD_MS4_ROMS}/mslug4h/263-ph1.p1")
    set(MS4_SET ${CMAKE_CURRENT_BINARY_DIR}/mslug4_mameset)
    add_test(NAME mslug4_roms_mame_make
             COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tests/loader/link_set.py ${PD_MS4_ROMS} ${MS4_SET}
                     mslug4h/263-ph1.p1 mslug4h/263-ph2.sp2 mslug4h/263-m1.m1 mslug4h/263-v1.v1
                     mslug4h/263-v2.v2 mslug4h/263-c1.c1 mslug4h/263-c2.c2 mslug4h/263-c3.c3
                     mslug4h/263-c4.c4 mslug4h/263-c5.c5 mslug4h/263-c6.c6
                     neogeo/sp-u2.sp1 neogeo/000-lo.lo neogeo/sfix.sfix neogeo/sm1.sm1)
    set_tests_properties(mslug4_roms_mame_make PROPERTIES FIXTURES_SETUP mslug4_mameset LABELS mslug4)
    # Phase 5 (docs/PHASE5_LOADER.md): the set decrypted at load (src/games/mslug4/crypt.c), the
    # recompiled program and the profile on (the same P1); loose and zipped (the game's zip finds
    # neogeo.zip next to it), every part identical to the GOG copy's (PD_COMPARE_ROMS).
    add_test(NAME mslug4_roms_mame COMMAND mslug4 --roms ${MS4_SET} --headless --frames 1 --no-audio)
    set_tests_properties(mslug4_roms_mame PROPERTIES LABELS mslug4 FIXTURES_REQUIRED mslug4_mameset
                         ENVIRONMENT "PD_ROMS_INFO=1"
                         PASS_REGULAR_EXPRESSION "MAME set, GOG program, BIOS us\n(recompiled|interpreter)\nprofile supported")
    if(EXISTS "${PD_MS4_ROMS}/gog/p1.bin")
        add_test(NAME mslug4_roms_mame_loose COMMAND mslug4 --roms ${PD_MS4_ROMS} --headless --frames 1 --no-audio)
        set_tests_properties(mslug4_roms_mame_loose PROPERTIES LABELS mslug4 FIXTURES_REQUIRED mslug4_mameset
                             ENVIRONMENT "PD_COMPARE_ROMS=${MS4_SET}")
        set(MS4_ZIPS ${CMAKE_CURRENT_BINARY_DIR}/mslug4_mame_zips)
        add_test(NAME mslug4_roms_mame_zips_make
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tests/loader/zip_set.py ${MS4_SET} ${MS4_ZIPS} mslug4h)
        set_tests_properties(mslug4_roms_mame_zips_make PROPERTIES LABELS mslug4 FIXTURES_REQUIRED mslug4_mameset
                             FIXTURES_SETUP mslug4_mame_zips)
        add_test(NAME mslug4_roms_mame_zip COMMAND mslug4 --roms ${PD_MS4_ROMS} --headless --frames 1 --no-audio)
        set_tests_properties(mslug4_roms_mame_zip PROPERTIES LABELS mslug4 FIXTURES_REQUIRED mslug4_mame_zips
                             ENVIRONMENT "PD_COMPARE_ROMS=${MS4_ZIPS}/mslug4h.zip")
    endif()
else()
    message(STATUS "Metal Slug 4 MAME test disabled: the rebuilt set (PD_MS4_ROMS/mslug4h) is missing")
endif()
endif()

# ------------------------------------------------------------------ Metal Slug 5 tests
# The Metal Slug 5 regression target's tests (its workspace's set, reference files and MAME runs).
if(PD_HAVE_MSLUG5)
# Phase 1: the MAME set alone (mslug5.zip and neogeo\, linked into the build folder) is
# recognized, every file by CRC-32 and SHA-1. Its decryption is not in the runtime yet, so the
# loader stops there and says so.
if(EXISTS "${PD_MS5_ROMS}/mslug5.zip")
    set(MS5_SET ${CMAKE_CURRENT_BINARY_DIR}/mslug5_mameset)
    add_test(NAME mslug5_roms_mame_make
             COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tests/loader/link_set.py ${PD_MS5_ROMS} ${MS5_SET}
                     mslug5.zip neogeo/sp-u2.sp1 neogeo/000-lo.lo neogeo/sfix.sfix neogeo/sm1.sm1)
    set_tests_properties(mslug5_roms_mame_make PROPERTIES FIXTURES_SETUP mslug5_mameset LABELS mslug5)
    # Phase 5: the set loads, decrypted at load (src/games/mslug5/crypt.c), with the recompiled
    # program and the profile (the same P1 and BIOS as the reference files).
    add_test(NAME mslug5_roms_mame COMMAND mslug5 --roms ${MS5_SET} --headless --frames 1 --no-audio)
    set_tests_properties(mslug5_roms_mame PROPERTIES LABELS mslug5 FIXTURES_REQUIRED mslug5_mameset
                         ENVIRONMENT "PD_ROMS_INFO=1"
                         PASS_REGULAR_EXPRESSION "MAME set, arcade program, BIOS us\n.*profile supported")
    # ...and decrypts to exactly the decrypted reference files (every region), zipped and loose.
    if(EXISTS "${PD_MS5_ROMS}/mslug5_game_m68k")
        add_test(NAME mslug5_roms_mame_zip COMMAND mslug5 --roms ${PD_MS5_ROMS} --headless --frames 1 --no-audio)
        set_tests_properties(mslug5_roms_mame_zip PROPERTIES LABELS mslug5 FIXTURES_REQUIRED mslug5_mameset
                             ENVIRONMENT "PD_COMPARE_ROMS=${MS5_SET}")
        set(MS5_LOOSE ${CMAKE_CURRENT_BINARY_DIR}/mslug5_mameset_loose)
        add_test(NAME mslug5_roms_mame_loose_make
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tests/loader/link_set.py ${PD_MS5_ROMS} ${MS5_LOOSE}
                         mslug5.zip! neogeo/sp-u2.sp1 neogeo/000-lo.lo neogeo/sfix.sfix neogeo/sm1.sm1)
        set_tests_properties(mslug5_roms_mame_loose_make PROPERTIES FIXTURES_SETUP mslug5_mameloose LABELS mslug5)
        add_test(NAME mslug5_roms_mame_loose COMMAND mslug5 --roms ${PD_MS5_ROMS} --headless --frames 1 --no-audio)
        set_tests_properties(mslug5_roms_mame_loose PROPERTIES LABELS mslug5 FIXTURES_REQUIRED mslug5_mameloose
                             ENVIRONMENT "PD_COMPARE_ROMS=${MS5_LOOSE}")
    endif()
else()
    message(STATUS "Metal Slug 5 tests disabled: its set (PD_MS5_ROMS/mslug5.zip) is missing")
endif()
# Phase 2: the decrypted reference files (tools/make_reference.py) load, every one with its hash.
if(EXISTS "${PD_MS5_ROMS}/mslug5_game_m68k")
    add_test(NAME mslug5_roms_reference COMMAND mslug5 --roms ${PD_MS5_ROMS} --headless --frames 1 --no-audio)
    set_tests_properties(mslug5_roms_reference PROPERTIES ENVIRONMENT "PD_ROMS_INFO=1" LABELS mslug5
                         PASS_REGULAR_EXPRESSION "Decrypted reference files \\(MAME's regions\\), arcade program, BIOS us\n")
    # Scripted runs against MAME (tools/mame_run.py --game mslug5, references in PD_MS5_ANALYSIS/phase2):
    #   attract: power-on with empty NVRAM, BIOS and the attract demo;
    #   start: a credit, START, the game's "How to play", soldier select, Mission 1 with a bot.
    # RAM, palette and image on every VBlank / frame (verify_mission.py); <run>_known_vblank.txt
    # lists the VBlanks that differ only in the stack (docs/PHASE2_HARDWARE.md).
    # Phase 4 (docs/PHASE4_PROFILE.md): phase4/debug/* the same game with the developer mode,
    # debug dips or red blood (<run>_env.txt: PD_DEVMODE, PD_DIPS, PD_BLOOD...), phase4/missions/*
    # started at another mission (<run>_poke.txt: PD_POKE).
    # boss_rush/stage1/boss_* (docs/BOSS_RUSH.md): the same started at each boss section, invincible.
    file(GLOB ms5_phase4_runs ${PD_MS5_ANALYSIS}/phase4/debug/*_fingerprints.txt
                              ${PD_MS5_ANALYSIS}/phase4/missions/mission*_fingerprints.txt
                              ${PD_MS5_ANALYSIS}/boss_rush/stage1/boss_*_fingerprints.txt)
    set(ms5_runs ${PD_MS5_ANALYSIS}/phase2/attract/attract ${PD_MS5_ANALYSIS}/phase2/start/start)
    foreach(fp ${ms5_phase4_runs})
        if(NOT fp MATCHES "_replay_fingerprints\\.txt$")
            string(REGEX REPLACE "_fingerprints\\.txt$" "" base "${fp}")
            list(APPEND ms5_runs ${base})
        endif()
    endforeach()
    foreach(base ${ms5_runs})
        get_filename_component(run "${base}" NAME)
        if(EXISTS "${base}_fingerprints.txt" AND EXISTS "${base}_date.txt" AND EXISTS "${base}_frames.txt")
            file(READ "${base}_date.txt" date)
            file(READ "${base}_frames.txt" frames)
            string(STRIP "${date}" date)
            string(STRIP "${frames}" frames)
            set(known)
            if(EXISTS "${base}_known_vblank.txt")
                set(known --known-differences ${base}_known_vblank.txt)
            endif()
            set(env)
            if(EXISTS "${base}_poke.txt")
                file(READ "${base}_poke.txt" poke)
                string(STRIP "${poke}" poke)
                string(REPLACE ";" "\;" poke "${poke}")     # several writes: kept in one variable
                set(env "PD_POKE=${poke}")
            endif()
            if(EXISTS "${base}_env.txt")                  # PD_DEVMODE, PD_DIPS... (mame_run.py)
                file(STRINGS "${base}_env.txt" run_env)
                list(APPEND env ${run_env})
            endif()
            add_test(NAME mslug5_${run}_vs_mame
                     COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                             --exe $<TARGET_FILE:mslug5> --roms ${PD_MS5_ROMS}
                             --inputs ${base}_inputs.txt --reference ${base}_fingerprints.txt
                             --frames ${frames} --date ${date} ${known}
                             --output ${CMAKE_CURRENT_BINARY_DIR}/mslug5_${run}_fingerprints_port.txt)
            set_tests_properties(mslug5_${run}_vs_mame PROPERTIES LABELS mslug5 ENVIRONMENT "${env}")
        endif()
    endforeach()
    # Phase 4: the "Start mission" option (PD_START_MISSION) against the MAME run that starts at
    # Mission 4: identical images; RAM differs while the option has written the mission and the
    # MAME run has not yet (mission4_option_known_vblank.txt).
    set(m4 ${PD_MS5_ANALYSIS}/phase4/missions/mission4)
    if(EXISTS "${m4}_option_known_vblank.txt" AND EXISTS "${m4}_fingerprints.txt")
        file(READ "${m4}_date.txt" date)
        file(READ "${m4}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        add_test(NAME mslug5_start_mission_option_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslug5> --roms ${PD_MS5_ROMS}
                         --inputs ${m4}_inputs.txt --reference ${m4}_fingerprints.txt
                         --frames ${frames} --date ${date} --known-differences ${m4}_option_known_vblank.txt
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug5_start_mission_option_fingerprints_port.txt)
        set_tests_properties(mslug5_start_mission_option_vs_mame PROPERTIES LABELS mslug5 ENVIRONMENT "PD_START_MISSION=4")
    endif()
    # Phase 4: the cheats (PD_CHEAT) against a MAME run that writes the same bytes at the same
    # moments (PD_POKE in cheats_poke.txt): the Heavy Machine Gun and the Enemy Chaser through the
    # game's own routine (the bytes it changed, PD_SPAWN_POKES), 63 bombs, 7 lives, Eri.
    set(ch ${PD_MS5_ANALYSIS}/phase4/cheats/cheats)
    if(EXISTS "${ch}_fingerprints.txt" AND EXISTS "${ch}_known_vblank.txt")
        file(READ "${ch}_date.txt" date)
        file(READ "${ch}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        add_test(NAME mslug5_cheats_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslug5> --roms ${PD_MS5_ROMS}
                         --inputs ${ch}_inputs.txt --reference ${ch}_fingerprints.txt
                         --frames ${frames} --date ${date} --known-differences ${ch}_known_vblank.txt
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug5_cheats_fingerprints_port.txt)
        set_tests_properties(mslug5_cheats_vs_mame PROPERTIES LABELS mslug5
                             ENVIRONMENT "PD_CHEAT=6700,weapon,0,5\;7300,weapon,0,12\;7500,bombs,0,63\;7650,lives,0,7\;7800,soldier,0,1")
    endif()
    # The vehicle cheat (phase4/vehicles): the Metal Slug spawned at frame 6700 (ridden by the bot);
    # the MAME run writes the RAM bytes the spawn changed (PD_SPAWN_POKES) at the same frame.
    set(ve ${PD_MS5_ANALYSIS}/phase4/vehicles/vehicles)
    if(EXISTS "${ve}_fingerprints.txt" AND EXISTS "${ve}_known_vblank.txt")
        file(READ "${ve}_date.txt" date)
        file(READ "${ve}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        add_test(NAME mslug5_vehicles_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslug5> --roms ${PD_MS5_ROMS}
                         --inputs ${ve}_inputs.txt --reference ${ve}_fingerprints.txt
                         --frames ${frames} --date ${date} --known-differences ${ve}_known_vblank.txt
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug5_vehicles_fingerprints_port.txt)
        set_tests_properties(mslug5_vehicles_vs_mame PROPERTIES LABELS mslug5 ENVIRONMENT "PD_CHEAT=6700,vehicle,0,0")
    endif()
    # The fat cheat (phase4/forms): fat at frame 6700, normal at 6900; the MAME run writes the bytes
    # the cheat wrote (PD_SPAWN_POKES: the requested form and the food counter every frame).
    set(fo ${PD_MS5_ANALYSIS}/phase4/forms/forms)
    if(EXISTS "${fo}_fingerprints.txt" AND EXISTS "${fo}_known_vblank.txt")
        file(READ "${fo}_date.txt" date)
        file(READ "${fo}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        add_test(NAME mslug5_forms_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslug5> --roms ${PD_MS5_ROMS}
                         --inputs ${fo}_inputs.txt --reference ${fo}_fingerprints.txt
                         --frames ${frames} --date ${date} --known-differences ${fo}_known_vblank.txt
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug5_forms_fingerprints_port.txt)
        set_tests_properties(mslug5_forms_vs_mame PROPERTIES LABELS mslug5 ENVIRONMENT "PD_CHEAT=6700,form,0,2\;6900,form,0,0")
    endif()
    # Boss health bar (docs/BOSS_RUSH.md, stage 2): the Mission 1 boss section, the tank's life set
    # to 10 at frame 7000; the bar follows it to its death (PD_BOSSBAR prints the bar's changes).
    add_test(NAME mslug5_bossbar_m1
             COMMAND mslug5 --roms ${PD_MS5_ROMS} --headless --frames 7200 --no-audio
                     --script ${PD_MS5_ANALYSIS}/phase2/start/start_script.txt --date 2026-10-18-00-59-09-0)
    set_tests_properties(mslug5_bossbar_m1 PROPERTIES LABELS mslug5
        ENVIRONMENT "PD_BOSSBAR=1;PD_DEVMODE=1;PD_DIPS=8000;PD_POKE=5800,10D680,0\;5800,10D681,8\;7000,101F00,0\;7000,101F01,A"
        PASS_REGULAR_EXPRESSION "bossbar 7000: boss 101EAC 10/5000
bossbar 7020: boss 101EAC 0/5000 dead")
    # Boss Rush (docs/BOSS_RUSH.md, stage 3): every confirmed boss in a row in one game, with the
    # "D kills every enemy" dip and a bot (analysis/boss_rush/stage3/bot_d.txt): each boss section
    # leads to the next, the last one to the ending. And the unlock: completing the game (the last
    # mission's step, 4 -> 5) from the final boss's section.
    set(rush3 ${PD_MS5_ANALYSIS}/boss_rush/stage3/bot_d.txt)
    if(EXISTS "${rush3}")
        add_test(NAME mslug5_bossrush_full
                 COMMAND mslug5 --roms ${PD_MS5_ROMS} --headless --no-audio --frames 14200 --script ${rush3}
                         --date 2026-10-18-00-59-09-0)
        set_tests_properties(mslug5_bossrush_full PROPERTIES LABELS mslug5
            ENVIRONMENT "PD_BOSSRUSH=1;PD_DEVMODE=1;PD_DIPS=8009"
            PASS_REGULAR_EXPRESSION "bossrush 7573: next boss, 1/5 bosses defeated
bossrush 8855: next boss, 2/5 bosses defeated
bossrush 9786: next boss, 3/5 bosses defeated
bossrush 11415: next boss, 4/5 bosses defeated
bossrush 14119: cleared, 5/5 bosses defeated")
        add_test(NAME mslug5_progress_game_completed
                 COMMAND mslug5 --roms ${PD_MS5_ROMS} --headless --no-audio --frames 9100 --script ${rush3}
                         --date 2026-10-18-00-59-09-0)
        set_tests_properties(mslug5_progress_game_completed PROPERTIES LABELS mslug5
            ENVIRONMENT "PD_PROGRESS=${CMAKE_CURRENT_BINARY_DIR}/mslug5_progress_completed.ini;PD_DEVMODE=1;PD_DIPS=8009;PD_POKE=5800,10D680,4\;5800,10D681,6"
            PASS_REGULAR_EXPRESSION "game completed at frame 9017")
        # A mission completed with the slowdown off is not recorded, and says why.
        add_test(NAME mslug5_records_slowdown_off
                 COMMAND mslug5 --roms ${PD_MS5_ROMS} --headless --no-audio --turbo 2 --frames 9100 --script ${rush3}
                         --date 2026-10-18-00-59-09-0)
        set_tests_properties(mslug5_records_slowdown_off PROPERTIES LABELS mslug5
            ENVIRONMENT "PD_RECORDS=${CMAKE_CURRENT_BINARY_DIR}/mslug5_records_turbo.ini;PD_RECORDS_MIN_FRAMES=600;PD_DEVMODE=1;PD_DIPS=8009;PD_POKE=5800,10D680,4\;5800,10D681,6"
            PASS_REGULAR_EXPRESSION "mission 5 completed at frame [0-9]+: .*\\(not clean: slowdown off\\)")
    endif()
    # Mission mode's direct start (cheats.h, cheats_mission_from): the coin right after the boot, no
    # "How to play", Mission 3 written again after the new game clears it and loaded after the
    # soldier select (the section byte 0xFF at frame 2707).
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/mslug5_mission_direct.txt "301 306 coin1\n351 356 start1\n")
    add_test(NAME mslug5_mission_mode_direct
             COMMAND mslug5 --roms ${PD_MS5_ROMS} --headless --no-audio --frames 3000
                     --script ${CMAKE_CURRENT_BINARY_DIR}/mslug5_mission_direct.txt --date 2026-10-18-00-59-09-0)
    set_tests_properties(mslug5_mission_mode_direct PROPERTIES LABELS mslug5 ENVIRONMENT "PD_MISSION_ONCE=3"
        PASS_REGULAR_EXPRESSION "start mission 3 written at frame 270[0-9]")
    # Phase 4: the sound menu (src/games/mslug5/sounds.c, the profile's driver modes) is audible in
    # the middle of the user's game and, after closing it, the game stays identical.
    set(play1 ${PD_MS5_ANALYSIS}/phase3/play/partida1)
    if(EXISTS "${play1}_replay_inputs.txt" AND EXISTS "${play1}_date.txt")
        file(READ "${play1}_date.txt" date)
        string(STRIP "${date}" date)
        add_test(NAME mslug5_sound_menu
                 COMMAND mslug5 --roms ${PD_MS5_ROMS} --headless --frames 20000 --test-sounds
                         --inputs ${play1}_replay_inputs.txt --date ${date})
        set_tests_properties(mslug5_sound_menu PROPERTIES LABELS mslug5)
    endif()
    # Phase 3: games recorded by hand in MAME (grabar_partida.bat, empty NVRAM), replayed by
    # mame_run.py --replay --name NAME into PD_MS5_ANALYSIS/phase3/play: mslug5_play_<NAME>_vs_mame.
    # <NAME>_known_vblank.txt: VBlanks that differ only in the stack (docs/PHASE3_RECOMP.md).
    file(GLOB played ${PD_MS5_ANALYSIS}/phase3/play/*_replay_fingerprints.txt)
    foreach(fp ${played})
        string(REGEX REPLACE "_replay_fingerprints\\.txt$" "" base "${fp}")
        get_filename_component(run "${base}" NAME)
        if(EXISTS "${base}_date.txt" AND EXISTS "${base}_frames.txt")
            file(READ "${base}_date.txt" date)
            file(READ "${base}_frames.txt" frames)
            string(STRIP "${date}" date)
            string(STRIP "${frames}" frames)
            set(known)
            if(EXISTS "${base}_known_vblank.txt")
                set(known --known-differences ${base}_known_vblank.txt)
            endif()
            add_test(NAME mslug5_play_${run}_vs_mame
                     COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                             --exe $<TARGET_FILE:mslug5> --roms ${PD_MS5_ROMS}
                             --inputs ${base}_replay_inputs.txt --reference ${fp}
                             --frames ${frames} --date ${date} ${known}
                             --output ${CMAKE_CURRENT_BINARY_DIR}/mslug5_play_${run}_fingerprints_port.txt)
            set_tests_properties(mslug5_play_${run}_vs_mame PROPERTIES LABELS mslug5)
        endif()
    endforeach()
endif()
endif()

# ------------------------------------------------------------------ Metal Slug 3 tests
if(PD_HAVE_MSLUG3)
    # The GOG copy loads, every file with its reference hash (verified), as the GOG program.
    add_test(NAME mslug3_roms_gog COMMAND mslug3 --roms ${PD_MS3_ROMS} --headless --frames 1 --no-audio)
    set_tests_properties(mslug3_roms_gog PROPERTIES ENVIRONMENT "PD_ROMS_INFO=1"
                         PASS_REGULAR_EXPRESSION "PC release files \\(DotEmu\\), GOG program, BIOS us\n"
                         LABELS mslug3)
    # Phase 5: a MAME set (encrypted: NEO-SMA program, CMC42 sprites and fix; src/games/mslug3/crypt.c)
    # loads the same bytes as the GOG copy, loose and zipped. The test set lives in
    # PD_MS3_ANALYSIS/phase5/mameset (mslug3 + neogeo, never versioned). Known difference: the GOG
    # system fix differs from MAME's sfix.sfix in 28 bytes (docs/ROM_MAP.md).
    set(mameset ${PD_MS3_ANALYSIS}/phase5/mameset)
    if(EXISTS "${mameset}/mslug3/256-pg1.p1")
        add_test(NAME mslug3_roms_mame_folder COMMAND mslug3 --roms ${PD_MS3_ROMS} --headless --frames 1 --no-audio)
        set_tests_properties(mslug3_roms_mame_folder PROPERTIES LABELS mslug3
                             ENVIRONMENT "PD_COMPARE_ROMS=${mameset};PD_COMPARE_KNOWN=fix_bios")
        set(MS3_ZIPS ${CMAKE_CURRENT_BINARY_DIR}/mslug3_mame_zips)
        add_test(NAME mslug3_roms_mame_zips_make
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tests/loader/zip_set.py ${mameset} ${MS3_ZIPS} mslug3)
        set_tests_properties(mslug3_roms_mame_zips_make PROPERTIES FIXTURES_SETUP mslug3_mame_zips LABELS mslug3)
        add_test(NAME mslug3_roms_mame_zip COMMAND mslug3 --roms ${PD_MS3_ROMS} --headless --frames 1 --no-audio)
        set_tests_properties(mslug3_roms_mame_zip PROPERTIES LABELS mslug3 FIXTURES_REQUIRED mslug3_mame_zips
                             ENVIRONMENT "PD_COMPARE_ROMS=${MS3_ZIPS}/mslug3.zip;PD_COMPARE_KNOWN=fix_bios")
    endif()

    # Scripted runs against MAME (tools/mame_run.py writes the references outside the repo):
    #   phase2/attract: power-on with empty NVRAM (the first boot ends with a watchdog reset), BIOS
    #     and the attract demo;
    #   phase2/start: a credit, START, soldier select and Mission 1 with a simple bot until game over;
    #   phase4/missions/mission2-5 (Phase 4): the same, starting at another mission (<run>_poke.txt:
    #     PD_POKE writes the start mission during the soldier select, as the game's memory card
    #     continue does);
    #   phase4/debug/* (Phase 4): the same with the developer mode and debug dips (<run>_env.txt);
    #   boss_rush/stage1/boss_* (docs/BOSS_RUSH.md): the same started at each boss section (the
    #     mission and section bytes written during the soldier select), invincible. Not the final
    #     boss's section (boss_m5b): the first real divergence from MAME, still open (CLAUDE.md).
    # Each run: RAM, palette and image (verify_mission.py) and, with mame_run.py --wav, the audio
    # sample for sample (verify_audio.py).
    file(GLOB mission_runs ${PD_MS3_ANALYSIS}/phase4/missions/mission*_fingerprints.txt
                           ${PD_MS3_ANALYSIS}/phase4/debug/*_fingerprints.txt
                           ${PD_MS3_ANALYSIS}/boss_rush/stage1/boss_m[1-4]_fingerprints.txt
                           ${PD_MS3_ANALYSIS}/boss_rush/stage1/boss_m5a_fingerprints.txt)
    set(runs ${PD_MS3_ANALYSIS}/phase2/attract ${PD_MS3_ANALYSIS}/phase2/start)
    foreach(fp ${mission_runs})
        string(REGEX REPLACE "_fingerprints\\.txt$" "" base "${fp}")
        list(APPEND runs ${base})
    endforeach()
    foreach(base ${runs})
        get_filename_component(run "${base}" NAME)
        if(EXISTS "${base}_fingerprints.txt" AND EXISTS "${base}_date.txt" AND EXISTS "${base}_frames.txt")
            file(READ "${base}_date.txt" date)
            file(READ "${base}_frames.txt" frames)
            string(STRIP "${date}" date)
            string(STRIP "${frames}" frames)
            set(known)                                      # see mslug3_play_<NAME>_vs_mame
            if(EXISTS "${base}_known_vblank.txt")
                set(known --known-differences ${base}_known_vblank.txt)
            endif()
            set(env)
            if(EXISTS "${base}_poke.txt")
                file(READ "${base}_poke.txt" poke)
                string(STRIP "${poke}" poke)
                string(REPLACE ";" "\;" poke "${poke}")     # several writes: kept in one variable
                set(env "PD_POKE=${poke}")
            endif()
            if(EXISTS "${base}_env.txt")                  # PD_DEVMODE, PD_DIPS... (mame_run.py)
                file(STRINGS "${base}_env.txt" run_env)
                list(APPEND env ${run_env})
            endif()
            add_test(NAME mslug3_${run}_vs_mame
                     COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                             --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                             --inputs ${base}_inputs.txt --reference ${base}_fingerprints.txt
                             --frames ${frames} --date ${date} ${known}
                             --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_${run}_fingerprints_port.txt)
            set_tests_properties(mslug3_${run}_vs_mame PROPERTIES LABELS mslug3 ENVIRONMENT "${env}")
            if(run STREQUAL "start")                      # run-ahead (runahead.c) leaves the game unchanged
                add_test(NAME mslug3_start_runahead_vs_mame
                         COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                                 --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                                 --inputs ${base}_inputs.txt --reference ${base}_fingerprints.txt
                                 --frames ${frames} --date ${date} ${known}
                                 --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_start_runahead_fingerprints_port.txt)
                add_test(NAME mslug3_start_runahead_audio_vs_mame
                         COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_audio.py
                                 --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                                 --inputs ${base}_inputs.txt --reference ${base}_mame.wav
                                 --frames ${frames} --date ${date}
                                 --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_start_runahead_port.wav)
                set_tests_properties(mslug3_start_runahead_vs_mame mslug3_start_runahead_audio_vs_mame
                                     PROPERTIES LABELS mslug3 ENVIRONMENT "PD_RUNAHEAD=2")
            endif()
            if(EXISTS "${base}_mame.wav")
                add_test(NAME mslug3_${run}_audio_vs_mame
                         COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_audio.py
                                 --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                                 --inputs ${base}_inputs.txt --reference ${base}_mame.wav
                                 --frames ${frames} --date ${date}
                                 --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_${run}_port.wav)
                set_tests_properties(mslug3_${run}_audio_vs_mame PROPERTIES LABELS mslug3 ENVIRONMENT "${env}")
            endif()
        endif()
    endforeach()
    # Phase 4: the "Start mission" option (cheats.c, PD_START_MISSION in headless mode) against
    # the MAME run that starts at Mission 4: identical images and audio; RAM differs only while the
    # option has written the mission and the MAME run has not yet (mission4_option_known_vblank.txt).
    set(m4 ${PD_MS3_ANALYSIS}/phase4/missions/mission4)
    if(EXISTS "${m4}_option_known_vblank.txt" AND EXISTS "${m4}_mame.wav")
        file(READ "${m4}_date.txt" date)
        file(READ "${m4}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        add_test(NAME mslug3_start_mission_option_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                         --inputs ${m4}_inputs.txt --reference ${m4}_fingerprints.txt
                         --frames ${frames} --date ${date} --known-differences ${m4}_option_known_vblank.txt
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_start_mission_option_fingerprints_port.txt)
        add_test(NAME mslug3_start_mission_option_audio_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_audio.py
                         --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                         --inputs ${m4}_inputs.txt --reference ${m4}_mame.wav --frames ${frames} --date ${date}
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_start_mission_option_port.wav)
        set_tests_properties(mslug3_start_mission_option_vs_mame mslug3_start_mission_option_audio_vs_mame
                             PROPERTIES LABELS mslug3 ENVIRONMENT "PD_START_MISSION=4")
    endif()
    # Phase 4: the RAM cheats (cheats.c through PD_CHEAT) against a MAME run that writes the same
    # bytes at the same moments (PD_POKE in <run>_poke.txt): the Heavy Machine Gun's item (the
    # bytes its spawn changed, PD_SPAWN_POKES), picked up by the bot; the pistol given directly
    # (with its form); bombs, lives, soldier.
    set(ch ${PD_MS3_ANALYSIS}/phase4/cheats/cheats)
    if(EXISTS "${ch}_fingerprints.txt" AND EXISTS "${ch}_mame.wav")
        file(READ "${ch}_date.txt" date)
        file(READ "${ch}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        set(known)
        if(EXISTS "${ch}_known_vblank.txt")
            set(known --known-differences ${ch}_known_vblank.txt)
        endif()
        add_test(NAME mslug3_cheats_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                         --inputs ${ch}_inputs.txt --reference ${ch}_fingerprints.txt
                         --frames ${frames} --date ${date} ${known}
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_cheats_fingerprints_port.txt)
        add_test(NAME mslug3_cheats_audio_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_audio.py
                         --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                         --inputs ${ch}_inputs.txt --reference ${ch}_mame.wav --frames ${frames} --date ${date}
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_cheats_port.wav)
        set_tests_properties(mslug3_cheats_vs_mame mslug3_cheats_audio_vs_mame PROPERTIES LABELS mslug3
                             ENVIRONMENT "PD_CHEAT=3300,weapon,0,5\;3500,weapon,0,0\;3600,bombs,0,63\;3900,lives,0,7\;4200,soldier,0,1")
    endif()
    # The form cheat the same way (phase4/forms/run): fat (its food counter kept every frame),
    # mummy, normal, zombie, normal, snowman.
    set(fo ${PD_MS3_ANALYSIS}/phase4/forms/run/forms)
    if(EXISTS "${fo}_fingerprints.txt" AND EXISTS "${fo}_mame.wav")
        file(READ "${fo}_date.txt" date)
        file(READ "${fo}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        add_test(NAME mslug3_forms_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                         --inputs ${fo}_inputs.txt --reference ${fo}_fingerprints.txt
                         --frames ${frames} --date ${date}
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_forms_fingerprints_port.txt)
        add_test(NAME mslug3_forms_audio_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_audio.py
                         --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                         --inputs ${fo}_inputs.txt --reference ${fo}_mame.wav --frames ${frames} --date ${date}
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_forms_port.wav)
        set_tests_properties(mslug3_forms_vs_mame mslug3_forms_audio_vs_mame PROPERTIES LABELS mslug3
                             ENVIRONMENT "PD_CHEAT=3240,form,0,2\;3500,form,0,3\;3800,form,0,0\;4000,form,0,7\;4400,form,0,0\;4800,form,0,6")
    endif()
    # The vehicle cheat (phase4/vehicles/run): the Metal Slug and the elephant. The MAME run writes
    # the RAM bytes the port's spawn changed (PD_SPAWN_POKES) at the same frames; both spawns are on
    # frames where the game still waits through the next VBlank, since MAME writes after it.
    set(ve ${PD_MS3_ANALYSIS}/phase4/vehicles/run/vehicles)
    if(EXISTS "${ve}_fingerprints.txt" AND EXISTS "${ve}_mame.wav")
        file(READ "${ve}_date.txt" date)
        file(READ "${ve}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        set(known)
        if(EXISTS "${ve}_known_vblank.txt")
            set(known --known-differences ${ve}_known_vblank.txt)
        endif()
        add_test(NAME mslug3_vehicles_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                         --inputs ${ve}_inputs.txt --reference ${ve}_fingerprints.txt
                         --frames ${frames} --date ${date} ${known}
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_vehicles_fingerprints_port.txt)
        add_test(NAME mslug3_vehicles_audio_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_audio.py
                         --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                         --inputs ${ve}_inputs.txt --reference ${ve}_mame.wav --frames ${frames} --date ${date}
                         --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_vehicles_port.wav)
        set_tests_properties(mslug3_vehicles_vs_mame mslug3_vehicles_audio_vs_mame PROPERTIES LABELS mslug3
                             ENVIRONMENT "PD_CHEAT=3300,vehicle,0,0\;5700,vehicle,0,9")
    endif()
    # Phase 3: games recorded by hand in MAME (MS3_Port/grabar_partida.bat, empty NVRAM), replayed
    # by mame_run.py --replay --name NAME into PD_MS3_ANALYSIS/phase3/play: mslug3_play_<NAME>_vs_mame.
    file(GLOB played ${PD_MS3_ANALYSIS}/phase3/play/*_replay_fingerprints.txt)
    foreach(fp ${played})
        string(REGEX REPLACE "_replay_fingerprints\\.txt$" "" base "${fp}")
        get_filename_component(run "${base}" NAME)
        if(EXISTS "${base}_date.txt" AND EXISTS "${base}_frames.txt")
            file(READ "${base}_date.txt" date)
            file(READ "${base}_frames.txt" frames)
            string(STRIP "${date}" date)
            string(STRIP "${frames}" frames)
            # <NAME>_known_vblank.txt: VBlanks where MAME takes the VBlank interrupt one instruction
            # apart (docs/PHASE3_RECOMP.md); the test then requires exactly those differences.
            set(known)
            if(EXISTS "${base}_known_vblank.txt")
                set(known --known-differences ${base}_known_vblank.txt)
            endif()
            add_test(NAME mslug3_play_${run}_vs_mame
                     COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                             --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                             --inputs ${base}_replay_inputs.txt --reference ${fp}
                             --frames ${frames} --date ${date} ${known}
                             --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_play_${run}_fingerprints_port.txt)
            set_tests_properties(mslug3_play_${run}_vs_mame PROPERTIES LABELS mslug3)
            if(EXISTS "${base}_replay_mame.wav")           # mame_run.py --replay --wav
                add_test(NAME mslug3_play_${run}_audio_vs_mame
                         COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_audio.py
                                 --exe $<TARGET_FILE:mslug3> --roms ${PD_MS3_ROMS}
                                 --inputs ${base}_replay_inputs.txt --reference ${base}_replay_mame.wav
                                 --frames ${frames} --date ${date}
                                 --output ${CMAKE_CURRENT_BINARY_DIR}/mslug3_play_${run}_port.wav)
                set_tests_properties(mslug3_play_${run}_audio_vs_mame PROPERTIES LABELS mslug3)
            endif()
        endif()
    endforeach()
    # Phase 4: a save state, loaded into a reset machine, reproduces RAM, image and audio: in the
    # attract mode, and in the middle of the user's recording (every sound channel in use).
    add_test(NAME mslug3_savestate_roundtrip
             COMMAND mslug3 --roms ${PD_MS3_ROMS} --headless --frames 3000 --test-state 600)
    set_tests_properties(mslug3_savestate_roundtrip PROPERTIES LABELS mslug3 PASS_REGULAR_EXPRESSION "IDENTICAL")
    set(play1 ${PD_MS3_ANALYSIS}/phase3/play/partida1)
    if(EXISTS "${play1}_replay_inputs.txt" AND EXISTS "${play1}_date.txt")
        file(READ "${play1}_date.txt" date)
        string(STRIP "${date}" date)
        add_test(NAME mslug3_savestate_game
                 COMMAND mslug3 --roms ${PD_MS3_ROMS} --headless --frames 6500 --test-state 5700
                         --inputs ${play1}_replay_inputs.txt --date ${date})
        set_tests_properties(mslug3_savestate_game PROPERTIES LABELS mslug3 PASS_REGULAR_EXPRESSION "IDENTICAL")
        # Mission mode (progress.c): the recording completes Missions 1 and 2, which unlocks
        # Missions 2 and 3 when the game's next-mission step runs.
        set(progress ${CMAKE_CURRENT_BINARY_DIR}/mslug3_progress.ini)
        add_test(NAME mslug3_progress_reset COMMAND ${CMAKE_COMMAND} -E rm -f ${progress})
        set_tests_properties(mslug3_progress_reset PROPERTIES FIXTURES_SETUP mslug3_progress LABELS mslug3)
        add_test(NAME mslug3_progress_unlocks
                 COMMAND mslug3 --roms ${PD_MS3_ROMS} --headless --no-audio --frames 59799
                         --inputs ${play1}_replay_inputs.txt --date ${date})
        set_tests_properties(mslug3_progress_unlocks PROPERTIES LABELS mslug3 FIXTURES_REQUIRED mslug3_progress
                             ENVIRONMENT "PD_PROGRESS=${progress}"
                             PASS_REGULAR_EXPRESSION "mission 2 unlocked at frame 15407\nmission 3 unlocked at frame 58916")
        set(boss ${PD_MS3_ANALYSIS}/boss_rush/stage1)
        # Boss health bar (bossbar.c): the Mission 1 boss section, its life set to 10 at frame 3700;
        # the bar follows the boss (not its part, which copies the life) to its death.
        if(EXISTS "${boss}/bot_spray.txt")
            add_test(NAME mslug3_bossbar_m1
                     COMMAND mslug3 --roms ${PD_MS3_ROMS} --headless --no-audio --frames 5000 --script ${boss}/bot_spray.txt)
            set_tests_properties(mslug3_bossbar_m1 PROPERTIES LABELS mslug3
                ENVIRONMENT "PD_BOSSBAR=1;PD_DEVMODE=1;PD_DIPS=8008;PD_POKE=2400,10ED7A,0\;2400,10ED7B,4\;3700,10080A,0\;3700,10080B,A"
                PASS_REGULAR_EXPRESSION "bossbar 3700: HUGE HERMIT 1007BC 10/5000
bossbar 3703: HUGE HERMIT 1007BC 0/5000 dead"
                FAIL_REGULAR_EXPRESSION "enemy")
        endif()
        # Progress (progress.c): the last mission's step completes the game (Boss Rush's unlock, in
        # Arcade or Mission mode), from the section before the final boss.
        if(EXISTS "${boss}/bot_spray_d.txt")
            add_test(NAME mslug3_progress_game_completed
                     COMMAND mslug3 --roms ${PD_MS3_ROMS} --headless --no-audio --frames 8000 --script ${boss}/bot_spray_d.txt)
            set_tests_properties(mslug3_progress_game_completed PROPERTIES LABELS mslug3
                ENVIRONMENT "PD_PROGRESS=${CMAKE_CURRENT_BINARY_DIR}/mslug3_progress_completed.ini;PD_DEVMODE=1;PD_DIPS=8009;PD_POKE=2400,10ED7A,4\;2400,10ED7B,D"
                PASS_REGULAR_EXPRESSION "game completed at frame 5295")
        endif()
        # Boss Rush (bossrush.c): every boss in a row in one game, with the "D kills every enemy" dip
        # and a bot that walks right: each boss section leads to the next, the last to the ending.
        if(EXISTS "${boss}/bot_long_d.txt")
            add_test(NAME mslug3_bossrush_full
                     COMMAND mslug3 --roms ${PD_MS3_ROMS} --headless --no-audio --frames 16500 --script ${boss}/bot_long_d.txt)
            set_tests_properties(mslug3_bossrush_full PROPERTIES LABELS mslug3
                ENVIRONMENT "PD_BOSSRUSH=1;PD_DEVMODE=1;PD_DIPS=8009"
                PASS_REGULAR_EXPRESSION "bossrush 4689: next boss, 1/7 bosses defeated
bossrush 10599: next boss, 3/7 bosses defeated
bossrush 11757: next boss, 4/7 bosses defeated
bossrush 13287: next boss, 5/7 bosses defeated
bossrush 14485: next boss, 6/7 bosses defeated
bossrush 15917: cleared, 7/7 bosses defeated")
        endif()
        # Records: the last mission's step (4 -> 5 when the final boss falls), from the section before
        # the final boss with the "D kills every enemy" dip (not clean: debug options).
        if(EXISTS "${boss}/bot_spray_d.txt")
            add_test(NAME mslug3_records_final_mission
                     COMMAND mslug3 --roms ${PD_MS3_ROMS} --headless --no-audio --frames 8000 --script ${boss}/bot_spray_d.txt)
            set_tests_properties(mslug3_records_final_mission PROPERTIES LABELS mslug3
                ENVIRONMENT "PD_RECORDS=${CMAKE_CURRENT_BINARY_DIR}/mslug3_records_final.ini;PD_RECORDS_MIN_FRAMES=600;PD_DEVMODE=1;PD_DIPS=8009;PD_POKE=2400,10ED7A,4\\;2400,10ED7B,D"
                PASS_REGULAR_EXPRESSION "mission 5 completed at frame 5295: 2654 frames")
        endif()
        # Records (records.c): the same recording, Mission 1 from the start of the game and
        # Mission 2 from the next-mission step, both clean.
        set(records ${CMAKE_CURRENT_BINARY_DIR}/mslug3_records.ini)
        add_test(NAME mslug3_records_reset COMMAND ${CMAKE_COMMAND} -E rm -f ${records})
        set_tests_properties(mslug3_records_reset PROPERTIES FIXTURES_SETUP mslug3_records LABELS mslug3)
        add_test(NAME mslug3_records_missions
                 COMMAND mslug3 --roms ${PD_MS3_ROMS} --headless --no-audio --frames 59799
                         --inputs ${play1}_replay_inputs.txt --date ${date})
        set_tests_properties(mslug3_records_missions PROPERTIES LABELS mslug3 FIXTURES_REQUIRED mslug3_records
                             ENVIRONMENT "PD_RECORDS=${records}"
                             PASS_REGULAR_EXPRESSION "mission 1 completed at frame 15407: 14481 frames \\(4:04.67\\), score 128430, new best time, new best score\nmission 2 completed at frame 58916: 43509 frames \\(12:15.12\\), score 307034, new best time, new best score")
    endif()
    # Phase 4: the sound menu (Metal Slug 3's list, src/games/mslug3/sounds.c) is audible in the
    # middle of the user's game and, after closing it, the game stays identical.
    if(EXISTS "${play1}_replay_inputs.txt")
        add_test(NAME mslug3_sound_menu
                 COMMAND mslug3 --roms ${PD_MS3_ROMS} --headless --frames 3000 --test-sounds
                         --inputs ${play1}_replay_inputs.txt --date ${date})
        set_tests_properties(mslug3_sound_menu PROPERTIES LABELS mslug3)
    endif()
    # The YM2610 probes behind the ADPCM-B changes to ymfm (docs/ADPCM_B.md): the port's chip
    # against MAME driving the chip directly (tools/mame/ym_probe.lua).
    set(probes ${PD_MS3_ANALYSIS}/phase2/adpcmb)
    if(EXISTS "${probes}/probe4_mame.wav")
        add_test(NAME mslug3_adpcmb_probes_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/ymprobe/verify_probes.py
                         --exe $<TARGET_FILE:ym_probe> --rom ${PD_MS3_ROMS}/mslug3_adpcm --dir ${probes}
                         --out ${CMAKE_CURRENT_BINARY_DIR}/ym_probes probe1 probe2 probe3 probe4)
        set_tests_properties(mslug3_adpcmb_probes_vs_mame PROPERTIES LABELS mslug3
                             PASS_REGULAR_EXPRESSION "ALL PROBES IDENTICAL")
    endif()
else()
    message(STATUS "Metal Slug 3 tests disabled: the Metal Slug 3 copy (PD_MS3_ROMS) is missing")
endif()

# ------------------------------------------------------------------ Metal Slug X regression tests
# The same tests as in the Metal Slug X port, against its copy and its MAME references (read
# only, in PD_MSX_ROMS and PD_MSX_ANALYSIS). They guard the shared runtime.
if(NOT PD_HAVE_MSLUGX)
    return()
endif()

# Phase 9: a MAME set (the one tools/extract_roms.py rebuilds in PD_ROMS/mame), as loose files and
# as zips (made at test time in the build folder), loads exactly the same data as the PC release
# files; and only the game's zip finds neogeo.zip next to it.
if(EXISTS "${PD_MSX_ROMS}/mame/mslugx" AND EXISTS "${PD_MSX_ROMS}/mame/neogeo")
    add_test(NAME roms_mame_folder COMMAND mslugx --roms ${PD_MSX_ROMS} --headless --frames 1 --no-audio)
    set_tests_properties(roms_mame_folder PROPERTIES ENVIRONMENT "PD_COMPARE_ROMS=${PD_MSX_ROMS}/mame")
    set(ZIPS ${CMAKE_CURRENT_BINARY_DIR}/mame_zips)
    add_test(NAME roms_mame_zips_make
             COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tests/loader/zip_set.py ${PD_MSX_ROMS}/mame ${ZIPS} mslugx)
    set_tests_properties(roms_mame_zips_make PROPERTIES FIXTURES_SETUP mame_zips)
    add_test(NAME roms_mame_zip COMMAND mslugx --roms ${PD_MSX_ROMS} --headless --frames 1 --no-audio)
    set_tests_properties(roms_mame_zip PROPERTIES ENVIRONMENT "PD_COMPARE_ROMS=${ZIPS}/mslugx.zip"
                         FIXTURES_REQUIRED mame_zips)
endif()

# Phase 5 (Metal Slug 3): the game search finds a simulated Steam install in two libraries.
foreach(id mslug3 mslugx)
    if(TARGET ${id})
        add_test(NAME ${id}_find_steam
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tests/loader/steam_layout.py
                         $<TARGET_FILE:${id}> ${id} ${CMAKE_CURRENT_BINARY_DIR}/${id}_steam)
        set_tests_properties(${id}_find_steam PROPERTIES LABELS ${id})
    endif()
endforeach()

# Phase 5: a save state, loaded into a reset machine, reproduces RAM, image and audio.
add_test(NAME savestate_roundtrip
         COMMAND mslugx --roms ${PD_MSX_ROMS} --headless --frames 3000 --test-state 600)
# Phase 6B: the sound menu is audible and, after closing it, the game stays identical.
add_test(NAME sound_menu
         COMMAND mslugx --roms ${PD_MSX_ROMS} --headless --frames 3000 --test-sounds)

# Phase 4: the user's recorded game (Mission 1) replayed against MAME, with no divergences in RAM,
# palette or image. The MAME references and the inputs live in PD_MSX_ANALYSIS/phase4 (outside the
# repo; how to generate them: tools/verify_mission.py).
if(EXISTS "${PD_MSX_ANALYSIS}/phase4/mission1_fingerprints_mame.txt" AND EXISTS "${PD_MSX_ANALYSIS}/phase4/mission1_inputs.txt")
    add_test(NAME mission1_vs_mame
             COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                     --exe $<TARGET_FILE:mslugx> --roms ${PD_MSX_ROMS}
                     --inputs ${PD_MSX_ANALYSIS}/phase4/mission1_inputs.txt
                     --reference ${PD_MSX_ANALYSIS}/phase4/mission1_fingerprints_mame.txt
                     --frames 11970 --date 2026-10-11-20-28-31-0
                     --output ${CMAKE_CURRENT_BINARY_DIR}/mission1_fingerprints_port.txt)
    # Phase 9: the same with the interpreter only (the distributable build's CPU path).
    add_test(NAME mission1_interpreter_vs_mame
             COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                     --exe $<TARGET_FILE:mslugx> --roms ${PD_MSX_ROMS}
                     --inputs ${PD_MSX_ANALYSIS}/phase4/mission1_inputs.txt
                     --reference ${PD_MSX_ANALYSIS}/phase4/mission1_fingerprints_mame.txt
                     --frames 11970 --date 2026-10-11-20-28-31-0
                     --output ${CMAKE_CURRENT_BINARY_DIR}/mission1_interpreter_fingerprints_port.txt)
    set_tests_properties(mission1_interpreter_vs_mame PROPERTIES ENVIRONMENT "PD_INTERPRETER=1")
endif()

# Phase 8: scripted runs of the developers' stage select against MAME (tools/stage_run.py writes
# the references to PD_MSX_ANALYSIS/phase8, outside the repo). Stages 2 and 6 still have known
# differences (Metal Slug X port, docs/PHASE8_STAGES.md) and are not tests.
foreach(stage 03 04 05 07 08 09 10 11 12 13)
    set(base ${PD_MSX_ANALYSIS}/phase8/stage${stage})
    if(EXISTS "${base}_fingerprints.txt" AND EXISTS "${base}_date.txt" AND EXISTS "${base}_frames.txt")
        file(READ "${base}_date.txt" date)
        file(READ "${base}_frames.txt" frames)
        string(STRIP "${date}" date)
        string(STRIP "${frames}" frames)
        add_test(NAME stage${stage}_vs_mame
                 COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/verify_mission.py
                         --exe $<TARGET_FILE:mslugx> --roms ${PD_MSX_ROMS} --devmode --dips 8000
                         --inputs ${base}_inputs.txt --reference ${base}_fingerprints.txt
                         --frames ${frames} --date ${date}
                         --output ${CMAKE_CURRENT_BINARY_DIR}/stage${stage}_fingerprints_port.txt)
    endif()
endforeach()

get_property(msx_tests DIRECTORY PROPERTY TESTS)
list(FILTER msx_tests EXCLUDE REGEX "^mslug[345]?_")      # mslug_: Metal Slug's own tests
set_tests_properties(${msx_tests} PROPERTIES LABELS mslugx)
