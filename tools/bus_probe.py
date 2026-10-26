"""Runs black-box bus probes in MAME (tools/mame/bus_probe.lua), one fresh machine per probe.

Usage:
    python tools/bus_probe.py [--game mslug5] [--frame N] [--work DIR] "PROBE" ["PROBE"...]

Each PROBE is a list of accesses for bus_probe.lua ("w16 2FFFF0 0007; r16 2FFFF0; ..."); the
results of each are printed after it. MAME runs headless from <workspace>/roms with -bios us.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPO = Path(__file__).resolve().parents[1]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("probes", nargs="+")
    ap.add_argument("--game", default="mslug5")
    ap.add_argument("--frame", type=int, default=2)
    ap.add_argument("--roms", type=Path, default=Path(os.environ.get("PD_ROMS") or ROOT / "roms"))
    ap.add_argument("--work", type=Path, default=ROOT / "analysis" / "phase2" / "bus_probe")
    pd_tools = os.environ.get("PD_TOOLS")
    ap.add_argument("--mame", type=Path, default=Path(pd_tools or ROOT.parent / "tools", "mame", "mame.exe"))
    a = ap.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    out = (a.work / "bus_probe.txt").resolve()
    for p in a.probes:
        nv = a.work / "nvram"
        shutil.rmtree(nv, ignore_errors=True)
        nv.mkdir()
        out.unlink(missing_ok=True)
        env = dict(os.environ, PD_PROBE=p, PD_PROBE_FRAME=str(a.frame), PD_TRACE=str(out))
        subprocess.run([str(a.mame), a.game, "-bios", "us", "-rompath", str(a.roms.resolve()),
                        "-nvram_directory", str(nv.resolve()), "-cfg_directory", str(nv.resolve()),
                        "-skip_gameinfo", "-video", "none", "-sound", "none", "-seconds_to_run", "30",
                        "-nothrottle", "-autoboot_script", str(REPO / "tools" / "mame" / "bus_probe.lua")],
                       cwd=a.mame.parent, env=env, capture_output=True, text=True)
        print(f"== {p}")
        print(out.read_text() if out.is_file() else "(no output)", end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
