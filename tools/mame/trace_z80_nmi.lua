-- Timing of the Z80's NMI handling in MAME: the time (us) of each fetch of the NMI vector
-- (program read at 0x0066), of each read of the 68000's command (I/O port 0x00) and of each
-- write to the reply port (0x0C), and of each 68000 command write (0x320000), within
-- PD_FROM_US-PD_TO_US. Output: PD_TRACE, lines "<us> NMI|CMD|REPLY|WRITE68K <value>". The port's
-- equivalent: PD_TRACE_Z80 + PD_TRACE_YM, and --trace-io.
if pd_z80_nmi_loaded then return end
pd_z80_nmi_loaded = true
local z80 = manager.machine.devices[":audiocpu"]
local prog, io_z = z80.spaces["program"], z80.spaces["io"]
local first = tonumber(os.getenv("PD_FROM_US") or "0")
local last = tonumber(os.getenv("PD_TO_US") or "1e12")
local lines = {}
local function now() return manager.machine.time:as_double() * 1e6 end
local function log(kind, v)
    local t = now()
    if t >= first and t <= last then lines[#lines + 1] = string.format("%.3f %s %02X", t, kind, v & 0xFF) end
end
nmi_rtap = prog:install_read_tap(0x0066, 0x0066, "pd_nmi", function(offset, data, mask) log("NMI", data) end)
cmd_rtap = io_z:install_read_tap(0x0000, 0xFFFF, "pd_cmd", function(offset, data, mask)
    if offset & 0xFF == 0x00 then log("CMD", data) end
end)
rep_wtap = io_z:install_write_tap(0x0000, 0xFFFF, "pd_rep", function(offset, data, mask)
    if offset & 0xFF == 0x0C then log("REPLY", data) end
end)
local m68k = manager.machine.devices[":maincpu"].spaces["program"]
w68_tap = m68k:install_write_tap(0x320000, 0x320001, "pd_w68", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then log("WRITE68K", data >> 8) end
end)
nmi_stop = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRACE") or "trace_z80_nmi.txt", "w")
    for _, s in ipairs(lines) do f:write(s .. "\n") end
    f:close()
end)
