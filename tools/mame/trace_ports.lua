-- Trace of 68000 I/O reads (0x300000-0x3FFFFF and 0x800000-0xBFFFFF): for each
-- (address, mask, value) counts how many times it was read and in which frame it first appeared.
-- Used to pin down the runtime's port values against the oracle.
-- Output: PD_TRACE (traza_puertos.txt by default).

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local scr = manager.machine.screens[":screen"]
local seen, order = {}, {}

local function record(offset, data, mask)
    local k = string.format("%06X m=%04X v=%04X", offset, mask, data & mask)
    local e = seen[k]
    if not e then
        e = { n = 0, f = scr:frame_number(), pc = cpu.state["PC"].value }
        seen[k] = e
        table.insert(order, k)
    end
    e.n = e.n + 1
end

rtap_io = mem:install_read_tap(0x300000, 0x3BFFFF, "pd_io_r", record)
rtap_mc = mem:install_read_tap(0x800000, 0xBFFFFF, "pd_mc_r", record)

ports_stop_sub = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRACE") or "traza_puertos.txt", "w")
    for _, k in ipairs(order) do
        local e = seen[k]
        f:write(string.format("%s n=%d primer_cuadro=%d pc=%06X\n", k, e.n, e.f, e.pc))
    end
    f:close()
end)
