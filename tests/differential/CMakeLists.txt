# Differential tests of the recompiler against Musashi.
# They require the user's ROMs (PD_ROMS) and the Phase 1 analysis listing (PD_ANALYSIS).
# The test C code is generated here, in the build directory, and is never committed.

if(NOT EXISTS "${PD_ROMS}/mslugx_game_m68k" OR NOT EXISTS "${PD_ANALYSIS}/listing_p1.txt")
    message(STATUS "Differential tests disabled: missing ROMs (${PD_ROMS}) or listing (${PD_ANALYSIS})")
    return()
endif()

# Musashi base cycle table (the volcar_ciclos executable is defined in src/).
set(CICLOS ${CMAKE_CURRENT_BINARY_DIR}/ciclos_68000.bin)
add_custom_command(OUTPUT ${CICLOS} COMMAND volcar_ciclos ${CICLOS} DEPENDS volcar_ciclos VERBATIM)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/pruebas_gen.c)
file(GLOB RECOMP_PY ${PROJECT_SOURCE_DIR}/tools/recomp68k/*.py)
add_custom_command(
    OUTPUT ${GEN}
    COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/generate_tests.py
            --roms ${PD_ROMS} --listado ${PD_ANALYSIS}/listing_p1.txt --output ${GEN} --cycles ${CICLOS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generate_tests.py ${RECOMP_PY} ${CICLOS}
    COMMENT "Generating differential tests from the user's ROMs"
    VERBATIM)

add_executable(diferencial differential.c test_memory.c musashi_side.c ${GEN})
target_include_directories(diferencial PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/cpu)
target_link_libraries(diferencial PRIVATE musashi)
# The generated code declares temporaries that are sometimes unused.
set_source_files_properties(${GEN} PROPERTIES COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-but-set-variable")

add_test(NAME diferencial_cpu
         COMMAND diferencial ${PD_ROMS}/mslugx_game_m68k ${PD_ROMS}/mslugx_bios_m68k 64)
