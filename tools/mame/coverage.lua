-- Dynamic 68000 coverage in MAME (requires -debug -debugger none).
-- Launched by tools/coverage.py; not used on its own.
--
-- Environment variables:
--   PD_TRAZA_DIR  folder (path WITHOUT spaces, e.g. 8.3) where traza_NNNN.txt files are written
--   PD_FRAMES     frames per trace file (300 by default)
--   PD_JUGAR      1 = simple bot that inserts coins and plays (1 by default)
--
-- Every P2 bank change (write to 0x2FFFF0) is noted in the trace as
-- "BANK n" with tracelog, so the analyzer knows which bank is active.

local dbg = manager.machine.debugger
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local screen = manager.machine.screens[":screen"]
local dir = os.getenv("PD_TRAZA_DIR")
local per_file = tonumber(os.getenv("PD_FRAMES") or "300")
local play = (os.getenv("PD_JUGAR") or "1") == "1"
local part = 0

local function new_trace()
    dbg:command(string.format("trace %s\\traza_%04d.txt,maincpu,noloop", dir, part))
    part = part + 1
end

bank_tap = mem:install_write_tap(0x2FFFF0, 0x2FFFFF, "pd_cov_bank", function(offset, data, mask)
    dbg:command(string.format('tracelog "BANK %d\\n"', data & 7))
end)

-- Reads from P1. They include instruction fetches; coverage.py subtracts the bytes
-- of executed instructions to keep only the data reads.
local reads = {}
read_tap = mem:install_read_tap(0x000000, 0x0FFFFF, "pd_cov_rd", function(offset, data, mask)
    reads[offset & 0xFFFFE] = true
end)

-- Input bot: it does not try to play well, only to walk through as much code as possible.
local fields = {}
for _, port in pairs(manager.machine.ioport.ports) do
    for name, field in pairs(port.fields) do fields[name] = field end
end
local function set(name, v) if fields[name] then fields[name]:set_value(v) end end

frame_sub = emu.add_machine_frame_notifier(function()
    local f = screen:frame_number()
    if f % per_file == 0 then new_trace() end
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
    for a in pairs(reads) do l:write(string.format("%X\n", a)) end
    l:close()
    local f = io.open(dir .. "\\FIN", "w"); f:write(tostring(part)); f:close()
end)
