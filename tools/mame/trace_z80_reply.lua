-- Times of the Z80's reply writes (I/O port 0x0C) and of the 68000's commands to the Z80, in
-- microseconds, between frames PD_FROM and PD_TO (Phase 8: 68000 / Z80 synchronization).
-- Same units as the port's --trace lines "RESP" (Z80 write) and "CMD". Output: PD_TRACE.

local z80 = manager.machine.devices[":audiocpu"]
local cpu = manager.machine.devices[":maincpu"]
local scr = manager.machine.screens[":screen"]
local first = tonumber(os.getenv("PD_FROM") or "0")
local last = tonumber(os.getenv("PD_TO") or "1e12")
local out = io.open(os.getenv("PD_TRACE") or "z80_reply.txt", "w")
local function now() return manager.machine.time:as_double() * 1e6 end
local function active() local f = scr:frame_number(); return f >= first and f <= last end

-- The port number is the low byte of the I/O address (the high byte comes from B or A).
z80_reply_tap = z80.spaces["io"]:install_write_tap(0x0000, 0xFFFF, "pd_z80_reply", function(offset, data, mask)
    if (offset & 0xFF) == 0x0C and active() then
        out:write(string.format("%.3f RESP %02X\n", now(), data & 0xFF))
    end
end)
z80_cmd_tap = cpu.spaces["program"]:install_write_tap(0x320000, 0x320001, "pd_z80_cmd", function(offset, data, mask)
    if active() and (mask & 0xFF00) ~= 0 then
        out:write(string.format("%.3f CMD %02X\n", now(), (data >> 8) & 0xFF))
    end
end)
z80_reply_stop = emu.add_machine_stop_notifier(function() out:close() end)
