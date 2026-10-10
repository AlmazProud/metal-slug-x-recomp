# Terceros

## Bibliotecas integradas al código del port

Ninguna todavía. Cada biblioteca que se incorpore (por ejemplo Musashi o ymfm) se anotará
aquí con su versión, origen, licencia y el uso que se le da.

| Biblioteca | Versión | Origen | Licencia | Uso |
|---|---|---|---|---|
| (ninguna) | | | | |

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
