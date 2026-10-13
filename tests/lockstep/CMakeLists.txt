# Lockstep of the recompiled program (the user's P1 + BIOS) against Musashi from reset.
# Uses the recompiled program from src/ (generated in build/ from the user's copy).

if(NOT PD_HAVE_PROGRAM)
    message(STATUS "Lockstep disabled: the recompiled program is missing")
    return()
endif()

add_executable(lockstep lockstep.c machine.c bus.c musashi_ref.c)
target_include_directories(lockstep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/cpu)
target_link_libraries(lockstep PRIVATE recompiled_program)

add_test(NAME lockstep_reset
         COMMAND lockstep ${PD_ROMS}/mslugx_game_m68k ${PD_ROMS}/mslugx_bios_m68k 20000000)

# From a real game state (MAME dump at frame 4000 of the user's recording).
if(EXISTS "${PD_ANALYSIS}/state_f4000.bin")
    add_test(NAME lockstep_game
             COMMAND lockstep ${PD_ROMS}/mslugx_game_m68k ${PD_ROMS}/mslugx_bios_m68k 120000000
                     ${PD_ANALYSIS}/state_f4000.bin)
endif()
