"""Phase 4 test: replays in the port a game recorded in MAME and requires zero divergences.

Runs the port without a window using the recorded inputs (tools/mame/inputs.lua) and the clock
date of the recording, writes its fingerprints and compares them with MAME's
(tools/mame/fingerprints.lua) through compare_fingerprints.py.

Usage:
  python verify_mission.py --exe mslugx.exe --roms FOLDER --inputs INPUTS.txt
                             --reference MAME_FINGERPRINTS.txt --frames N --date YYYY-MM-DD-hh-mm-ss-D
                             --output PORT_FINGERPRINTS.txt

How the references are produced (MAME 0.289, empty nvram in a new folder):
  set PD_PIXELS=1 & set PD_FINGERPRINTS=<ref> & set PD_INPUTS=<inputs>
  set PD_INPUTS_SCRIPT=<repo>/tools/mame/inputs.lua
  mame mslugx -bios us -rompath <roms>/mame -playback mision1.inp -input_directory <inp>
       -exit_after_playback -nvram_directory <empty> -video none -sound none -nothrottle
       -autoboot_script <repo>/tools/mame/fingerprints.lua
"""
from __future__ import annotations

import argparse
import os
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
    ap.add_argument("--devmode", action="store_true", help="BIOS development mode (Phase 8 runs)")
    ap.add_argument("--dips", help="debug dips forced on, hex (Phase 8 runs)")
    ap.add_argument("--known-differences", type=Path,
                    help="VBlanks known to differ from MAME (compare_fingerprints.py KNOWN.txt)")
    a = ap.parse_args()

    env = dict(os.environ)
    if a.devmode:
        env["PD_DEVMODE"] = "1"
    if a.dips:
        env["PD_DIPS"] = a.dips
    r = subprocess.run([str(a.exe), "--roms", str(a.roms), "--headless", "--frames", str(a.frames),
                        "--inputs", str(a.inputs), "--fingerprints", str(a.output), "--date", a.date],
                       capture_output=True, text=True, env=env)
    print(r.stdout.strip())
    if r.returncode != 0:
        print(r.stderr)
        return 1
    comparator = Path(__file__).resolve().parent / "compare_fingerprints.py"
    known = [str(a.known_differences)] if a.known_differences else []
    return subprocess.run([sys.executable, str(comparator), str(a.reference), str(a.output)] + known).returncode


if __name__ == "__main__":
    sys.exit(main())
