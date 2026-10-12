-- 68000 reads from the input ports (0x300000-0x39FFFF) within a time
-- window: time (us), PC, address, value and mask. Same format as the port's
-- PD_TRACE_READS option. Variables: PD_FROM_US, PD_TO_US, PD_TRACE.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local first = tonumber(os.getenv("PD_FROM_US") or "0")
local last = tonumber(os.getenv("PD_TO_US") or "1e12")
local lines = {}
rtap_lect = mem:install_read_tap(0x300000, 0x39FFFF, "pd_lect", function(offset, data, mask)
    local t = manager.machine.time:as_double() * 1e6
    if t >= first and t <= last and (offset & 0xFE0000) ~= 0x320000 then
        table.insert(lines, string.format("%.3f pc=%06X %06X %04X/%04X", t, cpu.state["PC"].value, offset, data & mask, mask))
    end
end)
reads_stop_sub = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRACE") or "traza_lecturas.txt", "w")
    for _, s in ipairs(lines) do f:write(s .. "\n") end
    f:close()
end)
