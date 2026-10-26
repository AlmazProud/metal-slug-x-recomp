"""Scripted MAME runs: the references the port is compared with (tools/verify_mission.py).

Runs a game in MAME 0.289 from an input script (tools/mame/scripted_run.lua), recording it, and
writes:

  <out>/<name>_script.txt        the input script (port headless format)
  <out>/<name>_inputs.txt        input changes recorded by inputs.lua (port --inputs)
  <out>/<name>_fingerprints.txt  MAME fingerprints (V, F, E lines)
  <out>/<name>.inp               the MAME recording (plays the same run back, with its clock)
  <out>/<name>_date.txt          clock date of the recording (port --date)
  <out>/<name>_frames.txt        frames the port must run to cover the whole MAME run

Built-in scripts (--script):
  attract   no inputs: boot, BIOS and the attract demo
  start     coin, START, the first soldier, then a simple bot (mostly right, firing)
  FILE      any script in the port's headless format ("<from> <to> token..." per line)

Usage:
  python mame_run.py --game mslug --script attract --frames 10000 --out DIR [--name NAME]
                     [--replay] [--screenshots LIST] [--debug] [--wav]

--wav also writes the audio, <out>/<name>[_replay]_mame.wav (-wavwrite at the port's 55 555 Hz,
tools/verify_audio.py compares it).

A game recorded by hand (MAME -record from an empty NVRAM, e.g. MS3_Port/grabar_partida.bat) is
replayed with --replay --name NAME and no --script: the missing <name>_script.txt (no scripted
inputs), _date.txt and _frames.txt (MAME's playback length) are written from the recording.

PD_POKE (RAM writes at given frames, tools/mame/scripted_run.lua) is kept in <out>/<name>_poke.txt,
and PD_DEVMODE, PD_DEVMODE_COPY, PD_DIPS, PD_BLOOD and PD_KILL_ENEMIES (developer mode, debug dips,
red blood, Metal Slug's enemy kills) in <name>_env.txt (KEY=VALUE lines); both are applied again
on --replay, and the port's tests read the same files.

--replay plays the recording back into <name>_replay_*.txt; PD_* variables of the Lua scripts
pass through (PD_DUMP_V, PD_DUMP_VRAM, PD_EXTRA_SCRIPT...). The MAME set comes from
<workspace>/roms/mame (tools/extract_roms.py rebuilds it from the user's copy). Metal Slug X's
per-stage runs are tools/stage_run.py.
"""
from __future__ import annotations

import argparse
import datetime
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPO = Path(__file__).resolve().parents[1]
FPS = 59.1856
RUN_ENV = ("PD_DEVMODE", "PD_DEVMODE_COPY", "PD_DIPS", "PD_DIPS_WORD", "PD_BLOOD", "PD_KILL_ENEMIES")   # kept per run in <name>_env.txt

# Frames (screen:frame_number()) of the menu steps; checked with screenshots in MAME.
START = {
    # Metal Slug X (tools/stage_run.py): coin, START, then A twice through the soldier select.
    "mslugx": [(600, 605, "coin1"), (700, 705, "start1"), (1000, 1005, "a"), (1300, 1305, "a")],
    "mslug3": [(1800, 1805, "coin1"), (1900, 1905, "start1"), (2200, 2205, "a"), (2450, 2455, "a")],
    # Metal Slug 5: after START the game's "How to play" runs until Soldier Select (about 5800).
    "mslug5": [(2600, 2605, "coin1"), (2800, 2805, "start1"), (6100, 6105, "a"), (6400, 6405, "a")],
    # Metal Slug 4: the "How to play" until about 3600, Soldier Select by 4500.
    "mslug4h": [(1800, 1805, "coin1"), (2000, 2005, "start1"), (4700, 4705, "a"), (5000, 5005, "a")],
    # Metal Slug: no soldier select for one player; the game's "How to play" runs from about 2100
    # (to about 4700 untouched) and the bot, from 2605, cuts it short with A.
    "mslug": [(1800, 1805, "coin1"), (2000, 2005, "start1")],
}


