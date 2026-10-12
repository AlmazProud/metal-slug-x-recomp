# Fase 1: reporte de cobertura del análisis del 68000

Generado por `herramientas/analisis68k.py` (Capstone 5.0.7) a partir de los ROMs del
usuario y de la cobertura dinámica de MAME (`herramientas/cobertura.py`,
513,345,320 instrucciones trazadas).

## P1 (`0x000000`–`0x0FFFFF`, 1 MB)

| Medida | Bytes | % |
|---|---:|---:|
| Código (total) | 503,166 | 48.0 |
| ...confirmado: alcanzado desde vectores, cabecera, llamadas directas o ejecución en MAME | 201,888 | 19.3 |
| ...heurística A: puntero cargado como constante por código confirmado | 202,062 | 19.3 |
| ...heurística B: tabla de 3 o más punteros a código válido | 99,458 | 9.5 |
| ...del total, ejecutado en MAME (cobertura dinámica) | 156,422 | 14.9 |
| Relleno (tramos de 0x00 o 0xFF de 16 bytes o más fuera del código) | 91,298 | 8.7 |
| Datos o desconocido | 454,112 | 43.3 |

- Funciones detectadas: **4872** (lista completa en `docs/funciones_p1.csv`).
- **Validación de las heurísticas contra MAME:** de las 40,404 direcciones de P1 que
  MAME ejecutó, el análisis puramente estático (sin datos de MAME) encuentra el
  **57.3 %** con heurísticas y el 7.3 % sin ellas.
- Instrucciones decodificadas: 121,528.
- Direcciones de datos referenciadas fuera del código: 1,303.
- Saltos o llamadas indirectos en P1 **sin resolver** (sin destino observado en MAME):
  83. Primeros: `001972`, `0022AA`, `002354`, `003A76`, `00B680`, `00BF4A`, `00C210`, `00CE26`, `00CFF8`, `00F098`, `00F35A`, `00F65A`, `00F82C`, `00F9EC`, `00FBBE`
- Direcciones ejecutadas en MAME que el análisis estático no alcanza: 0
  (deben ser 0; si no, hay un fallo de decodificación).
- Direcciones de arranque inválidas (no decodifican): 82.

## Precisión: código que MAME demostró que es dato

MAME registró 12,992 palabras de P1 leídas como datos, y no como instrucciones.
Bytes marcados como código que caen sobre esas palabras, según la evidencia:

| Evidencia | Bytes de código | Falsos positivos seguros |
|---|---:|---:|
| Confirmado | 201,888 | 0 |
| Heurística A (puntero en código) | 202,062 | 0 |
| Heurística B (tabla de punteros) | 99,458 | 0 |

Es una cota inferior: solo detecta los datos que el juego leyó durante la traza. En el análisis
combinado, las rutas heurísticas que pisarían esos datos se cortan (0 cortes),
así que aquí deberían quedar en 0. La validación puramente estática no usa esta información.

## Verificación cruzada con Ghidra

Ghidra 12.1.4 headless, P1 como binario crudo de 68000 big-endian, solo con las raíces
estáticas (vectores y cabecera) y su análisis automático (`herramientas/ghidra/`).

| Medida | Este análisis (combinado) | Ghidra |
|---|---:|---:|
| Bytes de instrucciones | 503,166 | 250,910 |
| Direcciones ejecutadas en MAME que reconoce | 100.0 % | 47.9 % |
| Bytes marcados como código que MAME leyó como datos (falsos positivos seguros) | 0 | 4 |

- Inicios de instrucción en común: 54,609.
- Instrucciones nuestras que caen a mitad de una instrucción de Ghidra (desalineación; uno
  de los dos se equivoca): 64.
- Sin datos de MAME, este análisis reconoce el 57.3 % de lo ejecutado; Ghidra,
  también sin MAME, el 47.9 %.

## Cobertura dinámica por región (direcciones de instrucción distintas)

bios: 1569, p1: 40404, p2_banco1: 144, ram: 28

## Vectores del 68000 (en P1)

| # | Offset | Vector | Destino |
|---|---|---|---|
| 0 | `0x000` | SSP | `10F300` |
| 1 | `0x004` | RESET | `C00402` |
| 2 | `0x008` | BUS_ERROR | `C00408` |
| 3 | `0x00c` | ADDRESS_ERROR | `C0040E` |
| 4 | `0x010` | ILLEGAL | `C00414` |
| 5 | `0x014` | ZERO_DIVIDE | `0022A2` |
| 6 | `0x018` | CHK | `0022A2` |
| 7 | `0x01c` | TRAPV | `0022A2` |
| 8 | `0x020` | PRIVILEGE | `C0041A` |
| 9 | `0x024` | TRACE | `C00420` |
| 10 | `0x028` | LINE_A | `0022A2` |
| 11 | `0x02c` | LINE_F | `0022A2` |
| 24 | `0x060` | SPURIOUS | `C00432` |
| 25 | `0x064` | IRQ1_VBLANK | `0022AC` |
| 26 | `0x068` | IRQ2_TIMER | `002322` |
| 27 | `0x06c` | IRQ3 | `0022AC` |
| 28 | `0x070` | IRQ4 | `C00426` |
| 29 | `0x074` | IRQ5 | `C00426` |
| 30 | `0x078` | IRQ6 | `C00426` |
| 31 | `0x07c` | IRQ7 | `C00426` |

## Entradas de la cabecera Neo Geo

| Offset | Entrada | Instrucción |
|---|---|---|
| `0x122` | USER | `jmp $2432.l` |
| `0x128` | PLAYER_START | `jmp $2658.l` |
| `0x12e` | DEMO_END | `jmp $2800.l` |
| `0x134` | COIN_SOUND | `jmp $27f6.l` |

## Llamadas y saltos al BIOS (`0xC00000`–`0xC0FFFF`)

| Destino | Llamadas (jsr/bsr) | Saltos (jmp) |
|---|---:|---:|
| `C0044A` | 1 | 0 |
| `C00450` | 1 | 0 |
| `C00468` | 3 | 0 |
| `C004C2` | 1 | 0 |
| `C00438` | 0 | 1 |
| `C00444` | 0 | 7 |
