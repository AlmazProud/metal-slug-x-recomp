# ROM_MAP: archivos de GOG/DotEmu y set `mslugx` de MAME

Fuente: instalación GOG de Metal Slug X, carpeta `resources\game\`. Los archivos se copian a
`roms\` con `INSTALAR.bat -CopiarRoms`.

Oráculo: MAME 0.289 (`mame -listxml mslugx`). Todo lo que aparece aquí se **verificó por
CRC32 y SHA1** con `herramientas/extraer_roms.py`, que obtiene los hashes esperados de MAME en
tiempo de ejecución.

## Resumen

| Archivo DotEmu | Bytes | Tipo | ROM de MAME | Transformación | Verificado |
|---|---|---|---|---|---|
| `mslugx_game_m68k` [0, 1 MB) | 1 048 576 | **P** (68000) | `250-p1.p1` | ninguna | **DISTINTO**: ver P1 |
| `mslugx_game_m68k` [1 MB, 5 MB) | 4 194 304 | **P** (68000) | `250-p2.ep1` | ninguna | OK |
| `mslugx_game_sfix` | 131 072 | **S** (fix del juego) | `250-s1.s1` | formato fix (ver abajo) | OK |
| `mslugx_game_z80` | 131 072 | **M** (Z80) | `250-m1.m1` | ninguna | OK |
| `mslugx_adpcm` [0, 4 MB) | 4 194 304 | **V** (ADPCM-A) | `250-v1.v1` | ninguna | OK |
| `mslugx_adpcm` [4 MB, 8 MB) | 4 194 304 | **V** | `250-v2.v2` | ninguna | OK |
| `mslugx_adpcm` [8 MB, 10 MB) | 2 097 152 | **V** | `250-v3.v3` | ninguna | OK |
| `mslugx_tiles` [0, 16 MB) | 16 777 216 | **C** (sprites) | `250-c1.c1` + `250-c2.c2` | formato sprite (ver abajo) | OK |
| `mslugx_tiles` [16 MB, 32 MB) | 16 777 216 | **C** | `250-c3.c3` + `250-c4.c4` | formato sprite | OK |
| `mslugx_tiles` [32 MB, 48 MB) | 16 777 216 | **C** | `250-c5.c5` + `250-c6.c6` | formato sprite | OK |
| `mslugx_bios_m68k` | 131 072 | BIOS 68000 | `sp-u2.sp1` (MVS EE. UU., `-bios us`) | ninguna | OK |
| `mslugx_bios_sfix` | 131 072 | Fix del sistema | `sfix.sfix` | formato fix | OK |
| `mslugx_zoom_table` | 131 072 | Tabla de zoom del LSPC (L0) | `000-lo.lo` | ninguna | OK |
| — | — | BIOS Z80 | `sm1.sm1` | **GOG no lo incluye** | FALTA |
| `mslugx_spr_usage` | 98 304 | Metadatos de DotEmu | — | 2 bits por tile de sprite (393 216 tiles) | n/a |
| `mslugx_gfix_usage` | 4 096 | Metadatos de DotEmu | — | 1 byte por tile de fix (4 096 tiles) | n/a |
| `mslugx_howtoplay`, `mslugx_stage`, `mslugx_multi_stage` | ~19 KB | Datos de menús de DotEmu | — | no forman parte del hardware | n/a |

## Formatos

### P (68000)

DotEmu guarda P1 y P2 concatenados, con el mismo orden de bytes que los archivos `.p` de MAME.
MAME los carga con `ROM_LOAD16_WORD_SWAP`, así que la vista del 68000 (big-endian) se obtiene
intercambiando cada par de bytes. Esa vista empieza con el vector SSP `0x0010F300`, el vector
PC `0x00C00402` (punto de entrada del BIOS) y `NEO-GEO` en 0x100.

### S / sfix (fix layer, tiles de 8x8)

- **DotEmu:** 4 bits por píxel en orden de filas. Cada tile ocupa 32 bytes: 8 filas de 4
  bytes, y cada byte guarda 2 píxeles (el nibble bajo es el de la izquierda).
- **Neo Geo:** cada tile de 32 bytes está organizado por columnas, en 4 grupos de 8 bytes (un
  byte por fila). El par de píxeles k = 0, 1, 2, 3 (píxeles 0-1, 2-3, 4-5, 6-7) va en el
  grupo 2, 3, 0, 1 respectivamente. El byte se copia tal cual, sin cambiar el orden de nibbles.
- **Cómo se determinó:** probando las 24 permutaciones de grupos × 2 órdenes de nibble contra
  el CRC de MAME. Coincidió una sola variante, la misma para `sfix.sfix` y para `250-s1.s1`.

### C (sprites, tiles de 16x16)

- **DotEmu:** 4 bits por píxel en orden de filas. Cada tile ocupa 128 bytes: 16 filas de 8
  bytes, con el nibble bajo como píxel izquierdo. Los tiles están en el mismo orden que en la
  región `sprites` de MAME.
- **Neo Geo:** en la región entrelazada (C impar en bytes pares, C par en bytes impares), cada
  tile ocupa 128 bytes:
  - la mitad izquierda (x = 0–7) está en 0x40–0x7F y la mitad derecha (x = 8–15) en 0x00–0x3F;
  - cada fila usa 4 bytes: +0 plano 0, +1 plano 2, +2 plano 1, +3 plano 3;
  - dentro de cada byte de plano, el píxel x de la mitad ocupa el **bit 7 − x**.

  Los C impares (c1, c3, c5) contienen los planos 0-1 y los pares (c2, c4, c6), los planos 2-3.
- **Cómo se determinó:** con la organización por tile documentada en MAME
  (`neogeo_spr.cpp`, usada solo como referencia) y un barrido de 192 variantes (orden de
  nibbles, orden de bits, mitades y asignación de planos) contra el CRC de `250-c1.c1`. Hubo
  una sola coincidencia, y después los 6 C ROMs coincidieron en CRC32 y SHA1.
- La conversión es una permutación de bits, por lo tanto **reversible** y sin pérdida.

## P1: contenido distinto del de MAME

- El P1 de GOG da CRC `60c33b1a` y SHA1 `8999bfc42b233cdb58778857c87d9f3187c61025`. MAME espera
  CRC `81f1f60b` y SHA1 `4c19f2e9824e606178ac1c9d4b0516fbaa625035`.
- No se explica por el orden de bytes: P2 coincide con el mismo tratamiento, y se probaron P1
  directo y con bytes intercambiados.
- La protección **no** fue retirada: el código conserva unas 460 rutinas en línea que leen
  `0x2FFFE0` y la tabla en `0x0DEDD2`.
- Hipótesis pendientes de verificar: un parche de DotEmu en algún punto de P1, o una revisión
  distinta del P1. Sin el P1 original no se puede localizar la diferencia por comparación
  directa. Se investigará en la Fase 1 (desensamblado) y con MAME en cuanto pueda arrancar.
- Consecuencia: el port y el oráculo deben usar **el mismo P1, el de GOG**. MAME lo acepta con
  una advertencia de checksum; no es un error fatal.

## Protección de mslugx

Referencia: MAME 0.289, archivos `src/devices/bus/neogeo/prot_mslugx.cpp`, `mslugx.cpp` y
`src/mame/snk/neogeo.cpp` (líneas 1439-1440). Solo se usan como documentación del hardware.

- **Hardware:** placa NEO-MVS PROGEOP con un CPLD ALTERA EPM7128 conectado a 250-P1.
- **Ventana de E/S:** `0x2FFFE0`–`0x2FFFEF` (lectura y escritura de 16 bits).
- **Estado interno:** `command` (16 bits) y `counter`.

| Escritura en | Efecto |
|---|---|
| `0x2FFFE0` | `command = 0` (inicio de lectura) |
| `0x2FFFE2`, `0x2FFFE4` | `command \|= dato` (se pulsan con el dato y luego con 0) |
| `0x2FFFE6` | sin efecto (¿fin?) |
| `0x2FFFEA` | `counter = 0`, `command = 0` (inicialización) |

**Lectura** (cualquier dirección de la ventana): devuelve un único bit, de una tabla de bits
que está en el propio P1, en `0x0DEDD2`, empezando por el bit más significativo:

- si `command = 0x0001`: devuelve el bit `counter` de la tabla y luego hace `counter++`
  (MAME anota que este caso se usa en las rutinas `$3BDC` y `$3C30`, registro D7);
- si `command = 0x0FFF`: devuelve el bit número `word[0x10F00A] − 1` (el resto de los
  accesos, registro D2; MAME anota que el cálculo exacto del índice es incierto);
- cualquier otro valor: devuelve 0 y registra el acceso como desconocido.

En el código del juego aparece repetido en línea el patrón
`lea $0DEDD2,a0 / lea $2FFFE0,a1 / ...`: unas 460 referencias, por ejemplo en `0x0039BC`.

**Pendiente:** confirmar este comportamiento en ejecución con el depurador de MAME
(puntos de vigilancia en `0x2FFFE0`–`0x2FFFEF`). Está bloqueado por la falta de `sm1.sm1`.

## Archivos que requiere MAME y que GOG no trae

- **`sm1.sm1`** (BIOS del Z80 en la placa MVS, CRC `94416d67`): sin él, MAME no arranca
  (`Required files are missing`).
- Las demás opciones de BIOS (`sp-s2`, uni-bios, etc.) no hacen falta si se arranca con
  `-bios us`.
