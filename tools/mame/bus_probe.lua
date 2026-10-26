-- Black-box probe of the 68000 bus in MAME: at frame $PD_PROBE_FRAME (default 2) runs the
-- accesses in $PD_PROBE (";"-separated), logs what the reads return and exits.
--
--   w16 ADDR VALUE     word write          w8 ADDR VALUE      byte write
--   r16 ADDR           word read           r8 ADDR            byte read
--   dump ADDR WORDS    reads WORDS words from ADDR and logs the ones that are not 0
--   hex ADDR WORDS     reads WORDS words from ADDR and logs them as one hex string
--   (hex numbers; reads and writes go through MAME's handlers, as the 68000's do)
-- Output: $PD_TRACE (bus_probe.txt by default), one line per read: "<command> = <value>".

if pd_bus_probe_loaded then return end
pd_bus_probe_loaded = true

local mem = manager.machine.devices[":maincpu"].spaces["program"]
local scr = manager.machine.screens[":screen"]
local at = tonumber(os.getenv("PD_PROBE_FRAME") or "2")

probe_frame = emu.add_machine_frame_notifier(function()
    if scr:frame_number() ~= at then return end
    local f = io.open(os.getenv("PD_TRACE") or "bus_probe.txt", "w")
    for cmd in string.gmatch(os.getenv("PD_PROBE") or "", "[^;]+") do
        local op, x, y = string.match(cmd, "^%s*(%S+)%s+(%x+)%s*(%x*)%s*$")
        local a, v = tonumber(x, 16), tonumber(y ~= "" and y or "0", 16)
        if op == "w16" then mem:write_u16(a, v)
        elseif op == "w8" then mem:write_u8(a, v)
        elseif op == "r16" then f:write(string.format("%s = %04X\n", cmd, mem:read_u16(a)))
        elseif op == "r8" then f:write(string.format("%s = %02X\n", cmd, mem:read_u8(a)))
        elseif op == "dump" then
            local n = 0
            for i = 0, v - 1 do
                local w = mem:read_u16(a + 2 * i)
                if w ~= 0 then
                    n = n + 1
                    if n <= 64 then f:write(string.format("dump %06X = %04X\n", a + 2 * i, w)) end
                end
            end
            f:write(string.format("%s: %d nonzero words\n", cmd, n))
        elseif op == "hex" then
            local parts = {}
            for i = 0, v - 1 do parts[#parts + 1] = string.format("%04X", mem:read_u16(a + 2 * i)) end
            f:write(string.format("hex %06X %s\n", a, table.concat(parts)))
        end
    end
    f:close()
    manager.machine:exit()
end)
