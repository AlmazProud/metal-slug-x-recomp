-- Z80 writes to its ports (YM2610 and reply to the 68000) within a time window:
-- time (us), port (low byte) and data. Same format as the port's PD_TRACE_YM.
-- Variables: PD_FROM_US, PD_TO_US, PD_TRACE.
local z80 = manager.machine.devices[":audiocpu"]
local io_z = z80.spaces["io"]
local first = tonumber(os.getenv("PD_FROM_US") or "0")
local last = tonumber(os.getenv("PD_TO_US") or "1e12")
local lines = {}
wtap_ym = io_z:install_write_tap(0x0000, 0xFFFF, "pd_ym", function(offset, data, mask)
    local t = manager.machine.time:as_double() * 1e6
    if t >= first and t <= last then
        table.insert(lines, string.format("%.3f %02X %02X", t, offset & 0xFF, data & 0xFF))
    end
end)
ym_stop_sub = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRACE") or "traza_ym.txt", "w")
    for _, s in ipairs(lines) do f:write(s .. "\n") end
    f:close()
end)
