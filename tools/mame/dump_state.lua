-- Dumps the state of the 68000 and of work memory at a given frame and closes MAME.
-- Used by the lockstep test to start from a real point in the game (instead of reset).
--
-- Environment variables: PD_FRAME (frame at which to dump), PD_STATE (output file),
-- PD_SMA_REG (NEO-SMA cartridges: the bank register address, hex; selects version 2), PD_PVC=1
-- (NEO-PVC cartridges; selects version 3).
-- Format (big-endian): "PDEST1" or "PDEST2", 19 u32 (D0-D7, A0-A6, SP, USP, SR, PC), 128 bytes of
-- the vector area (0x000000), 16 bytes of the P2 window (0x200000, to deduce or check the bank),
-- 64 KB of RAM (0x100000), 64 KB of SRAM (0xD00000), 8 KB of palette (0x400000).
-- Version 2 adds a u32: the last value written to the NEO-SMA bank register (0xFFFFFFFF if none),
-- since the window's first bytes do not identify an SMA bank. Version 3 adds a u32, the P2 offset
-- the NEO-PVC window shows (bytes 0x2FFFF0-3 kept as the port's bus.c does, from the writes), and
-- the chip's 8 KB of RAM (0x2FE000).

-- MAME runs the autoboot script again after a soft reset (the watchdog): keep the first run's
-- notifier and tap.
if pd_dump_state_loaded then return end
pd_dump_state_loaded = true

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local screen = manager.machine.screens[":screen"]
local target = tonumber(os.getenv("PD_FRAME") or "3000")
local output = os.getenv("PD_STATE") or "estado.bin"
local sma_reg = os.getenv("PD_SMA_REG") and tonumber(os.getenv("PD_SMA_REG"), 16)
local bank_value = 0xFFFFFFFF
local pvc = os.getenv("PD_PVC") == "1"

if pvc then
    local f = { [0] = 0, 0, 0, 0 }
    bank_value = 0
    dump_bank_tap = mem:install_write_tap(0x2FFFF0, 0x2FFFF3, "pd_dump_bank", function(offset, data, mask)
        local o = offset & 2
        if mask & 0xFF00 ~= 0 then f[o] = (data >> 8) & 0xFF end
        if mask & 0x00FF ~= 0 then f[o + 1] = data & 0xFF end
        bank_value = f[2] << 16 | f[3] << 8 | f[0]
        f[0] = f[0] & 0xFE; f[1] = 0xA0; f[2] = f[2] & 0x7F
    end)
elseif sma_reg then
    dump_bank_tap = mem:install_write_tap(sma_reg, sma_reg + 1, "pd_dump_bank", function(offset, data, mask)
        bank_value = data & 0xFFFF
    end)
end

local function block(base, n)
    local t = {}
    for i = 0, n - 1 do t[#t + 1] = string.char(mem:read_u8(base + i)) end
    return table.concat(t)
end

dump_sub = emu.add_machine_frame_notifier(function()
    if screen:frame_number() ~= target then return end
    local f = io.open(output, "wb")
    f:write(pvc and "PDEST3" or sma_reg and "PDEST2" or "PDEST1")
    for _, r in ipairs({"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
                        "A0", "A1", "A2", "A3", "A4", "A5", "A6", "SP", "USP", "SR", "PC"}) do
        f:write(string.pack(">I4", cpu.state[r].value & 0xFFFFFFFF))
    end
    f:write(block(0x000000, 128))
    f:write(block(0x200000, 16))
    f:write(block(0x100000, 0x10000))
    f:write(block(0xD00000, 0x10000))
    f:write(block(0x400000, 0x2000))
    if sma_reg then f:write(string.pack(">I4", bank_value)) end
    if pvc then
        f:write(string.pack(">I4", bank_value))
        f:write(block(0x2FE000, 0x2000))
    end
    f:close()
    print(string.format("PD_DUMPED frame=%d pc=%06X sr=%04X", target,
                        cpu.state["PC"].value, cpu.state["SR"].value))
    manager.machine:exit()
end)
