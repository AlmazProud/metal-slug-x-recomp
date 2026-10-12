# Lockstep del programa recompilado (P1 + BIOS del usuario) contra Musashi desde el reset.
# Usa el programa recompilado de src/ (generado en build/ desde la copia del usuario).

if(NOT TARGET recompiled_program)
    message(STATUS "Lockstep desactivado: falta el programa recompilado")
    return()
endif()

add_executable(lockstep lockstep.c machine.c bus.c musashi_ref.c)
target_include_directories(lockstep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/cpu)
target_link_libraries(lockstep PRIVATE recompiled_program)

add_test(NAME lockstep_reset
         COMMAND lockstep ${PD_ROMS}/mslugx_game_m68k ${PD_ROMS}/mslugx_bios_m68k 20000000)

# Desde un estado real del juego (volcado de MAME en el frame 4000 de la grabación del usuario).
if(EXISTS "${PD_ANALYSIS}/state_f4000.bin")
    add_test(NAME lockstep_juego
             COMMAND lockstep ${PD_ROMS}/mslugx_game_m68k ${PD_ROMS}/mslugx_bios_m68k 120000000
                     ${PD_ANALYSIS}/state_f4000.bin)
endif()
