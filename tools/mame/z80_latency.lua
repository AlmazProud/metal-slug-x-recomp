-- Z80 latency: time between each 68000 command (write to 0x320000) and each
-- Z80 reply (write to its port 0x0C), with the machine time in microseconds.
-- Output: PD_TRACE (latencia_z80.txt by default).

local cpu = manager.machine.devices[":maincpu"]
local z80 = manager.machine.devices[":audiocpu"]
local mem = cpu.spaces["program"]
local io_z80 = z80.spaces["io"]
local scr = manager.machine.screens[":screen"]
local lines = {}
local function vpos()
    local ok, v = pcall(function() return scr:vpos() end)
    return ok and v or -1
end

local function us()
    local ok, v = pcall(function() return manager.machine.time:as_double() * 1e6 end)
    if ok then return v end
    table.insert(lines, "ERROR tiempo: " .. tostring(v))
    return -1
end

wtap_cmd = mem:install_write_tap(0x320000, 0x320001, "pd_cmd", function(offset, data, mask)
    if mask & 0xFF00 ~= 0 then
        table.insert(lines, string.format("%.3f f=%d v=%d CMD %02X", us(), scr:frame_number(), vpos(), (data >> 8) & 0xFF))
    end
end)

wtap_resp = io_z80:install_write_tap(0x0000, 0xFFFF, "pd_resp", function(offset, data, mask)
    if offset & 0xFF ~= 0x0C then return end
    table.insert(lines, string.format("%.3f f=%d v=%d RESP %02X", us(), scr:frame_number(), vpos(), data & 0xFF))
end)

-- First 68000 read that sees each new reply value (it can come later than the
-- Z80 write because of the execution slices of MAME's scheduler).
local seen = -1
rtap_resp = mem:install_read_tap(0x320000, 0x320001, "pd_resp_r", function(offset, data, mask)
    if mask & 0xFF00 == 0 then return end
    local v = (data >> 8) & 0xFF
    if v ~= seen then
        table.insert(lines, string.format("%.3f f=%d v=%d VISTO %02X", us(), scr:frame_number(), vpos(), v))
        seen = v
    end
end)

latency_stop_sub = emu.add_machine_stop_notifier(function()
    local f = io.open(os.getenv("PD_TRACE") or "latencia_z80.txt", "w")
    for _, s in ipairs(lines) do f:write(s .. "\n") end
    f:close()
end)
