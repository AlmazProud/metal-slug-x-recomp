"""Runs a YM2610 probe in MAME (tools/mame/ym_probe.lua): the game boots from an empty NVRAM, the
probe stops the Z80 and writes the chip's registers, and MAME records the audio.

Usage: python run_probe.py --dir PROBE_DIR [--nvram-from DIR] [--nvram-to DIR] NAME...
Each NAME.txt (probe script) in PROBE_DIR (outside the repo) gives NAME_log.txt and NAME_mame.wav
(-wavwrite at the port's 55 555 Hz). tools/ymprobe/ym_probe.c plays the same script through the
port's YM2610. --nvram-from starts from a saved NVRAM (a second boot: no first-boot watchdog
reset); --nvram-to keeps the NVRAM MAME leaves.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
REPO = Path(__file__).resolve().parents[2]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", type=Path, required=True)
    ap.add_argument("--game", default="mslug3")
    ap.add_argument("--nvram-from", type=Path)
    ap.add_argument("--nvram-to", type=Path)
    ap.add_argument("names", nargs="+")
    a = ap.parse_args()
    d = a.dir.resolve()                             # MAME runs in its own folder
    tools = Path(os.environ["PD_TOOLS"]) if os.environ.get("PD_TOOLS") else ROOT.parent / "tools"
    mame = tools / "mame" / "mame.exe"
    rc = 0
    for name in a.names:
        work = d / f"_mame_{name}"
        if work.exists():
            shutil.rmtree(work)
        if a.nvram_from:
            shutil.copytree(a.nvram_from, work / "nvram")
        else:
            (work / "nvram").mkdir(parents=True)
        env = dict(os.environ, PD_PROBE=str(d / f"{name}.txt"), PD_LOG=str(d / f"{name}_log.txt"))
        cmd = [str(mame), a.game, "-bios", "us", "-rompath", str(ROOT / "roms" / "mame"),
               "-video", "none", "-nothrottle", "-debug", "-debugger", "none",
               "-autoboot_script", str(REPO / "tools" / "mame" / "ym_probe.lua"),
               "-cfg_directory", str(work / "cfg"), "-nvram_directory", str(work / "nvram"),
               "-samplerate", "55555", "-wavwrite", str(d / f"{name}_mame.wav")]
        r = subprocess.run(cmd, cwd=mame.parent, env=env, capture_output=True, text=True)
        if a.nvram_to:
            shutil.copytree(work / "nvram", a.nvram_to, dirs_exist_ok=True)
        shutil.rmtree(work, ignore_errors=True)
        print(f"{name}: MAME exit {r.returncode}")
        rc = rc or r.returncode
    return rc


if __name__ == "__main__":
    sys.exit(main())
