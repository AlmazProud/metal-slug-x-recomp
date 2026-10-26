"""Dynamic coverage of a game's 68000 code using MAME as the oracle (--game: MAME machine).

Launches MAME with the debugger (no UI) and tools/mame/coverage.lua, consumes the
traces in parts as they are produced and deletes them, and saves a JSON with:
  - executed addresses per region (P1, P2 window per bank, BIOS, RAM),
  - observed targets of indirect jumps (jmp/jsr with a parenthesized operand).

P2 window regions are "p2_banco<n>" (Metal Slug X: bank number) or, on a NEO-SMA or NEO-PVC
cartridge, "p2_sma<offset>" / "p2_pvc<offset>" (the P2 offset the window shows, 6 hex digits;
tools/game_desc.py reads the cartridge from the game's descriptor). The bank of an indirect jump
is the number or the offset. NEO-SMA and NEO-PVC runs also record the P2 data reads
("lecturas_p2", P2 offsets). Code run from the NEO-PVC's RAM (0x2FE000 up) is "pvc_ram".

Usage:
    python tools/coverage.py --seconds 180 [--output FILE] [--no-bot]

The result (addresses, not ROM bytes) goes to <root>/analysis/coverage.json by default.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

import game_desc

LINE = re.compile(r"^([0-9A-F]{6}): (\S+)\s*(.*)$")


def short_path(p: Path) -> str:
    """8.3 path (no spaces) for the MAME debugger, which splits arguments on commas/spaces."""
    import ctypes
    buf = ctypes.create_unicode_buffer(1024)
    ctypes.windll.kernel32.GetShortPathNameW(str(p), buf, 1024)
    if not buf.value or " " in buf.value:
        sys.exit(f"No short path without spaces for {p}")
    return buf.value


PVC = False                                            # set by main() from the descriptor


# Region names are JSON keys of the coverage file: keep them.
def region(pc: int) -> str:
    if pc < 0x100000:
        return "p1"
    if 0x2FE000 <= pc < 0x300000 and PVC:
        return "pvc_ram"
    if 0x200000 <= pc < 0x300000:
        return "p2_ventana"
    if 0xC00000 <= pc < 0xD00000:
        return "bios"
    if 0x100000 <= pc < 0x200000:
        return "ram"
    return "otro"


class Accumulator:
    def __init__(self, sma: bool, pvc: bool = False, p2_banks: int = 8) -> None:
        self.p2_banks = p2_banks
        self.pcs: dict[str, set] = defaultdict(set)
        self.indirect: set[tuple[int, int, int]] = set()   # (source, target, bank)
        self.bank = 0                                      # bank at reset: 0 / P2 offset 0
        self.window = "p2_pvc000000" if pvc else "p2_sma000000" if sma else "p2_banco0"
        self.instructions = 0

    def process(self, path: Path) -> None:
        pending = None
        with path.open("r", encoding="ascii", errors="replace") as f:
            for line in f:
                if line.startswith("BANK "):
                    self.bank = int(line[5:])
                    if self.bank >= self.p2_banks:
                        self.bank = 0
                    self.window = f"p2_banco{self.bank}"
                    continue
                if line.startswith(("SMA ", "PVC ")):
                    self.bank = int(line[4:], 16)
                    self.window = f"p2_{line[:3].lower()}{self.bank:06X}"
                    continue
                m = LINE.match(line)
                if not m:
                    continue
                pc = int(m.group(1), 16)
                self.instructions += 1
                r = region(pc)
                self.pcs[r if r != "p2_ventana" else self.window].add(pc)
                if pending is not None:
                    self.indirect.add((pending, pc, self.bank))
                    pending = None
                if m.group(2) in ("jmp", "jsr") and "(" in m.group(3):
                    pending = pc

    def result(self) -> dict:
        return {
            "instrucciones_trazadas": self.instructions,
            "pcs": {k: sorted(v) for k, v in sorted(self.pcs.items())},
            "indirectos": sorted(self.indirect),
        }


def tools_dir(root: Path) -> Path:
    """The portable tools: PD_TOOLS, else <root>/tools, else the shared ../tools."""
    if os.environ.get("PD_TOOLS"):
        return Path(os.environ["PD_TOOLS"])
    return root / "tools" if (root / "tools").is_dir() else root.parent / "tools"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    root = Path(os.environ.get("PD_ROOT", Path(__file__).resolve().parents[2]))
    ap.add_argument("--game", default="mslugx", help="MAME machine (mslug, mslug4h, mslug5, mslug3, mslugx)")
    ap.add_argument("--p2-banks", type=int, default=1,
                    help="P2's 1 MB banks: a bank select value outside them shows bank 0, as bus.c's "
                         "p2_bank (Metal Slug: 1, its second MB, never switched; Metal Slug 4: 4, the "
                         "boot writes 7)")
    ap.add_argument("--seconds", type=int, default=180, help="emulated seconds")
    ap.add_argument("--output", type=Path, default=root / "analysis" / "coverage.json")
    ap.add_argument("--no-bot", action="store_true")
    ap.add_argument("--playback", type=Path,
                    help="plays back a MAME .inp recording (ignores --seconds and disables the bot)")
    a = ap.parse_args()
    if a.playback:
        a.playback = a.playback.resolve()              # MAME runs in its own folder

    work = tools_dir(root) / "_verificacion" / f"cobertura_{a.game}"
    work.mkdir(parents=True, exist_ok=True)
    for old in list(work.glob("traza_*.txt")) + [work / f for f in ("FIN", "lecturas_p1.txt", "lecturas_p2.txt")]:
        old.unlink(missing_ok=True)

    mame = tools_dir(root) / "mame" / "mame.exe"
    lua = Path(__file__).resolve().parent / "mame" / "coverage.lua"
    # A playback requires the same initial state as the recording: empty nvram.
    nvram = work / "nvram"
    if nvram.exists():
        for f in sorted(nvram.rglob("*"), reverse=True):
            f.unlink() if f.is_file() else f.rmdir()
    play = not (a.no_bot or a.playback)
    env = dict(os.environ, PD_TRACE_DIR=short_path(work), PD_PLAY="1" if play else "0")
    global PVC
    sma = game_desc.sma(a.game)
    PVC = game_desc.pvc(a.game)
    if sma:
        env.update(PD_SMA_REG=f"{sma.reg:X}", PD_SMA_BITS=",".join(map(str, sma.bits)),
                   PD_SMA_BANKS=",".join(f"{o:X}" for o in sma.banks))
    if PVC:
        env["PD_PVC"] = "1"
    # The MAME set: roms\mame (Metal Slug 3 and X workspaces) or roms\ (Metal Slug 5's).
    rompath = root / "roms" / "mame" if (root / "roms" / "mame").is_dir() else root / "roms"
    cmd = [str(mame), a.game, "-bios", "us", "-rompath", str(rompath),
           "-video", "none", "-sound", "none", "-nothrottle",
           "-debug", "-debugger", "none", "-autoboot_script", str(lua),
           "-cfg_directory", str(work / "cfg"), "-nvram_directory", str(nvram)]
    if a.playback:
        cmd += ["-playback", a.playback.name, "-input_directory", str(a.playback.parent),
                "-exit_after_playback"]
    else:
        cmd += ["-seconds_to_run", str(a.seconds)]
    proc = subprocess.Popen(cmd, cwd=mame.parent, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    acc = Accumulator(sma is not None, PVC, a.p2_banks)
    done = 0
    start = time.time()

    def process_ready(final: bool) -> None:
        nonlocal done
        while True:
            current = work / f"traza_{done:04d}.txt"
            following = work / f"traza_{done + 1:04d}.txt"
            if not current.exists() or not (final or following.exists()):
                return
            acc.process(current)
            current.unlink()
            done += 1
            print(f"  part {done:3d}  {time.time() - start:6.0f} s  "
                  f"p1={len(acc.pcs['p1'])} bios={len(acc.pcs['bios'])} "
                  f"p2={sum(len(v) for k, v in acc.pcs.items() if k.startswith('p2_'))} "
                  f"indirect={len(acc.indirect)}",
                  flush=True)

    while proc.poll() is None:
        process_ready(False)
        time.sleep(2)
    process_ready(True)

    res = acc.result()
    # (Even) addresses read by the 68000, including opcode fetches, without the words whose last
    # reader is a bulk reader: a PC that was the last reader of BULK words or more (a ROM
    # checksum reads every word once; they are not data the game uses).
    BULK = 0x8000
    res["lectores_masivos"] = {}
    for key in ("lecturas_p1", "lecturas_p2"):
        reads = work / f"{key}.txt"
        if not reads.exists():
            continue
        last = [tuple(int(x, 16) for x in line.split()) for line in reads.read_text().splitlines()]
        per_pc = Counter(pc for _, pc in last)
        bulk = {pc for pc, n in per_pc.items() if n >= BULK}
        res["lectores_masivos"][key] = {f"{pc:06X}": per_pc[pc] for pc in sorted(bulk)}
        res[key] = sorted(w for w, pc in last if pc not in bulk)
        reads.unlink()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(res), encoding="utf-8")
    print(f"MAME exited with code {proc.returncode}; {acc.instructions} instructions; output: {a.output}")
    return 0 if proc.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
