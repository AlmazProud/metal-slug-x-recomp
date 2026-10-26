"""Regression test of the ADPCM-B behaviour learned from MAME (docs/ADPCM_B.md): replays each YM2610
probe through the port's YM2610 (ym_probe) and requires the audio to equal MAME's recording of
the same probe, sample for sample, from the first write after the chip is silenced (frame 700).

Usage: python verify_probes.py --exe ym_probe.exe --rom mslug3_adpcm --dir PROBE_DIR --out DIR NAME...
Each NAME has NAME.txt (script), NAME_log.txt (tools/mame/ym_probe.lua's log) and NAME_mame.wav in
PROBE_DIR (outside the repo).
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np


def read(path: Path) -> np.ndarray:
    with wave.open(str(path), "rb") as w:
        return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--dir", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("names", nargs="+")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    ok = True
    for name in a.names:
        script, log = a.dir / f"{name}.txt", a.dir / f"{name}_log.txt"
        port = a.out / f"{name}_port.wav"
        subprocess.run([str(a.exe), str(a.rom), str(script), str(log), str(port)], check=True)
        m, p = read(a.dir / f"{name}_mame.wav"), read(port)
        first = min(float(l.split()[1]) for l in log.read_text().splitlines() if int(l.split()[0]) > 700)
        s = int(first * 1e-6 * 55555) - 100
        n = min(len(m), len(p))
        diff = np.nonzero(np.any(m[s:n] != p[s:n], axis=1))[0]
        print(f"{name}: {n - s - len(diff)}/{n - s} samples equal"
              + (f", first difference at sample {s + diff[0]}" if len(diff) else ""))
        ok = ok and not len(diff)
    print("ALL PROBES IDENTICAL" if ok else "PROBES DIFFER")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
