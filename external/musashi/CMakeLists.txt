# Musashi 4.60 (MIT), commit 313ebf1 of github.com/kstenerud/Musashi.
# Configured as a plain 68000: no 010/020/030/040, no PMMU and no FPU (see README_PD.md).
# m68kops.c/h are generated at build time by m68kmake from m68k_in.c.

add_executable(m68kmake m68kmake.c)
set_target_properties(m68kmake PROPERTIES C_STANDARD 99)
target_compile_options(m68kmake PRIVATE -w)

set(MUSASHI_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
    OUTPUT ${MUSASHI_GEN}/m68kops.c ${MUSASHI_GEN}/m68kops.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MUSASHI_GEN}
    COMMAND m68kmake ${MUSASHI_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/m68k_in.c
    DEPENDS m68kmake ${CMAKE_CURRENT_SOURCE_DIR}/m68k_in.c
    COMMENT "Musashi: generating m68kops.c/h"
    VERBATIM)

add_library(musashi STATIC
    m68kcpu.c
    m68kdasm.c
    ${MUSASHI_GEN}/m68kops.c)
target_include_directories(musashi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${MUSASHI_GEN})
target_compile_definitions(musashi PUBLIC
    M68K_EMULATE_010=M68K_OPT_OFF
    M68K_EMULATE_EC020=M68K_OPT_OFF
    M68K_EMULATE_020=M68K_OPT_OFF
    M68K_EMULATE_030=M68K_OPT_OFF
    M68K_EMULATE_040=M68K_OPT_OFF
    M68K_EMULATE_PMMU=M68K_OPT_OFF)
# Third-party code: without the project's warnings.
target_compile_options(musashi PRIVATE -w)
