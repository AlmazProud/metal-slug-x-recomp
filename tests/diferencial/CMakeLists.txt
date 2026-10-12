# Pruebas diferenciales del recompilador contra Musashi.
# Requieren los ROMs del usuario (PD_ROMS) y el listado del análisis de la Fase 1 (PD_ANALISIS).
# El C de las pruebas se genera aquí, en el directorio de compilación, y nunca se versiona.

if(NOT EXISTS "${PD_ROMS}/mslugx_game_m68k" OR NOT EXISTS "${PD_ANALISIS}/listado_p1.txt")
    message(STATUS "Pruebas diferenciales desactivadas: faltan ROMs (${PD_ROMS}) o listado (${PD_ANALISIS})")
    return()
endif()

# Tabla de ciclos base de Musashi (el ejecutable volcar_ciclos se define en src/).
set(CICLOS ${CMAKE_CURRENT_BINARY_DIR}/ciclos_68000.bin)
add_custom_command(OUTPUT ${CICLOS} COMMAND volcar_ciclos ${CICLOS} DEPENDS volcar_ciclos VERBATIM)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/pruebas_gen.c)
file(GLOB RECOMP_PY ${PROJECT_SOURCE_DIR}/herramientas/recomp68k/*.py)
add_custom_command(
    OUTPUT ${GEN}
    COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/generar_pruebas.py
            --roms ${PD_ROMS} --listado ${PD_ANALISIS}/listado_p1.txt --salida ${GEN} --ciclos ${CICLOS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generar_pruebas.py ${RECOMP_PY} ${CICLOS}
    COMMENT "Generando pruebas diferenciales desde los ROMs del usuario"
    VERBATIM)

add_executable(diferencial diferencial.c memoria_prueba.c musashi_lado.c ${GEN})
target_include_directories(diferencial PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/cpu)
target_link_libraries(diferencial PRIVATE musashi)
# El código generado declara temporales que a veces no se usan.
set_source_files_properties(${GEN} PROPERTIES COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-but-set-variable")

add_test(NAME diferencial_cpu
         COMMAND diferencial ${PD_ROMS}/mslugx_game_m68k ${PD_ROMS}/mslugx_bios_m68k 64)
