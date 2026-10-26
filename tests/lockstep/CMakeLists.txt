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

# Metal Slug X: MAME dump at frame 4000 of the user's recording (analysis/state_f4000.bin).
pd_lockstep(mslugx lockstep "${PD_ROMS}" "${PD_ANALYSIS}/state_f4000.bin" lockstep)
