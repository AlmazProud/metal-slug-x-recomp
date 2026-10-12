# Lockstep del programa recompilado (P1 + BIOS del usuario) contra Musashi desde el reset.
# Requiere los ROMs (PD_ROMS), los listados del análisis y la cobertura (PD_ANALISIS).
# El programa recompilado se genera en el directorio de compilación y nunca se versiona.

if(NOT EXISTS "${PD_ROMS}/mslugx_game_m68k" OR NOT EXISTS "${PD_ANALISIS}/listado_bios.txt")
    message(STATUS "Lockstep desactivado: faltan ROMs o listados del análisis")
    return()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PROG_DIR ${CMAKE_CURRENT_BINARY_DIR}/programa)
set(CICLOS_LS ${CMAKE_CURRENT_BINARY_DIR}/ciclos_68000.bin)
add_custom_command(OUTPUT ${CICLOS_LS} COMMAND volcar_ciclos ${CICLOS_LS} DEPENDS volcar_ciclos VERBATIM)

set(PROG_C ${PROG_DIR}/programa_registro.c)
foreach(n RANGE 0 15)
    string(LENGTH "${n}" largo)
    if(largo EQUAL 1)
        set(num "00${n}")
    else()
        set(num "0${n}")
    endif()
    list(APPEND PROG_C ${PROG_DIR}/programa_${num}.c)
endforeach()

file(GLOB RECOMP_PY_LS ${PROJECT_SOURCE_DIR}/herramientas/recomp68k/*.py)
add_custom_command(
    OUTPUT ${PROG_C}
    COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/herramientas/recomp68k/programa.py
            --roms ${PD_ROMS}
            --listados ${PD_ANALISIS}/listado_p1.txt ${PD_ANALISIS}/listado_bios.txt
            --cobertura ${PD_ANALISIS}/cobertura.json ${PD_ANALISIS}/cobertura_mision1.json
            --ciclos ${CICLOS_LS} --salida ${PROG_DIR} --archivos 16
    DEPENDS ${RECOMP_PY_LS} ${CICLOS_LS}
    COMMENT "Recompilando P1 y BIOS del usuario a C"
    VERBATIM)

add_library(programa_recompilado STATIC ${PROG_C})
target_include_directories(programa_recompilado PRIVATE ${PROJECT_SOURCE_DIR}/src/cpu)
target_compile_options(programa_recompilado PRIVATE -O1 -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-label)

add_executable(lockstep lockstep.c maquina.c bus.c musashi_ref.c
               ${PROJECT_SOURCE_DIR}/src/cpu/despachador.c
               ${PROJECT_SOURCE_DIR}/src/cpu/interprete_musashi.c)
target_include_directories(lockstep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/cpu)
target_link_libraries(lockstep PRIVATE programa_recompilado musashi)

add_test(NAME lockstep_reset
         COMMAND lockstep ${PD_ROMS}/mslugx_game_m68k ${PD_ROMS}/mslugx_bios_m68k 20000000)

# Desde un estado real del juego (volcado de MAME en el frame 4000 de la grabaci?n del usuario).
if(EXISTS "${PD_ANALISIS}/estado_f4000.bin")
    add_test(NAME lockstep_juego
             COMMAND lockstep ${PD_ROMS}/mslugx_game_m68k ${PD_ROMS}/mslugx_bios_m68k 120000000
                     ${PD_ANALISIS}/estado_f4000.bin)
endif()
