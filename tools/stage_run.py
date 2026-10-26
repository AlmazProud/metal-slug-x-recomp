"""Metal Slug X: scripted MAME runs of each stage for the per-mission verification (Phase 8).

Builds an input script that starts a game, opens the developers' hidden menu (BIOS development
mode, SELECT held + D twice), picks a stage in its stage select and then plays with a simple bot;
runs it in MAME (tools/mame/scripted_run.lua) and writes the references the port is compared with
(tools/verify_mission.py --devmode --dips):

  <out>/stageNN_script.txt        the input script (port headless format)
  <out>/stageNN_inputs.txt        input changes recorded by inputs.lua (port --inputs)
  <out>/stageNN_fingerprints.txt  MAME fingerprints (V, F, E lines)
  <out>/stageNN.inp               the MAME recording (plays the same run back, with its clock)
  <out>/stageNN_date.txt          clock date of the recording (port --date)
  <out>/stageNN_frames.txt        frames the port must run to cover the whole MAME run

Usage:
  python stage_run.py --stage N [--frames F] [--dips HEX] --out DIR [--replay] [--screenshots LIST]

Stage N is the N-th entry of the developers' stage select (1 = Mission 1, 13 = ending).
"""
from __future__ import annotations

import argparse
import datetime
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPO = Path(__file__).resolve().parents[1]

# Frames (screen:frame_number()) of the menu steps; verified with screenshots in the port.
START = [(600, 605, "coin1"), (700, 705, "start1"), (1000, 1005, "a"), (1300, 1305, "a")]
DEV_MENU = [(1900, 1960, "select1"), (1910, 1915, "d"), (2000, 2060, "select1"), (2010, 2015, "d")]


def build_script(stage: int, frames: int) -> str:
    lines = [f"{a} {b} {k}" for a, b, k in START + DEV_MENU]
    f = 2140                                         # menu open: item 2 is the stage select
    for _ in range(2):
        lines.append(f"{f} {f + 4} down"); f += 12
    lines.append(f"{f + 10} {f + 14} a"); f += 60     # stage list, cursor on the first entry
    for _ in range(stage - 1):
        lines.append(f"{f} {f + 4} down"); f += 12
    lines.append(f"{f + 10} {f + 14} a"); f += 300    # load the stage
    lines.append("# bot: mostly right, firing; jumps, grenades and some up/left")
    period = 240
    for s in range(f, frames, period):
        lines.append(f"{s} {s + 199} right")
        lines.append(f"{s + 215} {s + 235} left")
        for k in range(0, period, 6):
            lines.append(f"{s + k} {s + k + 2} a")
        lines.append(f"{s + 40} {s + 47} b")
        lines.append(f"{s + 130} {s + 137} b")
        if (s // period) % 2:
            lines.append(f"{s + 100} {s + 103} c")
            lines.append(f"{s + 160} {s + 190} up")
    return "\n".join(lines) + "\n"


def inp_date(inp: Path) -> str:
    """Clock date of a MAME recording: the header's base time (seconds since 1970) in local time,
    which is how MAME starts the uPD4990A when it plays the recording back."""
    base = int.from_bytes(inp.read_bytes()[8:16], "little")
    t = datetime.datetime.fromtimestamp(base)
    return t.strftime("%Y-%m-%d-%H-%M-%S") + f"-{t.isoweekday() % 7}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", type=int, required=True)
    ap.add_argument("--frames", type=int, default=9000)
    ap.add_argument("--dips", default="8000", help="debug dips forced on (8000 = invincible)")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--replay", action="store_true",
                    help="play the existing recording back (same run, same clock) into "
                         "stageNN_replay_*.txt; extra PD_* variables (PD_DUMP_V...) pass through")
    ap.add_argument("--debug", action="store_true", help="run MAME with its debugger (cycles.lua traces)")
    ap.add_argument("--screenshots", help="frames to capture in MAME (comma-separated), to <out>/shots")
    a = ap.parse_args()

    a.out = a.out.resolve()                         # MAME runs in its own folder
    a.out.mkdir(parents=True, exist_ok=True)
    base = a.out / f"stage{a.stage:02d}"
    name = f"stage{a.stage:02d}"
    script = Path(f"{base}_script.txt")
    out = Path(f"{base}_replay" if a.replay else str(base))
    if not a.replay:
        script.write_text(build_script(a.stage, a.frames), encoding="utf-8")

    work = a.out / f"_mame{a.stage:02d}{'r' if a.replay else ''}"
    if work.exists():
        shutil.rmtree(work)
    (work / "nvram").mkdir(parents=True)
    mame = Path(os.environ["PD_TOOLS"]) / "mame" / "mame.exe" if os.environ.get("PD_TOOLS") else ROOT / "tools" / "mame" / "mame.exe"
    env = dict(os.environ, PD_RUN_SCRIPT=str(script), PD_DEVMODE="1", PD_DIPS=a.dips,
               PD_FINGERPRINTS_SCRIPT=str(REPO / "tools" / "mame" / "fingerprints.lua"),
               PD_INPUTS_SCRIPT=str(REPO / "tools" / "mame" / "inputs.lua"),
               PD_FINGERPRINTS=f"{out}_fingerprints.txt", PD_INPUTS=f"{out}_inputs.txt", PD_PIXELS="1")
    if a.screenshots:
        env.update(PD_SCREENSHOTS_SCRIPT=str(REPO / "tools" / "mame" / "screenshots.lua"),
                   PD_SCREENSHOT_FRAMES=a.screenshots, PD_PREFIX=name)
    cmd = [str(mame), "mslugx", "-bios", "us", "-rompath", str(ROOT / "roms" / "mame"),
           "-video", "none", "-sound", "none", "-nothrottle",
           "-autoboot_script", str(REPO / "tools" / "mame" / "scripted_run.lua"),
           "-cfg_directory", str(work / "cfg"), "-nvram_directory", str(work / "nvram"),
           "-snapshot_directory", str(a.out / "shots"), "-input_directory", str(a.out)]
    if a.debug:
        cmd += ["-debug", "-debugger", "none"]
    if a.replay:
        cmd += ["-playback", f"{name}.inp", "-exit_after_playback"]
    else:
        seconds = round(a.frames / 59.1856)
        cmd += ["-record", f"{name}.inp", "-seconds_to_run", str(seconds)]
        # Frames the port must run to cover every MAME VBlank (MAME stops after `seconds`).
        Path(f"{base}_frames.txt").write_text(str(int(seconds * 59.1856) + 5), encoding="utf-8")
    r = subprocess.run(cmd, cwd=mame.parent, env=env, capture_output=True, text=True)
    shutil.rmtree(work, ignore_errors=True)
    if r.returncode == 0 and not a.replay:
        Path(f"{base}_date.txt").write_text(inp_date(a.out / f"{name}.inp"), encoding="utf-8")
    print(f"stage {a.stage}: MAME exit {r.returncode}{' (replay)' if a.replay else ''}")
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
