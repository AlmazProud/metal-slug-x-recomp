-- Trace of 68000 writes to I/O (0x300000-0x3FFFFF) and palette (0x400000-0x7FFFFF) within
-- a time window, with the machine time (us), the PC and the data. Used to find the first timing
-- divergence against the port (src/neogeo, --trace-io option).
-- Variables: PD_FROM_US, PD_TO_US, PD_TRACE (output).

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local first = tonumber(os.getenv("PD_FROM_US") or "0")
local last = tonumber(os.getenv("PD_TO_US") or "1e12")
local lines = {}

local function record_io(offset, data, mask)
    local t = manager.machine.time:as_double() * 1e6
    if t >= first and t <= last then
        table.insert(lines, string.format("%.3f pc=%06X %06X %04X/%04X", t, cpu.state["PC"].value, offset, data & mask, mask))
    end
end

wtap_es = mem:install_write_tap(0x300000, 0x3FFFFF, "pd_es_w", record_io)
wtap_pal = mem:install_write_tap(0x400000, 0x7FFFFF, "pd_pal_w", record_io)

io_stop_sub = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRACE") or "traza_es.txt", "w")
    for _, s in ipairs(lines) do f:write(s .. "\n") end
    f:close()
end)
