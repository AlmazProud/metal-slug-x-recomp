# Fase 4: arranque y verificación diferencial

**Resultado:** la partida grabada del usuario (`mision1.inp`, 11 969 cuadros, de la pantalla de
título hasta la Esfinge de la Misión 1) se reproduce en el port **sin ninguna divergencia** con
MAME 0.289:

| Qué se compara | Dónde | Resultado |
|---|---|---|
| RAM de trabajo (64 KB) y paleta visible | en cada reconocimiento de la VBlank | **11 771 / 11 771 idénticas** |
| Imagen (320×224, píxel a píxel) | cada cuadro | **11 968 / 11 968 idénticas** |
| Comandos de sonido del 68000 | 1902 comandos | mismos valores, a 1 µs |
| Attract mode sin entradas (Fase 3) | 451 cuadros seguidos + 10 dispersos | todos idénticos |

La prueba es automática: `ctest` → `mision1_contra_mame` (unos 11 s). Las referencias de MAME y
las entradas grabadas viven en `analisis/fase4/`, fuera del repo.

## Método

1. **Huellas en MAME** (`tools/mame/fingerprints.lua`):
   - en cada entrada al manejador de la VBlank (escritura en `3C000C` con el bit 2), un hash
     FNV de la RAM y de la paleta;
   - en cada cuadro, un hash de la imagen (`screen:pixels()`, que entrega la imagen del cuadro
     anterior);
   - las entradas, como **cambios en el tiempo** (`tools/mame/inputs.lua`): instante de
     la última lectura con el valor viejo y de la primera con el nuevo.
2. **El port** (`--entradas`, `--huellas`, `--fecha`) aplica cada cambio de entrada en ese
   instante, con la fecha del reloj de la grabación, y escribe las mismas huellas.
3. **Para localizar cada divergencia:**
   - `tools/mame/cycles.lua` traza en MAME el contador de ciclos de cada instrucción
     (depurador);
   - el port hace lo mismo con `PD_TRAZA_INSTR` (todo por el intérprete, mismos ciclos);
   - `tools/compare_cycles.py` alinea ambas trazas y señala la primera instrucción con
     otra duración o flujo.

Notas de medición aprendidas:
- La RAM en el límite del cuadro no sirve para comparar: MAME puede detenerse a mitad de una
  instrucción. Se mide al entrar al manejador de la VBlank.
- `frame_number()` dentro de un tap no avanza como el cuadro; por eso las entradas van por tiempo.
- La nvram de MAME se guarda al salir: cada corrida usa una carpeta nueva y vacía.

## Divergencias encontradas y corregidas (en orden)

1. **Reset:** en MAME, la primera instrucción empieza en el ciclo 34.
2. **Entrada a la interrupción:**
   - el reconocimiento autovectorizado espera al reloj E (periodo de 10 ciclos);
   - duración = 10 + espera + 35, es decir 48 a 58 ciclos, no los 44 de Musashi.
3. **Muestreo de interrupciones** (`src/cpu/cycles68k.h`).
   - **Regla:** una IRQ activada en T se atiende al terminar una instrucción si T ≤ L, donde L
     es el último punto donde MAME puede detener la CPU antes del paso en que esa instrucción
     muestrea las interrupciones. Ese punto es el fin de un acceso al bus o, si no hay ninguno,
     el inicio de la instrucción.
   - **Origen de las reglas:** son reglas por familia, escritas a partir de la descripción del
     microcódigo que MAME documenta (`m68000-sdf.cpp`).
   - **Verificación:** se comprobaron opcode por opcode con `tools/sampling_queues.py` y
     `verify_sampling.py`, en 2091 casos con 0 diferencias.
4. **Ciclos de instrucciones distintos de la tabla de Musashi**, medidos en MAME:

   | Instrucción | Musashi | 68000 real (MAME) |
   |---|---|---|
   | `ADD/SUB/AND/OR.L <ea>,Dn` y `ADDA/SUBA.L` con fuente Dn/An | 6 | 8 |
   | `ADDQ.W #,An` | 4 | 8 |
   | `ADDA/SUBA.W #imm,An` | 14 | 12 |
   | `ANDI.L #,Dn` | 14 | 16 |
   | `BSET/BCHG/BCLR` sobre Dn con bit < 16 | máximo | 2 menos |
   | `MULS` | perdía la última transición | `38 + 2n`, con n = pares 01/10 de la fuente de 17 bits |
   | `DIVU/DIVS` | fijo (140/158) | según los datos, con el algoritmo publicado de temporización de la división |

   El intérprete de respaldo y las pruebas aplican el mismo ajuste (`cycles_adjust_previous`).
5. **Respuesta del Z80 que se ve a mitad de instrucción:** la lectura de bus ocurre cerca del
   final de la instrucción. Si antes cae un límite de tramo, el Z80 ya corrió hasta él.
6. **Entradas:** se registran por tiempo y no por número de cuadro (punto 3 del método). Esto,
   además, eliminó 11,7 millones de pasos interpretados: el juego ejecutaba otro código.
7. **Error del planificador:** un temporizador del YM2610 hacía procesar la línea siguiente antes
   de tiempo.
8. **Orden escritura de video / dibujo de línea:**
   - una escritura en VRAM, paleta o registros que termina en t_e se dibuja en la línea solo si
     ningún acceso anterior terminó a partir del inicio de esa línea;
   - en MOVE la escritura termina 4 ciclos antes del final de la instrucción; en las de
     lectura-modificación-escritura es el último acceso.
9. **Frecuencia de muestreo del YM:** MAME genera 55 555 muestras/s exactas, no 55 555,55. El
   audio del port ya no se desfasa con el tiempo.

## Diferencias residuales conocidas

- **Sound, a nivel de muestra.**
  - El Z80 del port atiende a veces la IRQ del YM2610 una instrucción antes que el de MAME. El
    primer caso aparece a los 15,1 s.
  - Algunos sonidos arrancan de 0 a 3 muestras corridos (menos de 0,1 ms). La energía de la
    señal coincide segundo a segundo y hay tramos con correlación 1,0 exacta.
  - No afecta al juego: el 68000 recibe las mismas respuestas en los mismos cuadros.
  - Igualarlo exige modelar cuándo ve las interrupciones el Z80 de MAME, como se hizo con el
    68000.
- **Clases de muestreo sin verificar con trazas:**
  - DIVU/DIVS con operando en memoria;
  - CHK, TRAPV, RESET, MOVEP;
  - MOVEM se modela como "último acceso al final" según el análisis.
- **Orden de las escrituras de 32 bits:** con `MOVE.L` a `-(An)` el 68000 escribe primero la
  palabra baja; el port, siempre la alta. No afectó a la grabación.
