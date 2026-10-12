"""Compara el audio del port (--wav) con el de MAME (-wavwrite), ambos WAV PCM de 16 bits.

Uso:
  python compare_audio.py PORT.wav MAME.wav [--max-desfase-ms 50]

Busca el desfase (en muestras) que mejor alinea ambas señales y reporta, para ese desfase:
  - correlación de Pearson global y por segundo (1.0 = idénticas salvo escala);
  - error relativo (RMS de la diferencia / RMS de MAME);
  - cuántas muestras coinciden exactamente.
MAME remuestrea la salida del YM2610 (55 555,5 Hz) a la frecuencia pedida con -samplerate, así
que no se espera igualdad bit a bit; una correlación cercana a 1 indica el mismo contenido.
"""
from __future__ import annotations

import argparse
import sys
import wave
from pathlib import Path

import numpy as np


def leer(ruta: Path) -> tuple[int, np.ndarray]:
    with wave.open(str(ruta), "rb") as w:
        if w.getsampwidth() != 2:
            raise ValueError(f"{ruta}: se esperan muestras de 16 bits")
        datos = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
        return w.getframerate(), datos.reshape(-1, w.getnchannels())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port", type=Path)
    ap.add_argument("mame", type=Path)
    ap.add_argument("--max-desfase-ms", type=float, default=50.0)
    a = ap.parse_args()

    hz_p, p = leer(a.port)
    hz_m, m = leer(a.mame)
    print(f"port: {len(p)} muestras a {hz_p} Hz ({len(p) / hz_p:.2f} s); MAME: {len(m)} a {hz_m} Hz ({len(m) / hz_m:.2f} s)")
    if hz_p != hz_m:
        print("frecuencias distintas: graba MAME con -samplerate igual a la del port")
        return 2
    pm, mm = p.mean(axis=1), m.mean(axis=1)
    n = min(len(pm), len(mm))
    pm, mm = pm[:n], mm[:n]

    # Desfase por correlación cruzada (FFT) dentro de +-max.
    max_d = int(a.max_desfase_ms * hz_p / 1000)
    tam = 1 << int(np.ceil(np.log2(2 * n)))
    cc = np.fft.irfft(np.fft.rfft(pm, tam) * np.conj(np.fft.rfft(mm, tam)), tam)
    candidatos = np.concatenate([cc[:max_d + 1], cc[-max_d:]])
    idx = int(np.argmax(candidatos))
    d = idx if idx <= max_d else idx - len(candidatos)
    # d > 0: el port va adelantado... se alinea port[i + d] con mame[i]
    if d >= 0:
        ps, ms = p[d:], m[:len(p) - d]
    else:
        ps, ms = p[:len(p) + d], m[-d:]
    k = min(len(ps), len(ms))
    ps, ms = ps[:k], ms[:k]
    print(f"desfase óptimo: {d} muestras ({1000 * d / hz_p:+.2f} ms; positivo = el port va detrás)")

    for c, nombre in enumerate(("izquierdo", "derecho")):
        x, y = ps[:, c], ms[:, c]
        corr = float(np.corrcoef(x, y)[0, 1]) if x.std() and y.std() else float("nan")
        err = float(np.sqrt(np.mean((x - y) ** 2)) / max(np.sqrt(np.mean(y ** 2)), 1e-9))
        exactas = int(np.sum(x == y))
        print(f"canal {nombre}: correlación {corr:.5f}  error relativo {100 * err:.2f}%  "
              f"muestras exactas {exactas}/{k} ({100 * exactas / k:.1f}%)")

    print("correlación por segundo (mezcla mono):")
    filas = []
    for s in range(k // hz_p):
        x = ps[s * hz_p:(s + 1) * hz_p].mean(axis=1)
        y = ms[s * hz_p:(s + 1) * hz_p].mean(axis=1)
        if y.std() < 1:
            filas.append(f"{s:3d}s silencio" + (" (port también)" if x.std() < 1 else f" (port rms {x.std():.0f})"))
        else:
            filas.append(f"{s:3d}s {np.corrcoef(x, y)[0, 1]:.4f}")
    for i in range(0, len(filas), 5):
        print("   " + "   ".join(filas[i:i + 5]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
