"""Audio test: replays a MAME run in the port and requires its audio to be identical to MAME's.

Runs the port without a window with the recorded inputs (tools/mame/inputs.lua) and the clock
date of the recording, writes its audio (--wav) and compares it with MAME's -wavwrite output
through compare_audio.py --require-exact: same offset (0) and every sample equal.

Usage:
  python verify_audio.py --exe mslug3.exe --roms FOLDER --inputs INPUTS.txt --reference MAME.wav
                         --frames N --date YYYY-MM-DD-hh-mm-ss-D --output PORT.wav

The references come from tools/mame_run.py --wav (MAME 0.289 at 55 555 Hz).
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--roms", type=Path, required=True)
    ap.add_argument("--inputs", type=Path, required=True)
    ap.add_argument("--reference", type=Path, required=True)
    ap.add_argument("--frames", type=int, required=True)
    ap.add_argument("--date", required=True)
    ap.add_argument("--output", type=Path, required=True)
    a = ap.parse_args()

    r = subprocess.run([str(a.exe), "--roms", str(a.roms), "--headless", "--frames", str(a.frames),
                        "--inputs", str(a.inputs), "--date", a.date, "--wav", str(a.output)],
                       capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode != 0:
        print(r.stderr)
        return 1
    comparator = Path(__file__).resolve().parent / "compare_audio.py"
    return subprocess.run([sys.executable, str(comparator), str(a.output), str(a.reference),
                           "--require-exact"]).returncode


if __name__ == "__main__":
    sys.exit(main())
