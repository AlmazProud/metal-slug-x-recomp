# Terceros

## Bibliotecas integradas al código del port

Cada biblioteca se anota con su versión, origen, licencia y el uso que se le da. Se copian sin
modificar, junto con su archivo de licencia.

| Biblioteca | Versión | Origen | Licencia | Uso |
|---|---|---|---|---|
| Musashi | 4.60 (commit `313ebf1`) | github.com/kstenerud/Musashi, copiado en `externo/musashi/` | MIT | Intérprete 68000 de referencia (pruebas diferenciales) y de respaldo en el runtime. **Sin SoftFloat**: `m68kfpu.c` y los encabezados de `softfloat/` son sustitutos propios (ver `externo/musashi/LEEME_PD.md`). Se enlaza al port. |
| z80.h (chips) | commit `9e88298` (2026-09-26); tarball SHA256 `fcae3999…a09b8d` | github.com/floooh/chips, `chips/z80.h` copiado en `externo/chips/` con su `LICENSE` | zlib | Z80 del sonido, emulado ciclo a ciclo. Se enlaza al port. |
| ymfm | commit `81aec25` (2026-07-27); tarball SHA256 `9bcf001b…f443f` | github.com/aaronsgiles/ymfm, solo los archivos del YM2610 (OPN, ADPCM y SSG) en `externo/ymfm/` con su `LICENSE` | BSD-3-Clause | YM2610 (FM, SSG, ADPCM-A/B), el mismo núcleo que usa MAME. Está escrito en C++: es el único código C++ del port, y lo usa un puente en C (`src/neogeo/ym2610.cpp`). Se enlaza al port, con el runtime de C++ estático. |
| Capstone | 5.0.7 | PyPI (`capstone`), github.com/capstone-engine/capstone | BSD-3-Clause | Decodificador de instrucciones 68000 en las herramientas de análisis (Fase 1). No se enlaza al port. |
| NumPy | 2.5.4 | PyPI (`numpy`) | BSD-3-Clause | Conversión de formatos gráficos en el extractor de ROMs. No se enlaza al port. |

## Herramientas del entorno (no se distribuyen con el repo)

El instalador (`entorno/setup.ps1`) las descarga en `tools\` desde sus fuentes oficiales.
Ninguna se enlaza ni se redistribuye con el port.

| Herramienta | Licencia | Uso |
|---|---|---|
| SDL3 | zlib | Ventana, entrada y audio del runtime. Se enlaza dinámicamente (SDL3.dll). |
| llvm-mingw | Apache-2.0 con excepción LLVM, más los componentes de mingw-w64 | Compilador C |
| CMake | BSD-3-Clause | Build |
| Ninja | Apache-2.0 | Build |
| Python (CPython) | PSF-2.0 | Herramientas de análisis |
| Eclipse Temurin JDK 21 | GPL-2.0 con Classpath Exception | Necesario para Ghidra |
| Ghidra | Apache-2.0 | Desensamblado y análisis |
| MAME | GPL-2.0+ (el proyecto en conjunto) | Oráculo y depurador. Su código fuente se usa solo como referencia de documentación del hardware y nunca se copia. |
| 7-Zip | LGPL-2.1 con restricción unRAR | Descompresión |
| ImHex | GPL-2.0 | Editor hexadecimal |
| MinGit (Git for Windows) | GPL-2.0 | Control de versiones |
