# Musashi en Projecto Decompile

- **Origen:** https://github.com/kstenerud/Musashi, commit `313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd`
  (2026-03-08), versión 4.60.
- **Paquete:** codeload.github.com, `Musashi-313ebf1.tar.gz`, SHA256
  `8bed9e28e7e5377256d41aff80a99c9e917a24e7392d9219e1c45c40ad4000e5`.
- **Licencia:** MIT (Copyright Karl Stenerud). El texto está en `readme.txt` y en el
  encabezado de cada archivo.

## Uso

1. Es el intérprete de referencia para validar la semántica de cada instrucción en las pruebas
   diferenciales del recompilador (Fase 2).
2. Es el intérprete de respaldo del runtime: código en RAM, saltos no resueltos y código no
   visto en el análisis.

## Archivos originales sin modificar

`m68k.h`, `m68k_in.c`, `m68kconf.h`, `m68kcpu.c`, `m68kcpu.h`, `m68kdasm.c`, `m68kmake.c`,
`m68kmmu.h`, `readme.txt`, `history.txt`.

La configuración se hace con definiciones de compilación (`CMakeLists.txt`), no editando
`m68kconf.h`: 68000 puro, sin 68010/020/030/040, sin PMMU.

## Archivos sustituidos (propios del proyecto)

El 68000 no tiene FPU. Para no incluir **SoftFloat 2b**, cuya licencia lleva una cláusula de
indemnización (decisión del usuario, opción B), se sustituyen:

| Archivo | Original | Sustituto |
|---|---|---|
| `m68kfpu.c` | Emulación de la FPU 68881/68040 (usa SoftFloat) | Las instrucciones de FPU generan la excepción Line-F, como en un 68000 real. Los accesos de la PMMU abortan; con la PMMU desactivada no se ejecutan. |
| `softfloat/milieu.h` | Tipos de SoftFloat | Solo los nombres de tipos enteros que espera Musashi |
| `softfloat/softfloat.h` | API de SoftFloat | Solo el tipo `floatx80` de los registros FPU en la estructura de la CPU |

No se incluye ningún archivo de `softfloat/` del paquete original.

## Verificación

Arrancando el BIOS del usuario desde el reset, la secuencia de PCs de Musashi coincide con la
traza del depurador de MAME 0.289 (`C00402 → C11002 → C11006 → … → C185D6`).
