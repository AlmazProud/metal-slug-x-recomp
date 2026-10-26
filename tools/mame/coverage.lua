-- Dynamic 68000 coverage in MAME (requires -debug -debugger none).
-- Launched by tools/coverage.py; not used on its own.
--
-- Environment variables:
--   PD_TRACE_DIR  folder (path WITHOUT spaces, e.g. 8.3) where traza_NNNN.txt files are written
--   PD_FRAMES     frames per trace file (300 by default)
--   PD_PLAY      1 = simple bot that inserts coins and plays (1 by default)
--
--   PD_SMA_REG, PD_SMA_BITS, PD_SMA_BANKS  NEO-SMA cartridge (set by coverage.py from the game's
--                descriptor): bank register (hex), the 6 value bits of the index and the 64 P2
--                offsets (hex), comma-separated
--   PD_PVC        1 = NEO-PVC cartridge (set by coverage.py from the game's descriptor)
--
-- Every P2 bank change is noted in the trace with tracelog, so the analyzer knows which bank is
-- active: "BANK n" (write to 0x2FFFF0, Metal Slug X) or "SMA offset" / "PVC offset" (NEO-SMA,
-- NEO-PVC: the P2 offset, in hex, that the window 0x200000 shows from then on).

-- MAME runs the autoboot script again after a soft reset (the watchdog): the taps and notifiers
-- of the first run stay installed, so a second run must do nothing.
if pd_coverage_loaded then return end
pd_coverage_loaded = true

local dbg = manager.machine.debugger
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local screen = manager.machine.screens[":screen"]
local dir = os.getenv("PD_TRACE_DIR")
local per_file = tonumber(os.getenv("PD_FRAMES") or "300")
local play = (os.getenv("PD_PLAY") or "1") == "1"
local part = 0

local function new_trace()
    dbg:command(string.format("trace %s\\traza_%04d.txt,maincpu,noloop", dir, part))
    part = part + 1
end

local sma_reg = os.getenv("PD_SMA_REG")
local pvc = os.getenv("PD_PVC") == "1"
local sma_base = 0                                     -- P2 offset the window shows (SMA, PVC)
if pvc then
    -- The chip's bytes 0x2FFFF0-3, kept as the port's bus.c does (docs/PHASE2_HARDWARE.md): the
    -- written bytes are stored, the window shows P2 offset F2 << 16 | F3 << 8 | F0, then F0 loses
    -- bit 0, F1 reads 0xA0 and F2 loses bit 7.
    local f = { [0] = 0, 0, 0, 0 }
    bank_tap = mem:install_write_tap(0x2FFFF0, 0x2FFFF3, "pd_cov_bank", function(offset, data, mask)
        local o = offset & 2
        if mask & 0xFF00 ~= 0 then f[o] = (data >> 8) & 0xFF end
        if mask & 0x00FF ~= 0 then f[o + 1] = data & 0xFF end
        sma_base = f[2] << 16 | f[3] << 8 | f[0]
        f[0] = f[0] & 0xFE; f[1] = 0xA0; f[2] = f[2] & 0x7F
        dbg:command(string.format('tracelog "PVC %X\\n"', sma_base))
    end)
elseif sma_reg then
    sma_reg = tonumber(sma_reg, 16)
    local bits, banks = {}, {}
    for b in os.getenv("PD_SMA_BITS"):gmatch("%d+") do bits[#bits + 1] = tonumber(b) end
    for o in os.getenv("PD_SMA_BANKS"):gmatch("%x+") do banks[#banks + 1] = tonumber(o, 16) end
    bank_tap = mem:install_write_tap(sma_reg, sma_reg + 1, "pd_cov_bank", function(offset, data, mask)
        local n = 0
        for i, b in ipairs(bits) do n = n | (((data >> b) & 1) << (i - 1)) end
        sma_base = banks[n + 1]
        dbg:command(string.format('tracelog "SMA %X\\n"', sma_base))
    end)
else
    bank_tap = mem:install_write_tap(0x2FFFF0, 0x2FFFFF, "pd_cov_bank", function(offset, data, mask)
        dbg:command(string.format('tracelog "BANK %d\\n"', data & 7))
    end)
end

-- Reads from P1 (and on NEO-SMA from the P2 window, as P2 offsets). They include instruction
-- fetches; coverage.py subtracts the bytes of executed instructions to keep only the data reads.
-- Each word keeps the PC of its last reader, so coverage.py can leave out bulk readers (a ROM
-- checksum reads every word).
local reads, reads_p2 = {}, {}
local pc_reg = cpu.state["PC"]
read_tap = mem:install_read_tap(0x000000, 0x0FFFFF, "pd_cov_rd", function(offset, data, mask)
    reads[offset & 0xFFFFE] = pc_reg.value
end)
if sma_reg or pvc then
    local top = pvc and 0x2FDFFF or 0x2FFFFF          -- NEO-PVC: 0x2FE000 up is the chip's RAM
    read_p2_tap = mem:install_read_tap(0x200000, top, "pd_cov_rd2", function(offset, data, mask)
        reads_p2[sma_base + (offset & 0xFFFFE) - 0x200000] = pc_reg.value
    end)
end

-- Input bot: it does not try to play well, only to walk through as much code as possible.
local fields = {}
for _, port in pairs(manager.machine.ioport.ports) do
    for name, field in pairs(port.fields) do fields[name] = field end
end
local function set(name, v) if fields[name] then fields[name]:set_value(v) end end

-- PD_POKE="frame,address,value[;...]" (hex address and value): writes a work RAM byte in the frame
-- notifier of that frame, as tools/mame/scripted_run.lua does (to replay its runs).
local pokes = {}
for f, a, v in string.gmatch(os.getenv("PD_POKE") or "", "(%d+),(%x+),(%x+)") do
    table.insert(pokes, { frame = tonumber(f), addr = tonumber(a, 16), value = tonumber(v, 16) })
end

frame_sub = emu.add_machine_frame_notifier(function()
    local f = screen:frame_number()
    if f % per_file == 0 then new_trace() end
    for _, p in ipairs(pokes) do
        if p.frame == f then mem:write_u8(p.addr, p.value) end
    end
    if not play or f < 600 then return end
    local c = f % 900                                  -- every 15 s: coin + START (continue)
    set("Coin 1", (c >= 0 and c < 6) and 1 or 0)
    set("1 Player Start", (c >= 60 and c < 66) and 1 or 0)
    set("P1 Right", (f % 240 < 200) and 1 or 0)     -- almost always to the right
    set("P1 Left", (f % 240 >= 215) and 1 or 0)
    set("P1 Up", (f % 500 >= 470) and 1 or 0)
    set("P1 Button 1", (f % 6 < 3) and 1 or 0)      -- fire
    set("P1 Button 2", (f % 90 < 8) and 1 or 0)     -- jump
    set("P1 Button 3", (f % 400 < 4) and 1 or 0)    -- grenade
end)

stop_sub = emu.add_machine_stop_notifier(function()
    dbg:command("trace off,maincpu")
    local l = io.open(dir .. "\\lecturas_p1.txt", "w")
    for a, pc in pairs(reads) do l:write(string.format("%X %X\n", a, pc)) end
    l:close()
    if sma_reg or pvc then
        l = io.open(dir .. "\\lecturas_p2.txt", "w")
        for a, pc in pairs(reads_p2) do l:write(string.format("%X %X\n", a, pc)) end
        l:close()
    end
    local f = io.open(dir .. "\\FIN", "w"); f:write(tostring(part)); f:close()
end)
