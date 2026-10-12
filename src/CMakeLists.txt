# Núcleo del port: CPU (despachador + respaldo), programa recompilado y runtime de la Neo Geo.

add_library(cpu68k STATIC cpu/despachador.c cpu/interprete_musashi.c)
target_include_directories(cpu68k PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpu)
target_link_libraries(cpu68k PUBLIC musashi)

# Tabla de ciclos base de Musashi para el 68000, que usa el generador.
add_executable(volcar_ciclos ${PROJECT_SOURCE_DIR}/herramientas/recomp68k/volcar_ciclos.c)
target_link_libraries(volcar_ciclos PRIVATE musashi)

# Consulta de las reglas de muestreo de interrupciones (herramientas/verificar_muestreo.py).
add_executable(clases_muestreo ${PROJECT_SOURCE_DIR}/herramientas/recomp68k/clases_muestreo.c)
target_link_libraries(clases_muestreo PRIVATE cpu68k)

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
    neogeo/bus.c neogeo/estado.c neogeo/maquina.c neogeo/roms.c neogeo/rtc.c neogeo/sha256.c
    neogeo/sonido.c neogeo/video.c neogeo/z80_impl.c neogeo/ym2610.cpp)
target_include_directories(neogeo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/neogeo
                           PRIVATE ${PROJECT_SOURCE_DIR}/externo/chips)
target_link_libraries(neogeo PUBLIC cpu68k ymfm)
set_target_properties(neogeo PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(neogeo PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra>)
# z80.h (tercero) genera avisos propios; se compila sin ellos.
set_source_files_properties(neogeo/z80_impl.c PROPERTIES COMPILE_OPTIONS "-w")

# Ejecutable del port (SDL3).
find_package(SDL3 CONFIG)
if(NOT SDL3_FOUND)
    message(STATUS "Port desactivado: no se encontró SDL3 (CMAKE_PREFIX_PATH)")
    return()
endif()
set(PORT_FUENTES port/main.c port/sin_ventana.c port/ventana.c port/config.c port/debug.c port/gog.c port/sonidos.c port/perfil_sonidos.c)
if(WIN32)
    enable_language(RC)
    list(APPEND PORT_FUENTES port/mslugx.rc)
endif()
add_executable(mslugx WIN32 ${PORT_FUENTES})
target_link_libraries(mslugx PRIVATE neogeo programa_recompilado SDL3::SDL3)
target_compile_options(mslugx PRIVATE -Wall -Wextra)
# Runtime de C++ (por ymfm) enlazado estático: el ejecutable solo necesita SDL3.dll.
target_link_options(mslugx PRIVATE -static-libstdc++ -static-libgcc)
set_target_properties(mslugx PROPERTIES LINKER_LANGUAGE CXX)
add_custom_command(TARGET mslugx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:mslugx> $<TARGET_FILE_DIR:mslugx>
    COMMAND_EXPAND_LISTS)

# Distribución (Fase 5): cmake --install build --prefix <carpeta> deja el ejecutable y SDL3.dll.
# El ejecutable contiene código traducido de la copia del usuario: es para uso personal.
install(TARGETS mslugx RUNTIME DESTINATION .)
install(FILES $<TARGET_RUNTIME_DLLS:mslugx> DESTINATION .)

# Fase 5: un estado guardado, cargado en una máquina reiniciada, reproduce RAM, imagen y audio.
add_test(NAME estado_guardado
         COMMAND mslugx --roms ${PD_ROMS} --sin-ventana --cuadros 3000 --probar-estado 600)
# Fase 6B: el menú de sonido suena y, al cerrarlo, el juego sigue idéntico.
add_test(NAME menu_sonido
         COMMAND mslugx --roms ${PD_ROMS} --sin-ventana --cuadros 3000 --probar-sonidos)

# Fase 4: la partida grabada del usuario (Misión 1) reproducida contra MAME, sin divergencias en RAM,
# paleta ni imagen. Las referencias de MAME y las entradas viven en PD_ANALISIS/fase4 (fuera del repo;
# cómo generarlas: herramientas/verificar_mision.py).
if(EXISTS "${PD_ANALISIS}/fase4/mision1_huellas_mame.txt" AND EXISTS "${PD_ANALISIS}/fase4/mision1_entradas.txt")
    add_test(NAME mision1_contra_mame
             COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/herramientas/verificar_mision.py
                     --exe $<TARGET_FILE:mslugx> --roms ${PD_ROMS}
                     --entradas ${PD_ANALISIS}/fase4/mision1_entradas.txt
                     --referencia ${PD_ANALISIS}/fase4/mision1_huellas_mame.txt
                     --cuadros 11970 --fecha 2026-10-11-20-28-31-0
                     --salida ${CMAKE_CURRENT_BINARY_DIR}/mision1_huellas_port.txt)
endif()
