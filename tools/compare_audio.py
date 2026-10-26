"""Compares the port's audio (--wav) with MAME's (-wavwrite), both 16-bit PCM WAV files.

Usage:
  python compare_audio.py PORT.wav MAME.wav [--max-offset-ms 50] [--require-exact]

Finds the offset (in samples) that best aligns both signals and reports, for that offset:
  - Pearson correlation, overall and per second (1.0 = identical up to scale);
  - relative error (RMS of the difference / RMS of MAME);
  - how many samples match exactly.
MAME resamples the YM2610 output (55 555.5 Hz) to the rate requested with -samplerate, so
bit-exact equality is not expected; a correlation close to 1 means the same content.
With --require-exact (a test) it returns 1 unless the offset is 0 and every sample MAME wrote
is equal in the port (the port may run a little longer).
"""
from __future__ import annotations

import argparse
import sys
import wave
from pathlib import Path

import numpy as np


def read_wav(path: Path) -> tuple[int, np.ndarray]:
    with wave.open(str(path), "rb") as w:
        if w.getsampwidth() != 2:
            raise ValueError(f"{path}: 16-bit samples expected")
        data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
        return w.getframerate(), data.reshape(-1, w.getnchannels())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port", type=Path)
    ap.add_argument("mame", type=Path)
    ap.add_argument("--max-offset-ms", type=float, default=50.0)
    ap.add_argument("--require-exact", action="store_true")
    a = ap.parse_args()

    hz_p, p = read_wav(a.port)
    hz_m, m = read_wav(a.mame)
    print(f"port: {len(p)} samples at {hz_p} Hz ({len(p) / hz_p:.2f} s); MAME: {len(m)} at {hz_m} Hz ({len(m) / hz_m:.2f} s)")
    if hz_p != hz_m:
        print("different sample rates: record MAME with -samplerate equal to the port's")
        return 2
    pm, mm = p.mean(axis=1), m.mean(axis=1)
    n = min(len(pm), len(mm))
    pm, mm = pm[:n], mm[:n]

    # Offset by cross-correlation (FFT) within +-max.
    max_d = int(a.max_offset_ms * hz_p / 1000)
    size = 1 << int(np.ceil(np.log2(2 * n)))
    cc = np.fft.irfft(np.fft.rfft(pm, size) * np.conj(np.fft.rfft(mm, size)), size)
    candidates = np.concatenate([cc[:max_d + 1], cc[-max_d:]])
    idx = int(np.argmax(candidates))
    d = idx if idx <= max_d else idx - len(candidates)
    # d > 0: the port is ahead... port[i + d] is aligned with mame[i]
    if d >= 0:
        ps, ms = p[d:], m[:len(p) - d]
    else:
        ps, ms = p[:len(p) + d], m[-d:]
    k = min(len(ps), len(ms))
    ps, ms = ps[:k], ms[:k]
    print(f"best offset: {d} samples ({1000 * d / hz_p:+.2f} ms; positive = the port lags behind)")

    all_exact = d == 0 and k == len(m)
    for c, name in enumerate(("left", "right")):
        x, y = ps[:, c], ms[:, c]
        corr = float(np.corrcoef(x, y)[0, 1]) if x.std() and y.std() else float("nan")
        err = float(np.sqrt(np.mean((x - y) ** 2)) / max(np.sqrt(np.mean(y ** 2)), 1e-9))
        exact = int(np.sum(x == y))
        all_exact = all_exact and exact == k
        print(f"{name} channel: correlation {corr:.5f}  relative error {100 * err:.2f}%  "
              f"exact samples {exact}/{k} ({100 * exact / k:.1f}%)")

    print("correlation per second (mono mix):")
    rows = []
    for s in range(k // hz_p):
        x = ps[s * hz_p:(s + 1) * hz_p].mean(axis=1)
        y = ms[s * hz_p:(s + 1) * hz_p].mean(axis=1)
        if y.std() < 1:
            rows.append(f"{s:3d}s silence" + (" (port too)" if x.std() < 1 else f" (port rms {x.std():.0f})"))
        else:
            rows.append(f"{s:3d}s {np.corrcoef(x, y)[0, 1]:.4f}")
    for i in range(0, len(rows), 5):
        print("   " + "   ".join(rows[i:i + 5]))
    if a.require_exact:
        print("IDENTICAL AUDIO" if all_exact else "AUDIO DIFFERS")
        return 0 if all_exact else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
