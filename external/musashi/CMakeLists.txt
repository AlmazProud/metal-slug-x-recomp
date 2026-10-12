# Musashi 4.60 (MIT), commit 313ebf1 de github.com/kstenerud/Musashi.
# Configurado como 68000 puro: sin 010/020/030/040, sin PMMU y sin FPU (ver LEEME_PD.md).
# m68kops.c/h se generan en tiempo de compilación con m68kmake a partir de m68k_in.c.

add_executable(m68kmake m68kmake.c)
set_target_properties(m68kmake PROPERTIES C_STANDARD 99)
target_compile_options(m68kmake PRIVATE -w)

set(MUSASHI_GEN ${CMAKE_CURRENT_BINARY_DIR}/generado)
add_custom_command(
    OUTPUT ${MUSASHI_GEN}/m68kops.c ${MUSASHI_GEN}/m68kops.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MUSASHI_GEN}
    COMMAND m68kmake ${MUSASHI_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/m68k_in.c
    DEPENDS m68kmake ${CMAKE_CURRENT_SOURCE_DIR}/m68k_in.c
    COMMENT "Musashi: generando m68kops.c/h"
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
# Código de terceros: sin los avisos del proyecto.
target_compile_options(musashi PRIVATE -w)
