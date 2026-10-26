# Differential tests of the recompiler against Musashi, on the Metal Slug X program (regression
# target; PD_MSX_ROMS and its analysis listing in PD_MSX_ANALYSIS).
# The test C code is generated here, in the build directory, and is never committed.

if(NOT EXISTS "${PD_MSX_ROMS}/mslugx_game_m68k" OR NOT EXISTS "${PD_MSX_ANALYSIS}/listing_p1.txt")
    message(STATUS "Differential tests disabled: missing ROMs (${PD_MSX_ROMS}) or listing (${PD_MSX_ANALYSIS})")
    return()
endif()

# Musashi base cycle table (the dump_cycles executable is defined in src/).
set(CYCLES_BIN ${CMAKE_CURRENT_BINARY_DIR}/cycles_68000.bin)
add_custom_command(OUTPUT ${CYCLES_BIN} COMMAND dump_cycles ${CYCLES_BIN} DEPENDS dump_cycles VERBATIM)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/tests_gen.c)
file(GLOB RECOMP_PY ${PROJECT_SOURCE_DIR}/tools/recomp68k/*.py)
add_custom_command(
    OUTPUT ${GEN}
    COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/generate_tests.py
            --roms ${PD_MSX_ROMS} --game mslugx --listing ${PD_MSX_ANALYSIS}/listing_p1.txt --output ${GEN} --cycles ${CYCLES_BIN}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generate_tests.py ${RECOMP_PY} ${CYCLES_BIN}
    COMMENT "Generating differential tests from the user's ROMs"
    VERBATIM)

add_executable(diferencial differential.c test_memory.c musashi_side.c ${GEN})
target_include_directories(diferencial PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/cpu)
target_link_libraries(diferencial PRIVATE musashi)
# The generated code declares temporaries that are sometimes unused.
set_source_files_properties(${GEN} PROPERTIES COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-but-set-variable")

add_test(NAME cpu_differential
         COMMAND diferencial ${PD_MSX_ROMS}/mslugx_game_m68k ${PD_MSX_ROMS}/mslugx_bios_m68k 64)
set_tests_properties(cpu_differential PROPERTIES LABELS mslugx)