def bot(first: int, frames: int) -> list[str]:
    lines = ["# bot: mostly right, firing; jumps, grenades and some up/left"]
    period = 240
    for s in range(first, frames, period):
        lines.append(f"{s} {s + 199} right")
        lines.append(f"{s + 215} {s + 235} left")
        for k in range(0, period, 6):
            lines.append(f"{s + k} {s + k + 2} a")
        lines.append(f"{s + 40} {s + 47} b")
        lines.append(f"{s + 130} {s + 137} b")
        if (s // period) % 2:
            lines.append(f"{s + 100} {s + 103} c")
            lines.append(f"{s + 160} {s + 190} up")
    return lines


def build_script(game: str, script: str, frames: int) -> str:
    if script == "attract":
        return "# attract mode: no inputs\n"
    if script == "start":
        steps = START[game]
        lines = [f"{a} {b} {k}" for a, b, k in steps]
        return "\n".join(lines + bot(steps[-1][1] + 600, frames)) + "\n"
    return Path(script).read_text(encoding="utf-8")


def inp_date(inp: Path) -> str:
    """Clock date of a MAME recording: the header's base time (seconds since 1970) in local time,
    which is how MAME starts the uPD4990A when it plays the recording back."""
    base = int.from_bytes(inp.read_bytes()[8:16], "little")
    t = datetime.datetime.fromtimestamp(base)
    return t.strftime("%Y-%m-%d-%H-%M-%S") + f"-{t.isoweekday() % 7}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="mslugx", help="MAME machine (mslug, mslug4h, mslug5, mslug3, mslugx)")
    ap.add_argument("--script", help="attract, start or a script file (not needed with --replay --name)")
    ap.add_argument("--frames", type=int, default=10000)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--name", help="file prefix (default: the script name)")
    ap.add_argument("--replay", action="store_true",
                    help="play the existing recording back (same run, same clock) into <name>_replay_*.txt")
    ap.add_argument("--debug", action="store_true", help="run MAME with its debugger (cycles.lua traces)")
    ap.add_argument("--screenshots", help="frames to capture in MAME (comma-separated), to <out>/shots")
    ap.add_argument("--wav", action="store_true", help="record the audio too (-wavwrite, 55 555 Hz)")
    a = ap.parse_args()
    if not a.script and not (a.replay and a.name):
        ap.error("--script is required (or --replay --name NAME for a recorded game)")

    name = a.name or Path(a.script).stem
    a.out = a.out.resolve()                         # MAME runs in its own folder
    a.out.mkdir(parents=True, exist_ok=True)
    base = a.out / name
    script = Path(f"{base}_script.txt")
    out = Path(f"{base}_replay" if a.replay else str(base))
    poke = Path(f"{base}_poke.txt")
    run_env = Path(f"{base}_env.txt")
    if not a.replay:
        script.write_text(build_script(a.game, a.script, a.frames), encoding="utf-8")
        if os.environ.get("PD_POKE"):
            poke.write_text(os.environ["PD_POKE"], encoding="utf-8")
        else:
            poke.unlink(missing_ok=True)
        kept = [f"{k}={os.environ[k]}" for k in RUN_ENV if os.environ.get(k)]
        if kept:
            run_env.write_text("\n".join(kept) + "\n", encoding="utf-8")
        else:
            run_env.unlink(missing_ok=True)
    elif not script.exists():                       # a game recorded by hand
        script.write_text("# recorded by hand: no scripted inputs\n", encoding="utf-8")

    work = a.out / f"_mame_{name}{'_r' if a.replay else ''}"
    if work.exists():
        shutil.rmtree(work)
    (work / "nvram").mkdir(parents=True)
    tools = Path(os.environ["PD_TOOLS"]) if os.environ.get("PD_TOOLS") else ROOT / "tools"
    if not (tools / "mame").is_dir():
        tools = ROOT.parent / "tools"
    mame = tools / "mame" / "mame.exe"
    env = dict(os.environ)
    if a.replay and poke.exists():
        env["PD_POKE"] = poke.read_text(encoding="utf-8").strip()
    if a.replay and run_env.exists():
        env.update(line.split("=", 1) for line in run_env.read_text(encoding="utf-8").split())
    env.update(PD_RUN_SCRIPT=str(script),
               PD_FINGERPRINTS_SCRIPT=str(REPO / "tools" / "mame" / "fingerprints.lua"),
               PD_INPUTS_SCRIPT=str(REPO / "tools" / "mame" / "inputs.lua"),
               PD_FINGERPRINTS=f"{out}_fingerprints.txt", PD_INPUTS=f"{out}_inputs.txt", PD_PIXELS="1")
    if a.screenshots:
        env.update(PD_SCREENSHOTS_SCRIPT=str(REPO / "tools" / "mame" / "screenshots.lua"),
                   PD_SCREENSHOT_FRAMES=a.screenshots, PD_PREFIX=name)
    # The MAME set: <workspace>/roms/mame (Metal Slug 3: rebuilt by extract_roms.py) or, for Metal
    # Slug 5 and 4, <workspace>/roms itself (mslug5.zip / the rebuilt mslug4h, and neogeo).
    rompath = ROOT / "roms" / "mame" if (ROOT / "roms" / "mame").is_dir() else ROOT / "roms"
    cmd = [str(mame), a.game, "-bios", "us", "-rompath", str(rompath),
           "-video", "none", "-sound", "none", "-nothrottle",
           "-autoboot_script", str(REPO / "tools" / "mame" / "scripted_run.lua"),
           "-cfg_directory", str(work / "cfg"), "-nvram_directory", str(work / "nvram"),
           "-snapshot_directory", str(a.out / "shots"), "-input_directory", str(a.out)]
    if a.debug:
        cmd += ["-debug", "-debugger", "none"]
    if a.wav:
        cmd += ["-samplerate", "55555", "-wavwrite", f"{out}_mame.wav"]
    if a.replay:
        cmd += ["-playback", f"{name}.inp", "-exit_after_playback"]
    else:
        seconds = round(a.frames / FPS)
        cmd += ["-record", f"{name}.inp", "-seconds_to_run", str(seconds)]
        # Frames the port must run to cover every MAME VBlank (MAME stops after `seconds`).
        Path(f"{base}_frames.txt").write_text(str(int(seconds * FPS) + 5), encoding="utf-8")
    r = subprocess.run(cmd, cwd=mame.parent, env=env, capture_output=True, text=True)
    shutil.rmtree(work, ignore_errors=True)
    if r.returncode == 0 and (not a.replay or not Path(f"{base}_date.txt").exists()):
        Path(f"{base}_date.txt").write_text(inp_date(a.out / f"{name}.inp"), encoding="utf-8")
    played = re.search(r"Total playback frames: (\d+)", r.stdout + r.stderr)
    if r.returncode == 0 and a.replay and played and not Path(f"{base}_frames.txt").exists():
        Path(f"{base}_frames.txt").write_text(played.group(1), encoding="utf-8")
    print(f"{a.game} {name}: MAME exit {r.returncode}{' (replay)' if a.replay else ''}")
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
