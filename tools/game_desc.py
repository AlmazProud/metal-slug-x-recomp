"""Reads a game's cartridge description from its descriptor (src/games/<id>/game.c), so the Python
tools share the runtime's single copy of it.

Today: the NEO-SMA bank register (`.sma_bank_reg`, `.sma_bank_bits`, the `SMA_BANKS` table) and
whether the cartridge is a NEO-PVC.
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Sma:
    reg: int                     # bank register address
    bits: tuple[int, ...]        # bits of the written value, index bit 0 first
    banks: tuple[int, ...]       # 64 offsets from the start of P2 (MAME region offset 0x100000)

    def index(self, value: int) -> int:
        return sum(((value >> b) & 1) << i for i, b in enumerate(self.bits))

    def base(self, value: int) -> int:
        """P2 offset the window 0x200000 shows after `value` is written to the register."""
        return self.banks[self.index(value)]


def pvc(game: str) -> bool:
    """Whether the game's cartridge has a NEO-PVC (byte-offset P2 window, 8 KB of RAM)."""
    path = REPO / "src" / "games" / game / "game.c"
    return path.exists() and "NG_CART_PVC" in path.read_text(encoding="utf-8")


def sma(game: str) -> Sma | None:
    """The game's NEO-SMA description, or None if its cartridge has no NEO-SMA."""
    path = REPO / "src" / "games" / game / "game.c"
    if not path.exists():
        return None
    t = path.read_text(encoding="utf-8")
    if "NG_CART_SMA" not in t:
        return None
    reg = int(re.search(r"\.sma_bank_reg\s*=\s*(0x[0-9A-Fa-f]+)", t).group(1), 16)
    bits = tuple(int(x) for x in re.search(r"\.sma_bank_bits\s*=\s*\{([^}]*)\}", t).group(1).split(","))
    table = re.search(r"SMA_BANKS\[64\]\s*=\s*\{([^}]*)\}", t).group(1)
    banks = tuple(int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", table))
    assert len(bits) == 6 and len(banks) == 64, game
    return Sma(reg, bits, banks)
