-- Dumps the state of the 68000 and of work memory at a given frame and closes MAME.
-- Used by the lockstep test to start from a real point in the game (instead of reset).
--
-- Environment variables: PD_FRAME (frame at which to dump), PD_STATE (output file).
-- Format (big-endian): "PDEST1", 19 u32 (D0-D7, A0-A6, SP, USP, SR, PC), 128 bytes of the
-- vector area (0x000000), 16 bytes of the P2 window (0x200000, to deduce the bank),
-- 64 KB of RAM (0x100000), 64 KB of SRAM (0xD00000), 8 KB of palette (0x400000).

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local screen = manager.machine.screens[":screen"]
local target = tonumber(os.getenv("PD_FRAME") or "3000")
local output = os.getenv("PD_STATE") or "estado.bin"

local function block(base, n)
    local t = {}
    for i = 0, n - 1 do t[#t + 1] = string.char(mem:read_u8(base + i)) end
    return table.concat(t)
end

dump_sub = emu.add_machine_frame_notifier(function()
    if screen:frame_number() ~= target then return end
    local f = io.open(output, "wb")
    f:write("PDEST1")
    for _, r in ipairs({"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
                        "A0", "A1", "A2", "A3", "A4", "A5", "A6", "SP", "USP", "SR", "PC"}) do
        f:write(string.pack(">I4", cpu.state[r].value & 0xFFFFFFFF))
    end
    f:write(block(0x000000, 128))
    f:write(block(0x200000, 16))
    f:write(block(0x100000, 0x10000))
    f:write(block(0xD00000, 0x10000))
    f:write(block(0x400000, 0x2000))
    f:close()
    print(string.format("PD_DUMPED frame=%d pc=%06X sr=%04X", target,
                        cpu.state["PC"].value, cpu.state["SR"].value))
    manager.machine:exit()
end)
