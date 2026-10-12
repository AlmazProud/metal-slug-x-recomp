# Núcleo del port: CPU (despachador + respaldo), programa recompilado y runtime de la Neo Geo.

add_library(cpu68k STATIC cpu/despachador.c cpu/interprete_musashi.c)
target_include_directories(cpu68k PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpu)
target_link_libraries(cpu68k PUBLIC musashi)

# Tabla de ciclos base de Musashi para el 68000, que usa el generador.
add_executable(volcar_ciclos ${PROJECT_SOURCE_DIR}/herramientas/recomp68k/volcar_ciclos.c)
target_link_libraries(volcar_ciclos PRIVATE musashi)

if(NOT EXISTS "${PD_ROMS}/mslugx_game_m68k" OR NOT EXISTS "${PD_ANALISIS}/listado_bios.txt")
    message(STATUS "Programa recompilado desactivado: faltan ROMs (${PD_ROMS}) o listados del análisis (${PD_ANALISIS})")
    return()
endif()

set(CICLOS ${CMAKE_CURRENT_BINARY_DIR}/ciclos_68000.bin)
add_custom_command(OUTPUT ${CICLOS} COMMAND volcar_ciclos ${CICLOS} DEPENDS volcar_ciclos VERBATIM)

set(PROG_DIR ${CMAKE_CURRENT_BINARY_DIR}/programa)
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

set(COBERTURA ${PD_ANALISIS}/cobertura.json)
if(EXISTS "${PD_ANALISIS}/cobertura_mision1.json")
    list(APPEND COBERTURA ${PD_ANALISIS}/cobertura_mision1.json)
endif()

file(GLOB RECOMP_PY ${PROJECT_SOURCE_DIR}/herramientas/recomp68k/*.py)
add_custom_command(
    OUTPUT ${PROG_C}
    COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/herramientas/recomp68k/programa.py
            --roms ${PD_ROMS}
            --listados ${PD_ANALISIS}/listado_p1.txt ${PD_ANALISIS}/listado_bios.txt
            --cobertura ${COBERTURA}
            --ciclos ${CICLOS} --salida ${PROG_DIR} --archivos 16
    DEPENDS ${RECOMP_PY} ${CICLOS}
    COMMENT "Recompilando P1 y BIOS del usuario a C"
    VERBATIM)

# Código generado a partir de la copia del usuario: vive solo en build/.
add_library(programa_recompilado STATIC ${PROG_C})
target_link_libraries(programa_recompilado PUBLIC cpu68k)
target_compile_options(programa_recompilado PRIVATE -O1 -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-label)

# Runtime de hardware de la Neo Geo (Fase 3).
add_library(neogeo STATIC
    neogeo/bus.c neogeo/maquina.c neogeo/roms.c neogeo/rtc.c neogeo/sha256.c
    neogeo/sonido.c neogeo/video.c)
target_include_directories(neogeo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/neogeo)
target_link_libraries(neogeo PUBLIC cpu68k)
target_compile_options(neogeo PRIVATE -Wall -Wextra)

# Ejecutable del port (SDL3).
find_package(SDL3 CONFIG)
if(NOT SDL3_FOUND)
    message(STATUS "Port desactivado: no se encontró SDL3 (CMAKE_PREFIX_PATH)")
    return()
endif()
add_executable(mslugx WIN32 port/main.c)
target_link_libraries(mslugx PRIVATE neogeo programa_recompilado SDL3::SDL3)
target_compile_options(mslugx PRIVATE -Wall -Wextra)
add_custom_command(TARGET mslugx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:mslugx> $<TARGET_FILE_DIR:mslugx>
    COMMAND_EXPAND_LISTS)
