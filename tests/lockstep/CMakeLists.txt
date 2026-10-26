# Lockstep of the recompiled program (P1 + BIOS) against Musashi, with the minimal machine model of
# machine.c and the game's cartridge (its descriptor, src/games/<id>/game.c): one executable per
# game with a recompiled program (Metal Slug X's keeps the names lockstep, lockstep_reset/_game).

# pd_lockstep(id exe roms state prefix): test <prefix>_reset (from reset) and, if the MAME dump
# exists, <prefix>_game (from that game state).
function(pd_lockstep id exe roms state prefix)
    if(NOT PD_HAVE_PROGRAM_${id})
        message(STATUS "Lockstep ${id} disabled: the recompiled program is missing")
        return()
    endif()
    set(crypt)
    if(EXISTS ${PROJECT_SOURCE_DIR}/src/games/${id}/crypt.c)       # the MAME set's decryption
        set(crypt ${PROJECT_SOURCE_DIR}/src/games/${id}/crypt.c)
    endif()
    add_executable(${exe} lockstep.c machine.c bus.c musashi_ref.c ${PROJECT_SOURCE_DIR}/src/games/${id}/game.c ${crypt})
    target_include_directories(${exe} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/cpu
                               ${PROJECT_SOURCE_DIR}/src/neogeo)
    target_link_libraries(${exe} PRIVATE recompiled_${id})
    add_test(NAME ${prefix}_reset COMMAND ${exe} ${roms}/${id}_game_m68k ${roms}/${id}_bios_m68k 20000000)
    set_tests_properties(${prefix}_reset PROPERTIES LABELS ${id})
    if(EXISTS "${state}")
        add_test(NAME ${prefix}_game
                 COMMAND ${exe} ${roms}/${id}_game_m68k ${roms}/${id}_bios_m68k 120000000 ${state})
        set_tests_properties(${prefix}_game PROPERTIES LABELS ${id})
    endif()
endfunction()

# Metal Slug: MAME dump in Mission 1 of the scripted game (analysis/phase3, dump_state.lua); the
# 68000 files are the GOG copy's own (PD_ROMS).
pd_lockstep(mslug lockstep_mslug "${PD_ROMS}" "${PD_ANALYSIS}/phase3/state_start_f4000.bin" lockstep_mslug)
# Metal Slug 4: MAME dump in Mission 1 of the scripted game (analysis/phase3, dump_state.lua); the
# 68000 files from tools/make_reference.py --mslug4.
if(PD_HAVE_MSLUG4)
    pd_lockstep(mslug4 lockstep_mslug4 "${PD_MS4_ANALYSIS}/phase3/program" "${PD_MS4_ANALYSIS}/phase3/state_start_f8000.bin" lockstep_mslug4)
endif()
# Metal Slug 5: MAME dump in Mission 1 of the scripted game (analysis/phase3, dump_state.lua with
# PD_PVC=1: version 3, the NEO-PVC's window offset and RAM).
if(PD_HAVE_MSLUG5)
    pd_lockstep(mslug5 lockstep_mslug5 "${PD_MS5_ROMS}" "${PD_MS5_ANALYSIS}/phase3/state_start_f8000.bin" lockstep_mslug5)
endif()
# Metal Slug X: MAME dump at frame 4000 of the user's recording.
if(PD_HAVE_MSLUGX)
    pd_lockstep(mslugx lockstep "${PD_MSX_ROMS}" "${PD_MSX_ANALYSIS}/state_f4000.bin" lockstep)
endif()
# Metal Slug 3: MAME dump in Mission 1 of the scripted game (analysis/phase3, dump_state.lua).
if(PD_HAVE_MSLUG3)
    pd_lockstep(mslug3 lockstep_mslug3 "${PD_MS3_ROMS}" "${PD_MS3_ANALYSIS}/phase3/state_start_f5000.bin" lockstep_mslug3)
endif()
